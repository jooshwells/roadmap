#include "physics_processor.h"
#include "spatial_hash.h"
#include "vehicle_state.h"
#include "node.h"
#include "road.h"
#include <vector>
#include <math.h>
#include <limits> 
#include <iostream>
#include <algorithm>

PhysicsProcessor::PhysicsProcessor(Network* mapNetwork, VehicleSpatialHash* spatialObj) : network(mapNetwork), spatialHash(spatialObj), vehicleList(), vehicleUpdates() {}

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
    if (follower->currentRouteIndex > leader->currentRouteIndex || 
       (follower->currentRouteIndex == leader->currentRouteIndex && follower->getPos() >= leader->getPos())) {
        return 0.0f; // No gap to calculate, the follower is in front!
    }
    
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

float PhysicsProcessor::calculateDistanceToDestination(VehicleState* vhcl) 
{
    float totalDist = 0.0f;
    
    // 1. Remaining distance on the current road
    float currentRoadLen = getRouteSegmentLength(vhcl, vhcl->currentRouteIndex);
    totalDist += (currentRoadLen - vhcl->getPos());

    // 2. Sum of all remaining roads in the route
    for (size_t i = vhcl->currentRouteIndex + 1; i < vhcl->currentRoute.size() - 1; i++) {
        totalDist += getRouteSegmentLength(vhcl, i);
    }
    
    return std::max(0.0f, totalDist);
}

void PhysicsProcessor::update(float dt)
{
    spatialHash->rebuild(vehicleList);
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
    
    // ==========================================
    // PASS 1: CALCULATE INTENDED PHYSICS
    // ==========================================
    for (VehicleState* vhcl : vehicleList)
    {
        // ---> THE PARKING BRAKE <---
        // If the car has reached its destination and is barely moving, force a hard stop.
        if (vhcl->getDesiredSpeed() == 0.0f && vhcl->getSpeed() < 0.5f) {
            vehicleUpdates.push_back(-vhcl->getSpeed()); // Bleed off the exact remaining speed

            // Log 0 acceleration and track wait time while parked/stopped
            vhcl->setAcceleration(0.0f);
            vhcl->updateWaitTime(dt);
            continue; 
        }

        // recheck leader in case of MOBIL
        vhcl->setLeader(getLeader(vhcl, vhcl->getLane()));
        float acceleration = IDM(vhcl, vhcl->getLeader(), false);
        float dv = acceleration *dt;

        // ---> NEW: TELEMETRY TRACKING <---
        vhcl->setAcceleration(acceleration); // Store exact IDM output
        vhcl->updateWaitTime(dt);            // Accumulate delay if below threshold

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
        // IDM brings the car to a halt perfectly on the line, so it never crosses it. 
        // Check if remaining distance is within a tiny tolerance (e.g., 0.5 meters) and speed is near zero.
        if (calculateDistanceToDestination(vhcl) < 0.5f && vhcl->getSpeed() < 0.1f) 
        {
            // std::cout << "A vehicle has reached its destination!\n";
            
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
                    // std::cout << "A vehicle has reached its destination!\n";
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
                            vhcl->setCurrentEdge(&edge);

                            if (vhcl->getLane() >= edge.getLanes()) {
                                vhcl->setLane(edge.getLanes() - 1); 
                            }

                            break;
                        }
                    }
                }
            }
        }
        
        i++;
    }

    // ==========================================
    // DEAD CAR COLLECTION PHASE
    // ==========================================
    for (VehicleState* deadVhcl : vehiclesToDestroy)
    {
        delete deadVhcl;
    }
    vehiclesToDestroy.clear();

    // ==========================================
    // PARKED CAR COLLECTION PHASE
    // ==========================================
    for (VehicleState* parkedVehicle : vehiclesToRemove) 
    {
        // Safely remove the specific pointer from the main vector
        vehicleList.erase(
            std::remove(vehicleList.begin(), vehicleList.end(), parkedVehicle), 
            vehicleList.end()
        );

        vehiclesToDestroy.push_back(parkedVehicle); // Destroy it next frame
        
        // Notice we do NOT call 'delete deadVhcl;' here.
        // This ensures the main.cpp loop can safely read and print 
        // the final parked coordinates without a segmentation fault.
    }
    
    // Clear the queue for the next frame
    vehiclesToRemove.clear();
}

float PhysicsProcessor::IDM(VehicleState* vhcl, VehicleState* leader, bool mobil )
{
    float safeDesiredSpeed = std::max(vhcl->getDesiredSpeed(), 0.001f);
    float freeRoadRatio = pow((vhcl->getSpeed() / safeDesiredSpeed), vhcl->getAccelExp());

    float interactionTerm = 0.0f;
    
    // Default to an infinitely far road
    float effectiveGap = 99999.0f; 
    float approachSpeed = 0.0f; // deltaV

    // --- 1. Check Physical Leader ---
    if (leader != nullptr) {
        effectiveGap = calculateTrueGap(vhcl, leader); 
        approachSpeed = vhcl->getSpeed() - leader->getSpeed();
    }

    // --- 2. Check Virtual Leader (The Destination) ---
    float distToDest = calculateDistanceToDestination(vhcl);
    
    // We add the car's minGap to the destination distance. 
    // Otherwise, the IDM will try to stop 'minGap' meters BEFORE the destination.
    // Adding it back offsets the IDM's bumper-to-bumper safety buffer.
    float destGap = distToDest + vhcl->getMinGap();

    // --- 3. React to the most restrictive condition ---
    // If the destination is closer than the physical car ahead, brake for the destination!
    if (destGap < effectiveGap) {
        effectiveGap = destGap;
        approachSpeed = vhcl->getSpeed() - 0.0f; // The destination is a brick wall (0 speed)
    }

    // --- 4. Standard IDM Calculation ---
    // Only calculate the interaction term if there is actually a reason to brake
    if (effectiveGap < 9999.0f) 
    {
        if (effectiveGap <= 0.0001f) effectiveGap = 0.001f; // prevent division by 0

        float top = vhcl->getSpeed() * approachSpeed; 
        float bottom = 2.0f * sqrt(vhcl->getMaxAccel() * vhcl->getSafeBrakePower()); 
        
        float dynamicGap = (vhcl->getSpeed() * vhcl->getSafeTimeHeadway()) + (top / bottom);
        float desiredGap = vhcl->getMinGap() + std::max(0.0f, dynamicGap);
        
        interactionTerm = pow((desiredGap / effectiveGap), 2);

    }

    float finalAccel = vhcl->getMaxAccel() * (1.0f - freeRoadRatio - interactionTerm);

    // Apply a realistic physical limit for a hard emergency stop.
    // Tires lose grip around -9.8 m/s^2. Clamping it here prevents math explosions
    // while still simulating heavy emergency braking telemetry.
    float maxPhysicalDeceleration = -10.0f; 

    return std::max(maxPhysicalDeceleration, finalAccel);
}

void PhysicsProcessor::addVehicle(VehicleState* vhcl)
{
    vehicleList.push_back(vhcl);

    // Initialize the vehicle's current road edge upon entering the simulation
    if (vhcl->getCurrentEdge() == nullptr && vhcl->currentRoute.size() > 1) 
    {
        int currentNodeId = vhcl->currentRoute[vhcl->currentRouteIndex];
        int nextNodeId = vhcl->currentRoute[vhcl->currentRouteIndex + 1];

        Node* currentNode = network->getNode(currentNodeId);
        if (currentNode) {
            for (Road& edge : currentNode->outgoingEdges) {
                if (edge.getDest() == nextNodeId) {
                    vhcl->setCurrentEdge(&edge); // Set the initial edge
                    break;
                }
            }
        }
    }
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
        newFollowerAccel = IDM(newFollower, vhcl, true);
        if (newFollowerAccel < -safeBrake) { //note accel is negative for braking
            return -999.0f; // not safe to change
        }
    }

    // incentive criterion, acceralation gained
    float curAccel = IDM(vhcl, curLeader, true);
    float newAccel = IDM(vhcl, newLeader, true);
    float newAccelGain = newAccel - curAccel;

    // effect on new follower
    float newFollowerGain = 0.0f;
    if (newFollower != nullptr) {
        float newFAccelBefore = IDM(newFollower, newLeader, true); 
        newFollowerGain = newFollowerAccel - newFAccelBefore;
    }

    // effect on new follower 
    float oldFollowerGain = 0.0f;
    if (oldFollower != nullptr) {
        float oldFollowerAccel = IDM(oldFollower, vhcl, true);
        float oldFAccelAfter = IDM(oldFollower, curLeader, true);
        oldFollowerGain = oldFAccelAfter - oldFollowerAccel;
    }

    float incentive= newAccelGain +politeness*(newFollowerGain + oldFollowerGain);
    return incentive;
    
}

VehicleState* PhysicsProcessor::getLeader(VehicleState* vhcl, int targetLane)
{
    return spatialHash->getLeader(vhcl, targetLane, network);
}

VehicleState* PhysicsProcessor::getFollower(VehicleState* vhcl, int targetLane)
{
    return spatialHash->getFollower(vhcl, targetLane, network);
}

PhysicsProcessor::~PhysicsProcessor() 
{
    for (VehicleState* v : vehicleList)
    {
        delete(v);
    }
    for (VehicleState* v : vehiclesToDestroy)
    {
        delete(v);
    }
}