#ifndef SIM_FIXTURES_H
#define SIM_FIXTURES_H

// Shared scaffolding for the simulation unit tests: the smallest world in
// which PhysicsProcessor::IDM and ::MOBIL can be called for real. Both
// dereference the vehicle's current edge, and MOBIL walks the spatial hash,
// so a bare VehicleState is not enough -- a two-node network is.

#include "idm_profiles.h"
#include "network.h"
#include "physics_processor.h"
#include "spatial_hash.h"
#include "vehicle_state.h"

#include <cmath>
#include <memory>
#include <vector>

// One straight PASS_THROUGH road with a live PhysicsProcessor over it. Both
// endpoints are uncontrolled and the end node has a single neighbour, so
// IsIntersectionNode() is false there: no signal phase, stop-sign queue,
// don't-block-the-box check, or ghost leader can enter the picture. What the
// vehicles do is then purely the car-following model, which is what these
// tests are about.
class StraightRoadFixture
{
    public:
        static constexpr uint64_t StartNodeId = 1;
        static constexpr uint64_t EndNodeId   = 2;

        // The default road is deliberately very long: several tests integrate
        // for minutes of sim time at highway speed, and a vehicle running off
        // the end would start reading route segments that do not exist.
        explicit StraightRoadFixture(double lengthMeters = 50000.0,
                                     int lanes = 1,
                                     double speedLimit = 30.0)
        {
            network.addNode(StartNodeId, 0.0, 0.0, 0.0, 0.0);
            network.addNode(EndNodeId, 0.0, 0.0, lengthMeters, 0.0);
            network.addDirectedEdge(StartNodeId, EndNodeId, lengthMeters, speedLimit, lanes);

            // Built after the graph so the processor's constructor sees the
            // finished topology, exactly as the real sim does.
            physics.reset(new PhysicsProcessor(&network, &spatialHash));
            road = &network.getNode(StartNodeId)->outgoingEdges.front();
        }

        ~StraightRoadFixture()
        {
            for (VehicleState* v : vehicles) delete v;
        }

        StraightRoadFixture(const StraightRoadFixture&) = delete;
        StraightRoadFixture& operator=(const StraightRoadFixture&) = delete;

        // A vehicle bound to the test road, already routed across it. Owned by
        // the fixture; do not delete. 'pos' is the front bumper's distance
        // along the edge, matching the simulator's convention.
        VehicleState* spawn(float speed, float pos, int lane = 0,
                            const IDMParameters& params = IDM_Profiles::getBasicDriverProfile())
        {
            VehicleState* v = new VehicleState(StartNodeId, EndNodeId, speed, pos, lane, params);
            v->currentRoute = {StartNodeId, EndNodeId};
            v->currentRouteIndex = 0;
            v->setCurrentEdge(road);
            vehicles.push_back(v);
            return v;
        }

        // Convenience wrapper: the acceleration IDM asks of 'v' this frame.
        float idm(VehicleState* v, VehicleState* leader) const
        {
            return physics->IDM(v, leader, false);
        }

        // Mirrors the start of PhysicsProcessor::update(): rebuild the spatial
        // hash from the live vehicle list, then hand every car the leader it
        // would see this frame. MOBIL reads both.
        void refreshLeaders()
        {
            spatialHash.rebuild(vehicles);
            for (VehicleState* v : vehicles)
            {
                v->setLeader(physics->getLeader(v, v->getLane()));
            }
        }

        // The exact integration the physics loop uses (PASS 1 / PASS 2 of
        // PhysicsProcessor::update): dv is clamped so speed never goes
        // negative, then the car moves at its new speed. Kept identical so a
        // test's trajectory is the sim's trajectory, minus routing.
        static void integrate(VehicleState* v, float accel, float dt)
        {
            float dv = accel * dt;
            if (v->getSpeed() + dv < 0.0f) dv = -v->getSpeed();
            v->accelerate(dv);
            v->move(v->getSpeed() * dt);
        }

        // Bumper-to-bumper gap the model sees between two cars on this road.
        float gap(VehicleState* follower, VehicleState* leader) const
        {
            return physics->calculateTrueGap(follower, leader);
        }

        Network network;
        VehicleSpatialHash spatialHash;
        std::unique_ptr<PhysicsProcessor> physics;
        Road* road = nullptr;
        std::vector<VehicleState*> vehicles;
};

// The physical deceleration clamp inside PhysicsProcessor::IDM: tyres lose
// grip around -9.8 m/s^2, so the model never asks for more than -10. Kept
// here so the "maximum braking" tests state the same number the code does.
constexpr float MaxPhysicalDecel = -10.0f;

// Closed-form IDM steady-state following distance for a car cruising at
// 'speed' behind a leader at the same speed:
//     s_eq = (s0 + v*T) / sqrt(1 - (v/v0)^delta)
// Both IDM terms cancel exactly at this gap, so a settled follower must sit
// here. Derived from the model, not from the implementation -- that is the
// point of comparing against it.
inline float idmEquilibriumGap(const VehicleState* v, float speed)
{
    const float freeRoadRatio = std::pow(speed / v->getDesiredSpeed(), v->getAccelExp());
    const float dynamicGap = v->getMinGap() + speed * v->getSafeTimeHeadway();
    return dynamicGap / std::sqrt(1.0f - freeRoadRatio);
}

// The IDM safe headway s* for the given state -- the gap the driver wants
// right now, given how fast it is closing on its leader.
inline float idmDesiredGap(const VehicleState* v, float leaderSpeed)
{
    const float approachSpeed = v->getSpeed() - leaderSpeed;
    const float bottom = 2.0f * std::sqrt(v->getMaxAccel() * v->getSafeBrakePower());
    const float dynamic = v->getSpeed() * v->getSafeTimeHeadway() + (v->getSpeed() * approachSpeed) / bottom;
    return v->getMinGap() + std::max(0.0f, dynamic);
}

#endif
