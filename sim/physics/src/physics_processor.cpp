#include "physics_processor.h"
#include "vehicle_state.h"
#include <vector>
#include <math.h>

PhysicsProcessor::PhysicsProcessor() : vehicleList(), vehicleUpdates() {}

void PhysicsProcessor::update(float dt)
{

    vehicleUpdates.clear();
    
    // Calculate physics updates for each vehicle in our master list
    // and store them in vehicleUpdates (so previous speed/pos changes
    // don't affect subsequent changes)
    for (VehicleState* vhcl : vehicleList)
    {
        float acceleration = IDM(vhcl);
        float dv = acceleration *dt; 

        // no negative speed 
        if (vhcl->getSpeed() + dv < 0.0f) {
            dv = -vhcl->getSpeed();
        }
        vehicleUpdates.push_back(dv);
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
    // free road (no cars ahead)
    float freeRoadRatio = pow((vhcl->getSpeed() / vhcl->getDesiredSpeed()), vhcl->getAccelExp());

    // add logic for "interatction term"
    float interactionTerm = 0.0f;
    VehicleState* leader = vhcl->getLeader();
    if(leader != nullptr) {
        float currGap = leader->getPos() - vhcl->getPos() - leader->getLength(); // updated to factor in length

        if (currGap <=0.0001f) currGap=0.001f; // prevent division by 0
        float deltaV = vhcl->getSpeed() - leader->getSpeed(); // how fast car is approaching
        
        //rightmost fraction part of S*()
        float top = vhcl->getSpeed() * deltaV; //curr speed* delta
        float bottom = 2.0f * sqrt(vhcl->getMaxAccel() * vhcl->getSafeBrakePower()); //2 *sqrt(ab)
        
        // S*() = speed * T + fraction
        float dynamicGap = (vhcl->getSpeed() * vhcl->getSafeTimeHeadway()) + (top / bottom);
        
        // S*(): min gap + dynamic gap (everything but S_0)
        float desiredGap = vhcl->getMinGap() + std::max(0.0f, dynamicGap);
        
        // final right part of big equation  S*()/S_a) ^2
        interactionTerm = pow((desiredGap / currGap), 2);
    }


    return vhcl->getMaxAccel() *(1.0f -freeRoadRatio -interactionTerm);
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

