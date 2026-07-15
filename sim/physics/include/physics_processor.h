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
    
    // default to phase 2 N/S straight 
    int currentPhase = 2; 
    bool isInitialized = false; 
    
    // [0] = N/S, [1] = E/W
    std::vector<Road*> axisEdges[2]; 
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
        bool checkLeftTurnDemand(Node* node);

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
        
        bool canVehicleEnter(VehicleState* vhcl, Node* destNode);
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
        bool hasSafeGap(VehicleState* yieldingCar, Node* destNode, float criticalGapSeconds);
};

#endif