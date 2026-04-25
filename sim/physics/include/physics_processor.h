#ifndef PHYSICS_PROCESSOR_H
#define PHYSICS_PROCESSOR_H

#include "vehicle_state.h"
#include "network.h"
#include <vector>

class PhysicsProcessor 
{
    public:
        void update(float dt);
        float IDM(VehicleState* vhcl);
        void addVehicle(VehicleState* vhcl);

        // Helper to get the length of a specific segment in a vehicle's route
        float getRouteSegmentLength(VehicleState* vhcl, int routeIndex);
        // Helper to calculate the true gap across multiple edges
        float calculateTrueGap(VehicleState* follower, VehicleState* leader);

        PhysicsProcessor(Network* mapNetwork);
        ~PhysicsProcessor();

    private:
        Network* network;
        std::vector<VehicleState*> vehicleList;
        std::vector<float> vehicleUpdates;

        std::vector<VehicleState*> vehiclesToRemove;
        
};



#endif