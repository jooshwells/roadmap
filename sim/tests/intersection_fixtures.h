#ifndef INTERSECTION_FIXTURES_H
#define INTERSECTION_FIXTURES_H

// Scaffolding for the intersection tests: a single junction with arms long
// enough to approach it at speed, wired the way the JSONL loader wires a real
// map, and driven through the real PhysicsProcessor::update() loop.
//
// Unlike StraightRoadFixture, this fixture does NOT own its vehicles.
// PhysicsProcessor::addVehicle takes ownership, and update() deletes cars that
// reach their destination, so anything the tests hold must be checked for
// liveness through getActiveVehicles() before it is dereferenced.

#include "idm_profiles.h"
#include "intersection_geometry.h"
#include "network.h"
#include "physics_processor.h"
#include "spatial_hash.h"
#include "vehicle_state.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

class IntersectionFixture
{
    public:
        // A full cross (4 legs) or a T with no west arm (3 legs). The leg
        // count decides what Network::calculateIntersectionPriorities does
        // with a stop: 4 legs stay an all-way stop, 3 legs are downgraded to
        // a yield with a major/minor split.
        enum class Arms { Cross, Tee };

        // The centre node id is not arbitrary. The PhysicsProcessor
        // constructor staggers each light's start-up timer by a node-id hash
        // (so a whole map's signals don't run in lockstep); 1024 is an id
        // whose hash lands at the very start of the first green, which gives
        // the approach tests a predictable ~full phase of initial red instead
        // of leaving it to the hash.
        static constexpr uint64_t Centre = 1024;
        static constexpr uint64_t North = 10;
        static constexpr uint64_t South = 11;
        static constexpr uint64_t East  = 12;
        static constexpr uint64_t West  = 13;

        // 'control' is the node type string the JSONL loader uses: "signal",
        // "stop", "yield" or "none". In Tee mode the east arm is the stem and
        // takes the minor* road parameters, which is what makes it the minor
        // road once priorities are assigned.
        IntersectionFixture(const std::string& control,
                            Arms arms = Arms::Cross,
                            int lanes = 3,
                            double speedLimit = 15.0,
                            double armLength = 400.0,
                            int minorLanes = 0,
                            double minorSpeed = 0.0)
            : armMeters(armLength)
        {
            if (minorLanes <= 0) minorLanes = lanes;
            if (minorSpeed <= 0.0) minorSpeed = speedLimit;

            network.addNode(Centre, 0.0, 0.0, 0.0, 0.0, control);
            network.addNode(North, 0.0, 0.0, 0.0, armLength, "none");
            network.addNode(South, 0.0, 0.0, 0.0, -armLength, "none");
            network.addNode(East, 0.0, 0.0, armLength, 0.0, "none");
            if (arms == Arms::Cross) network.addNode(West, 0.0, 0.0, -armLength, 0.0, "none");

            // North first, deliberately: a light's axis 0 is defined by the
            // bearing of the centre's FIRST incoming edge, so adding the
            // north arm first makes axis 0 the north/south street and axis 1
            // the east/west one. The tests rely on that (the ring starts on
            // an axis-0 green, so an east/west approach begins on red).
            link(North, armLength, lanes, speedLimit);
            link(South, armLength, lanes, speedLimit);
            link(East, armLength, minorLanes, minorSpeed);
            if (arms == Arms::Cross) link(West, armLength, lanes, speedLimit);

            // Same order the load pipeline uses. This is what converts a
            // 3-leg stop into a yield and fills in minorRoadOriginIds.
            network.calculateIntersectionPriorities();

            // Built last so its constructor snapshots the finished topology
            // and the final control types, exactly as the real sim does.
            physics.reset(new PhysicsProcessor(&network, &spatialHash));
        }

        IntersectionFixture(const IntersectionFixture&) = delete;
        IntersectionFixture& operator=(const IntersectionFixture&) = delete;

        // A vehicle on the edge route[0] -> route[1], routed onward through
        // the junction. Ownership passes to the PhysicsProcessor. Returns
        // nullptr if the route could not be bound to an edge (addVehicle
        // destroys the car in that case).
        VehicleState* spawn(const std::vector<uint64_t>& route, float speed, float pos,
                            int lane = 0,
                            const IDMParameters& params = IDM_Profiles::getBasicDriverProfile())
        {
            VehicleState* v = new VehicleState(route.front(), route.back(), speed, pos, lane, params);
            v->currentRoute = route;
            v->currentRouteIndex = 0;
            physics->addVehicle(v);

            const std::vector<VehicleState*>& active = physics->getActiveVehicles();
            if (active.empty() || active.back() != v) return nullptr;
            return v;
        }

        void step(float dt) { physics->update(dt); }

        bool isAlive(VehicleState* v) const
        {
            const std::vector<VehicleState*>& active = physics->getActiveVehicles();
            return std::find(active.begin(), active.end(), v) != active.end();
        }

        // Arc position of the stop line on the edge the vehicle is currently
        // on -- the junction-box boundary, not the node centre.
        float stopLineFor(VehicleState* v)
        {
            Road* edge = v->getCurrentEdge();
            if (edge == nullptr) return 0.0f;
            Node* dest = network.getNode(edge->getDest());
            if (dest == nullptr) return static_cast<float>(edge->getLength());
            return RoadIntersectionUtil::GetStopLineArcPos(
                &network, *edge, *dest, RoadIntersectionUtil::MedianGapMeters);
        }

        float distanceToStopLine(VehicleState* v) { return stopLineFor(v) - v->getPos(); }

        // True when the intersection is currently denying this car entry.
        //
        // The signal/stop/yield logic itself is private, but its effect is
        // public and is precisely what the vehicle feels: a denied car is
        // handed a "ghost" leader parked at the stop line, which its IDM then
        // brakes for like any other obstacle. Ghosts are built with a zero
        // length (see PhysicsProcessor::getLeader), which is what
        // distinguishes one from a real car.
        bool heldByControl(VehicleState* v)
        {
            VehicleState* leader = physics->getLeader(v, v->getLane());
            return leader != nullptr && leader->getLength() <= 0.001f;
        }

        // Current phase index of the centre light (0-9 ring). Returns -1 for
        // junctions that are not signalized.
        int phase() const
        {
            const auto& intersections = physics->getIntersections();
            auto it = intersections.find(Centre);
            return (it == intersections.end()) ? -1 : it->second.currentPhase;
        }

        // Pins a vehicle to a known state so a test can interrogate the
        // control logic at a fixed distance and speed instead of chasing a
        // moving car. VehicleState exposes no speed setter, so the speed is
        // set by accelerating through the difference.
        static void freeze(VehicleState* v, float pos, float speed, int lane)
        {
            v->setPos(pos);
            v->accelerate(speed - v->getSpeed());
            v->setLane(lane);
        }

        Node* centreNode() { return network.getNode(Centre); }
        double armLength() const { return armMeters; }

        Network network;
        VehicleSpatialHash spatialHash;
        std::unique_ptr<PhysicsProcessor> physics;

    private:
        void link(uint64_t arm, double length, int lanes, double speedLimit)
        {
            network.addDirectedEdge(arm, Centre, length, speedLimit, lanes);
            network.addDirectedEdge(Centre, arm, length, speedLimit, lanes);
        }

        double armMeters;
};

#endif
