#include "physics_processor.h"
#include "vehicle_state.h"
#include <vector>
#include <math.h>
#include <limits> 

PhysicsProcessor::PhysicsProcessor() : vehicleList(), vehicleUpdates() {}

void PhysicsProcessor::update(float dt)
{
    // check for MOBIL
    for (VehicleState* vhcl : vehicleList)
        {
            int currentLane = vhcl->getLane();

            int totalLanes = vhcl->getCurrentEdge()->getLanes(); 
            int bestLane = currentLane;

            // use to track best lane MOBIL incentive
            float threshold = 0.1f; 
            float bestIncentive = threshold;

            //check left
            if (currentLane > 0) {
                float leftIncentive = MOBIL(vhcl, currentLane - 1);
                if (leftIncentive > bestIncentive) {
                   bestLane = currentLane - 1;
                   bestIncentive = leftIncentive;
               }
           }
        
           //check right
           if (currentLane < totalLanes - 1) {
                float rightIncentive = MOBIL(vhcl, currentLane + 1);
                if (rightIncentive > bestIncentive) {
                   bestLane = currentLane + 1;
                   bestIncentive = rightIncentive;
               }
           }
            // take lane with best MOBIL incentive
           if (bestLane != currentLane) {
               vhcl->setLane(bestLane);
           }
    
        }
    vehicleUpdates.clear();
    
    // Calculate physics updates for each vehicle in our master list
    // and store them in vehicleUpdates (so previous speed/pos changes
    // don't affect subsequent changes)
    for (VehicleState* vhcl : vehicleList)
    {
        // recheck leader in case of MOBIL
        vhcl->setLeader(getLeader(vhcl, vhcl->getLane()));
        float acceleration = IDM(vhcl, vhcl->getLeader());
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

float PhysicsProcessor::IDM(VehicleState* vhcl, VehicleState* leader )
{
    // free road (no cars ahead)
    float freeRoadRatio = pow((vhcl->getSpeed() / vhcl->getDesiredSpeed()), vhcl->getAccelExp());

    // add logic for "interatction term"
    float interactionTerm = 0.0f;

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

float  PhysicsProcessor::MOBIL(VehicleState* vhcl, int targetLane)
{
    // need to implement spatial logic for finding leaders and followers
    VehicleState* newLeader = getLeader(vhcl, targetLane);
    VehicleState* newFollower = getFollower(vhcl, targetLane);
    VehicleState* oldFollower = getFollower(vhcl, vhcl->getLane());
    VehicleState* curLeader = vhcl->getLeader();

    float politeness = 0.2f; // 0 is selfish, 1 is selfless
    float safeBrake = 2.0f; // b_safe, max deceleration vehicle can cause on new follower

    // saftey criterion, check if lane change is safe to do 
    
    float newFollowerAccel = 0.0f;
    if (newFollower != nullptr) {
        newFollowerAccel = IDM(newFollower, vhcl);
        if (newFollowerAccel < -safeBrake) { //note accel is negative for braking
            return -999.0f; // not safe to change
        }
    }

    // incentive criterion, acceralation gained
    float curAccel = IDM(vhcl, curLeader);
    float newAccel = IDM(vhcl, newLeader);
    float newAccelGain = newAccel - curAccel;

    // effect on new follower
    float newFollowerGain = 0.0f;
    if (newFollower != nullptr) {
        float newFAccelBefore = IDM(newFollower, newLeader); 
        newFollowerGain = newFollowerAccel - newFAccelBefore;
    }

    // effect on new follower 
    float oldFollowerGain = 0.0f;
    if (oldFollower != nullptr) {
        float oldFollowerAccel = IDM(oldFollower, vhcl);
        float oldFAccelAfter = IDM(oldFollower, curLeader);
        oldFollowerGain = oldFAccelAfter - oldFollowerAccel;
    }

    float incentive= newAccelGain +politeness*(newFollowerGain + oldFollowerGain);
    return incentive;
    
}

VehicleState* PhysicsProcessor::getLeader(VehicleState* vhcl, int targetLane)
{
    VehicleState* closestLeader = nullptr;
    float minDistance = std::numeric_limits<float>::max();

    for (VehicleState* other : vehicleList)
    {
        if (other == vhcl) continue;

        // lane check
        if (other->getLane() == targetLane)
        {
            //check ahead
            if (other->getPos() > vhcl->getPos())
            {
                float distance = other->getPos() - vhcl->getPos();
                
                // save closest
                if (distance < minDistance)
                {
                    minDistance = distance;
                    closestLeader = other;
                }
            }
        }
    }
    return closestLeader;
}

VehicleState* PhysicsProcessor::getFollower(VehicleState* vhcl, int targetLane)
{
    VehicleState* closestFollower = nullptr;
    float minDistance = std::numeric_limits<float>::max();

    for (VehicleState* other : vehicleList)
    {
        if (other == vhcl) continue;

        //lane check
        if (other->getLane() == targetLane)
        {
            // check behind
            if (other->getPos() < vhcl->getPos())
            {
                float distance = vhcl->getPos() - other->getPos();
                
                // save closest
                if (distance < minDistance)
                {
                    minDistance = distance;
                    closestFollower = other;
                }
            }
        }
    }
    return closestFollower;
}

PhysicsProcessor::~PhysicsProcessor() 
{
    for (VehicleState* v : vehicleList)
    {
        delete(v);
    }
}

