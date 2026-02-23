#include "physics_processor.h"
#include "vehicle_state.h"
#include <vector>
#include <math.h>

PhysicsProcessor::PhysicsProcessor() : vehicleList(), vehicleUpdates() {}

void PhysicsProcessor::update(float dt)
{
    // Calculate physics updates for each vehicle in our master list
    // and store them in vehicleUpdates (so previous speed/pos changes
    // don't affect subsequent changes)
    for (VehicleState* vhcl : vehicleList)
    {
        VehicleState* leader = vhcl->getLeader();
        float gap = 0;
        float brakePower = 0;

        // check braking logic
        if(leader != nullptr) {       
            gap = (leader->getPos()) - (vhcl->getPos());

            // just tesing with gap of 20 for now
            if(gap < 20.0f) {
                brakePower = -4.0f * dt;
                // vhcl->accelerate(brakePower);
            }
        }
        
        // no negative speed 
        if (vhcl->getSpeed() + brakePower < 0.0f) {
            brakePower = 0;
        }

        vehicleUpdates.push_back(brakePower);
    }

    int i = 0;

    // Actually perform all of the updates on our vehicles
    for (VehicleState* vhcl : vehicleList)
    {
        vhcl->accelerate(vehicleUpdates[i]);
        vhcl->move(vhcl->getSpeed() * dt);
        
        i++;
    }
}

float PhysicsProcessor::IDM(VehicleState* vhcl)
{
    float freeRoadRatio = pow((vhcl->getSpeed() / vhcl->getDesiredSpeed()), vhcl->getAccelExp());
}

void PhysicsProcessor::addVehicle(VehicleState* vhcl)
{
    vehicleList.push_back(vhcl);
}

PhysicsProcessor::~PhysicsProcessor() 
{
    for (VehicleState* v : vehicleList)
    {
        delete(v);
    }
}

