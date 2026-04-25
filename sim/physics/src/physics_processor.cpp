#include "physics_processor.h"
#include "vehicle_state.h"
#include "node.h"
#include "road.h"
#include <vector>
#include <math.h>
#include <iostream>
#include <algorithm>

PhysicsProcessor::PhysicsProcessor(Network* mapNetwork) : network(mapNetwork), vehicleList(), vehicleUpdates() {}

float PhysicsProcessor::getRouteSegmentLength(VehicleState* vhcl, int routeIndex) {
    // Safety bounds check
    if (routeIndex < 0 || routeIndex >= vhcl->currentRoute.size() - 1) {
        return 0.0f; 
    }

    int startNodeId = vhcl->currentRoute[routeIndex];
    int endNodeId = vhcl->currentRoute[routeIndex + 1];

    Node* startNode = network->getNode(startNodeId);
    if (!startNode) return 0.0f;

    // Find the edge connecting these two nodes and return its length
    for (Road& edge : startNode->outgoingEdges) {
        if (edge.getDest() == endNodeId) {
            return edge.getLength();
        }
    }
    return 0.0f;
}

float PhysicsProcessor::calculateTrueGap(VehicleState* follower, VehicleState* leader) {
    // If they are on the exact same road segment, it's just standard 1D math
    if (follower->currentRouteIndex == leader->currentRouteIndex) {
        float simpleGap = leader->getPos() - follower->getPos() - leader->getLength();
        return std::max(0.0f, simpleGap);
    }

    // Otherwise, we calculate the multi-segment gap
    float totalGap = 0.0f;

    // 1. The Tail: Distance from the follower to the end of its current road
    float followerRoadLen = getRouteSegmentLength(follower, follower->currentRouteIndex);
    totalGap += (followerRoadLen - follower->getPos());

    // 2. The Middle: Sum of all intermediate roads
    for (int i = follower->currentRouteIndex + 1; i < leader->currentRouteIndex; i++) {
        totalGap += getRouteSegmentLength(follower, i);
    }

    // 3. The Head: Distance the leader has traveled on its road
    totalGap += leader->getPos();

    // Subtract the physical length of the leader car (bumper-to-bumper gap)
    totalGap -= leader->getLength();

    return std::max(0.0f, totalGap); // Ensure gap never goes negative due to floating point drift
}

void PhysicsProcessor::update(float dt)
{
    vehicleUpdates.clear();
    
    // ==========================================
    // PASS 1: CALCULATE INTENDED PHYSICS
    // ==========================================
    for (VehicleState* vhcl : vehicleList)
    {
        // ---> THE PARKING BRAKE <---
        // If the car has reached its destination and is barely moving, force a hard stop.
        if (vhcl->getDesiredSpeed() == 0.0f && vhcl->getSpeed() < 0.5f) {
            vehicleUpdates.push_back(-vhcl->getSpeed()); // Bleed off the exact remaining speed
            continue; 
        }

        float acceleration = IDM(vhcl);
        float dv = acceleration * dt; 

        // no negative speed 
        if (vhcl->getSpeed() + dv < 0.0f) {
            dv = -vhcl->getSpeed();
        }
        vehicleUpdates.push_back(dv);
    }

    // ==========================================
    // PASS 2: APPLY MOVEMENT & ROUTING
    // ==========================================
    int i = 0;
    for (VehicleState* vhcl : vehicleList)
    {
        // ---> CONTINUOUS DESTINATION CHECK <---
        if (vhcl->currentRouteIndex >= vhcl->currentRoute.size() - 1 && vhcl->getSpeed() < 0.1f) 
        {
            // Flag for removal from the active physics loop
            vehiclesToRemove.push_back(vhcl);

            // Untether followers (Give trailing cars a free road!)
            for (VehicleState* otherCar : vehicleList) 
            {
                if (otherCar->getLeader() == vhcl) {
                    otherCar->setLeader(nullptr);
                }
            }
            i++; 
            continue; // Skip the rest of the loop for this parked car
        }

        // Apply physics
        vhcl->accelerate(vehicleUpdates[i]);
        vhcl->move(vhcl->getSpeed() * dt);

        // Routing Edge Transitions
        bool routeAdvanced = true;
        while (routeAdvanced && !vhcl->currentRoute.empty() && vhcl->currentRouteIndex < vhcl->currentRoute.size() - 1) 
        {
            routeAdvanced = false;

            int currentNodeId = vhcl->currentRoute[vhcl->currentRouteIndex];
            int nextNodeId = vhcl->currentRoute[vhcl->currentRouteIndex + 1];

            Node* currentNode = network->getNode(currentNodeId);
            
            // Look up the length of the road we are currently driving on
            double currentRoadLength = 0.0;
            for (Road& edge : currentNode->outgoingEdges) {
                if (edge.getDest() == nextNodeId) {
                    currentRoadLength = edge.getLength();
                    break;
                }
            }

            // Check if we reached the end of the current road
            if (vhcl->getPos() >= currentRoadLength) 
            {
                // Carry over momentum to the beginning of the next road
                vhcl->setPos(vhcl->getPos() - currentRoadLength); 
                vhcl->currentRouteIndex++;
                routeAdvanced = true;

                // Check if we have arrived at the final destination
                if (vhcl->currentRouteIndex >= vhcl->currentRoute.size() - 1) 
                {
                    vhcl->setDesiredSpeed(0.0f); // Apply brakes
                    std::cout << "A vehicle has reached its destination!\n";
                } 
                else 
                {
                    // We are on a new road! Update the desired speed to the new speed limit
                    int newCurrentNodeId = vhcl->currentRoute[vhcl->currentRouteIndex];
                    int newNextNodeId = vhcl->currentRoute[vhcl->currentRouteIndex + 1];
                    Node* newCurrentNode = network->getNode(newCurrentNodeId);
                    
                    for (Road& edge : newCurrentNode->outgoingEdges) {
                        if (edge.getDest() == newNextNodeId) {
                            vhcl->setDesiredSpeed(edge.getSpeedLimit());
                            break;
                        }
                    }
                }
            }
        }
        
        i++;
    }

    // ==========================================
    // GARBAGE COLLECTION PHASE
    // ==========================================
    for (VehicleState* deadVhcl : vehiclesToRemove) 
    {
        // Safely remove the specific pointer from the main vector
        vehicleList.erase(
            std::remove(vehicleList.begin(), vehicleList.end(), deadVhcl), 
            vehicleList.end()
        );
        
        // Notice we do NOT call 'delete deadVhcl;' here.
        // This ensures the main.cpp loop can safely read and print 
        // the final parked coordinates without a segmentation fault.
    }
    
    // Clear the queue for the next frame
    vehiclesToRemove.clear();
}

float PhysicsProcessor::IDM(VehicleState* vhcl)
{
    // Fix: Prevent division by zero if desired speed is set to 0
    float safeDesiredSpeed = std::max(vhcl->getDesiredSpeed(), 0.001f);

    // free road (no cars ahead)
    float freeRoadRatio = pow((vhcl->getSpeed() / safeDesiredSpeed), vhcl->getAccelExp());

    // add logic for "interatction term"
    float interactionTerm = 0.0f;
    VehicleState* leader = vhcl->getLeader();
    if(leader != nullptr) {
        float currGap = calculateTrueGap(vhcl, leader); 

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