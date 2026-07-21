#ifndef PHYSICS_PROCESSOR_H
#define PHYSICS_PROCESSOR_H

#include "vehicle_state.h"
#include "network.h"
#include "spatial_hash.h"
#include <vector>
#include <queue>          
#include <unordered_map>
#include <map>
#include <utility>

// keep track of intersection queues and traffic lights
struct IntersectionState {
    std::queue<VehicleState*> waitQueue;
    VehicleState* currentOccupant = nullptr;
    // Watchdog: seconds the current occupant has held the intersection.
    // Released past a generous ceiling so a wedged occupant (downstream
    // spillback, stale queue entry) degrades to a slow intersection instead
    // of freezing every approach forever.
    float occupantHeldTime = 0.0f;
    float lightTimer = 0.0f;
    // Seconds since the last demand scan during a green phase; scans hit the
    // spatial hash, so they poll at 2 Hz instead of every frame.
    float demandPollTimer = 0.0f;

    // default to phase 2 N/S straight
    int currentPhase = 2;
    bool isInitialized = false;

    // Per-phase durations in seconds, indices matching the 10-phase ring in
    // updateIntersections. initializeLightAxes overwrites the greens/yellows
    // from the axes' speed limits and lane counts; these values are the
    // fallback for lights whose axes carry no usable road data.
    float phaseDurations[10] = {6.0f, 3.0f, 15.0f, 4.0f, 2.0f, 6.0f, 3.0f, 15.0f, 4.0f, 2.0f};

    // [0] = N/S, [1] = E/W
    std::vector<Road*> axisEdges[2];

    // Axis approaches grouped by direction of travel: [axis][0] holds the
    // legs roughly aligned with the axis reference direction (the baseline
    // for axis 0, baseline+90 for axis 1), [axis][1] the opposing legs.
    // Always populated; consumed by split phasing and per-approach lamp
    // colors.
    std::vector<Road*> axisLegEdges[2][2];

    // Split ("solo") phasing: an axis whose approaches are all 1-2 lanes
    // wide has no room for a left-turn pocket, so mirroring one green to
    // both directions puts permissive lefts and opposing through traffic in
    // the box together. A split axis instead serves each direction alone
    // with every movement protected: phase 0/5 becomes the leg-0 solo green
    // and phase 2/7 the leg-1 solo green (ring shape unchanged). Axes with a
    // single leg (T stems, one-way pairs) are solo by construction.
    bool axisSplit[2] = {false, false};

    // Multi-node junction coordination (see rebuildSignalClusters): every
    // light of one physical junction cluster mirrors the master light's
    // phase, so the perimeter nodes can never show conflicting greens into
    // the shared box. bInCluster is set on every clustered light; the master
    // (lowest light id) runs the actuated ring, the rest copy it each tick.
    bool bInCluster = false;
    uint64_t syncMasterId = 0;
    // Shared compass baseline (degrees) for axis grouping, so "axis 0" means
    // the same physical direction at every node of the cluster. Negative =
    // unclustered, derive the baseline from the node's own first edge.
    double axisBaselineDeg = -1.0;
};

class PhysicsProcessor 
{
    public:
        inline void setNetwork(Network* net) { network = net; } // physics can read nodes now
        std::string getUpcomingTurnDirection(VehicleState* vhcl); // testing turn lanes
        // Turn made at route node 'nodeIndex' (between the edges into and out
        // of it); getUpcomingTurnDirection is this at the current edge's end.
        std::string getTurnDirectionAt(VehicleState* vhcl, size_t nodeIndex);

        void update(float dt);
        float IDM(VehicleState* vhcl, VehicleState* leader, bool mobil); // now takes leader for MOBIL to use
        void addVehicle(VehicleState* vhcl);
        float MOBIL(VehicleState* vhcl, int targetLane);
        // True when a stopped car near one of 'axis's stop lines wants to
        // turn left -- the sensor that decides whether a protected-left
        // phase is worth serving.
        bool checkLeftTurnDemand(Node* node, const IntersectionState& state, int axis);

        // Rebuilds every light's axis grouping and phase timings from live
        // topology, drops state for deleted nodes, and creates it for newly
        // promoted lights. Call after ANY runtime road edit: mutating a
        // node's outgoingEdges vector can reallocate it, dangling the Road*
        // held by every light fed from that node -- not just the lights at
        // the edited endpoints.
        void refreshIntersectionStates();

        VehicleState* getLeader(VehicleState* vhcl, int targetLane);
        VehicleState* getFollower(VehicleState* vhcl, int targetLane);

        // getter for frontend lights
        const std::unordered_map<uint64_t, IntersectionState>& getIntersections() const { 
            return intersections; 
        }
        
        // Helper to get the length of a specific segment in a vehicle's route
        float getRouteSegmentLength(VehicleState* vhcl, int routeIndex);
        // Helper to calculate the true gap across multiple edges
        float calculateTrueGap(VehicleState* follower, VehicleState* leader);

        float calculateDistanceToDestination(VehicleState* vhcl);

        const std::vector<VehicleState*>& getActiveVehicles() const {
            return vehicleList;
        }

        // Immediately removes and destroys a vehicle (e.g. the road under it
        // was deleted). Clears other vehicles' leader references to it. Must
        // only be called between update() calls, never from inside one.
        void despawnVehicle(VehicleState* vhcl);

        PhysicsProcessor(Network* mapNetwork, VehicleSpatialHash* spatialObj);
        ~PhysicsProcessor();

    private:
        Network* network;
        std::vector<VehicleState*> vehicleList;
        std::vector<float> vehicleUpdates;
        VehicleSpatialHash* spatialHash; // Store the pointer here

        // memory management
        std::vector<VehicleState*> vehiclesToRemove;
        std::vector<VehicleState*> vehiclesToDestroy;
        
        // intersection stuff
        std::unordered_map<uint64_t, IntersectionState> intersections;
        std::map<std::pair<Road*, int>, VehicleState*> ghostVehicles; // ghost vehicles for each lane in intersection

        // Multi-node junction clusters: controlled nodes joined by internal
        // junction legs are one physical intersection. Maps every member
        // node id to the full member list (including itself); nodes not part
        // of any cluster are absent. Rebuilt with the intersection states.
        std::unordered_map<uint64_t, std::vector<uint64_t>> junctionClusterOf;

        // Recomputes junctionClusterOf and stamps every clustered light's
        // sync master + shared axis baseline. Must run before axes are
        // (re)built so the baseline is in place when edges are grouped.
        void rebuildSignalClusters();

        // Demand for 'axis' anywhere in node's cluster (or just at node when
        // unclustered) -- what the master's ring actuates on, so a car at
        // any perimeter node can call up its own green.
        bool clusterAxisHasDemand(Node* node, const IntersectionState& state, int axis);
        bool clusterLeftTurnDemand(Node* node, const IntersectionState& state, int axis);
        // Per-direction demand for split-phased axes: leg 0 is the
        // baseline-aligned direction, leg 1 the opposing one.
        bool legHasDemand(Node* node, const IntersectionState& state, int axis, int leg);
        bool clusterLegHasDemand(Node* node, const IntersectionState& state, int axis, int leg);
        // Any car approaching (or nosing past) road's stop line into node --
        // the shared sensor behind axisHasDemand and legHasDemand.
        bool approachHasDemand(Road* road, Node* node);

        // A clustered junction's perimeter lights each see only their own
        // approaches (a divided road's two directions enter at different
        // member nodes), so the split-phasing decision must pool the whole
        // cluster; one member running solo phases while another mirrors
        // conventional ones would put conflicting greens in the shared box.
        // Runs after every full axis (re)build.
        void syncClusterSplitPhasing();

        // Seconds vhcl needs to fully clear destNode's junction box for its
        // specific movement: path length through the box (internal cluster
        // legs included) until the car's tail passes the exit-side boundary,
        // covered accelerating from its current speed toward the movement's
        // junction pacing. Gap acceptance adds this to its required gap so a
        // granted car is fully across before conflicting traffic arrives.
        float estimateCrossingSeconds(VehicleState* vhcl, Node* destNode);

        // The movement at the end of the current edge doubles back to the
        // node the car came from (A -> B -> A). The chord classifier cannot
        // call this case -- an anti-parallel pair's cross product is
        // numerical noise, splitting U-turns randomly between "left" and
        // "right" -- so signal and gap logic ask topology instead.
        bool isUpcomingUTurn(VehicleState* vhcl) const;

        // Movement across the WHOLE junction box entered at the end of the
        // current edge. At a multi-node cluster the immediate next hop is an
        // internal leg that reads "through" while the actual turn happens
        // mid-box (where nothing gates it), so the perimeter gate must
        // classify from the approach direction to the first non-internal
        // (box exit) edge. Plain single-node intersections fall back to the
        // chord classifier unchanged. outUTurn is set for topological
        // U-turns and for box paths that reverse direction; callers treat
        // those as lefts with the box swept edge-to-edge.
        std::string getUpcomingBoxMovement(VehicleState* vhcl, bool& outUTurn);
        // getUpcomingBoxMovement generalized to any node path: the movement
        // at route[i+1] entered from route[i], walking internal legs to the
        // box exit. Lets the wrong-lane reroute classify a candidate tail
        // with exactly the gate's rule.
        std::string boxMovementOnRoute(const std::vector<uint64_t>& route, size_t i, bool& outUTurn);

        // Dest node id of the first non-internal edge along vhcl's route
        // from its next hop onward -- where the car re-emerges from the
        // junction fabric (at a plain node: simply route[i+2]). 0 when the
        // route ends first. Used to compare paths across a cluster, where
        // route[i+2] alone is an internal node id.
        uint64_t boxExitDestId(VehicleState* vhcl);

        bool canVehicleEnter(VehicleState* vhcl, Node* destNode);
        // Signal/right-of-way decision for destNode: traffic-light phase,
        // stop-sign FIFO (including queue-admission side effects), yield gap.
        // Split from canVehicleEnter so the wrong-lane gate can ask "would I
        // otherwise be let through?" before choosing between holding to merge
        // and rerouting.
        bool controlGrantsEntry(VehicleState* vhcl, Node* destNode);
        // Wrong-lane escape: replans the tail of vhcl's route so its first
        // movement out of destNode is one its current lane allows, splicing
        // the cheapest complete alternative into currentRoute. False when no
        // allowed exit reaches the destination -- the caller then falls back
        // to the wrong-lane turn.
        bool tryRerouteAroundWrongLaneTurn(VehicleState* vhcl, Road* approach, Node* destNode);
        // True when the lane vhcl lands in on its exit edge out of destNode
        // has room for the whole car beyond the junction box -- the
        // don't-block-the-box gate inside canVehicleEnter.
        bool exitLaneHasRoom(VehicleState* vhcl, Node* destNode);
        // Sets the frame's desired speed: the next road's speed limit while
        // crossing a junction box (scaled down for turning movements, so a
        // turn onto a fast arterial sweeps faster than one into a side
        // street), the current road's limit everywhere else.
        void applyJunctionTargetSpeed(VehicleState* vhcl);
        // Geometry-derived comfortable corner speed (m/s) for a car turning
        // from 'approach' onto 'exitRoad' through 'node', capped at exitLimit.
        // Falls back to exitLimit (no reduction) when edge tangents are
        // unavailable. Wraps RoadIntersectionUtil::CornerSpeed so the live
        // junction pacing and the crossing-time estimate share one model.
        float cornerSpeedForEdges(const Road* approach, const Road* exitRoad,
                                  const Node* node, float aLat, float exitLimit);
        // True when vhcl is at destNode's stop line with no same-lane car
        // between it and the line -- the only state from which a granted car
        // can actually enter the junction box.
        bool isAtStopLine(VehicleState* vhcl, Node* destNode);
        void updateIntersections(float dt);
        // Groups a light's incoming edges into the two signal axes; called for
        // every TRAFFIC_LIGHT node at construction and lazily for nodes added
        // at runtime.
        void initializeLightAxes(Node* node, IntersectionState& state);
        // True when any car is approaching (or waiting at) one of 'axis's
        // stop lines -- the demand signal behind phase skipping, gap-out,
        // and rest-on-green.
        bool axisHasDemand(Node* node, const IntersectionState& state, int axis);
        bool hasSafeGap(VehicleState* yieldingCar, Node* destNode, float criticalGapSeconds);
};

#endif