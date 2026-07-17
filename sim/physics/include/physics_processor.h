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