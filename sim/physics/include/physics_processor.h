#ifndef PHYSICS_PROCESSOR_H
#define PHYSICS_PROCESSOR_H

#include "vehicle_state.h"
#include <vector>

class PhysicsProcessor 
{
    public:
        void update(float dt);
        float IDM(VehicleState* vhcl);
        void addVehicle(VehicleState* vhcl);
        bool MOBIL(VehicleState* vhcl, int targetLane);

        PhysicsProcessor();
        ~PhysicsProcessor();

    private:
        std::vector<VehicleState*> vehicleList;
        std::vector<float> vehicleUpdates;        
};



#endif