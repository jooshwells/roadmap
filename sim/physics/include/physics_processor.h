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

        void update(float dt);
        float IDM(VehicleState* vhcl, VehicleState* leader, bool mobil); // now takes leader for MOBIL to use
        void addVehicle(VehicleState* vhcl);
        float MOBIL(VehicleState* vhcl, int targetLane);
        bool checkLeftTurnDemand(Node* node);

        VehicleState* getLeader(VehicleState* vhcl, int targetLane);
        VehicleState* getFollower(VehicleState* vhcl, int targetLane);
        
        // Helper to get the length of a specific segment in a vehicle's route
        float getRouteSegmentLength(VehicleState* vhcl, int routeIndex);
        // Helper to calculate the true gap across multiple edges
        float calculateTrueGap(VehicleState* follower, VehicleState* leader);

        float calculateDistanceToDestination(VehicleState* vhcl);

        const std::vector<VehicleState*>& getActiveVehicles() const { 
            return vehicleList; 
        }

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
        void updateIntersections(float dt);
        bool hasSafeGap(VehicleState* yieldingCar, Node* destNode, float criticalGapSeconds);
};

#endif