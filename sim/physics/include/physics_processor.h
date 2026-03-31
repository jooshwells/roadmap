#ifndef PHYSICS_PROCESSOR_H
#define PHYSICS_PROCESSOR_H

#include "vehicle_state.h"
#include <vector>

class PhysicsProcessor 
{
    public:
        void update(float dt);
        float IDM(VehicleState* vhcl, VehicleState* leader); // now takes leader for MOBIL to use
        void addVehicle(VehicleState* vhcl);
        bool MOBIL(VehicleState* vhcl, int targetLane);


        VehicleState* getLeader(VehicleState* vhcl, int targetLane);
        VehicleState* getFollower(VehicleState* vhcl, int targetLane);
        
        PhysicsProcessor();
        ~PhysicsProcessor();

    private:
        std::vector<VehicleState*> vehicleList;
        std::vector<float> vehicleUpdates;        
};



#endif