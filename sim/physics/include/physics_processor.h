#ifndef PHYSICS_PROCESSOR_H
#define PHYSICS_PROCESSOR_H

#include "vehicle_state.h"
#include <vector>

struct PhysicsUpdate 
{
    float brakePower;
    float newPosition;

    PhysicsUpdate(float a, float b) : brakePower(a), newPosition(b) {}
};

class PhysicsProcessor 
{
    public:
        void update(float dt);
        void addVehicle(VehicleState* vhcl);

        PhysicsProcessor();
        ~PhysicsProcessor();

    private:
        std::vector<VehicleState*> vehicleList;
        std::vector<PhysicsUpdate> vehicleUpdates;        
};



#endif