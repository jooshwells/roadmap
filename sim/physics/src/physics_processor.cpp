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
    // 1. If they are on the exact same physical road edge
    if (follower->getCurrentEdge() == leader->getCurrentEdge()) {
        if (follower->getPos() >= leader->getPos()) return 0.0f; // Follower is in front
        return std::max(0.0f, leader->getPos() - follower->getPos() - leader->getLength());
    }

    // 2. Otherwise, calculate the multi-segment gap
    float totalGap = 0.0f;
    float followerRoadLen = getRouteSegmentLength(follower, follower->currentRouteIndex);
    totalGap += (followerRoadLen - follower->getPos());

    bool leaderFound = false;

    // Trace forward strictly along the follower's route
    for (size_t i = follower->currentRouteIndex + 1; i < follower->currentRoute.size() - 1; i++) {
        int stepStartNode = follower->currentRoute[i];
        int stepEndNode = follower->currentRoute[i+1];
        
        // Check if this route step matches the leader's current physical edge
        int leaderStartNode = leader->currentRoute[leader->currentRouteIndex];
        int leaderEndNode = leader->currentRoute[leader->currentRouteIndex + 1];

        if (stepStartNode == leaderStartNode && stepEndNode == leaderEndNode) {
            leaderFound = true;
            break;
        }
        totalGap += getRouteSegmentLength(follower, i);
    }

    if (!leaderFound) {
        // Leader is not physically on the follower's remaining route 
        // (Could happen if leader is turning off the route or spatial hash is 1 frame stale)
        return 9999.0f; 
    }

    totalGap += leader->getPos();
    totalGap -= leader->getLength();

    return std::max(0.0f, totalGap);
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
    
    // ==========================================
    // PASS 0: MOBIL & LANE CHANGING
    // ==========================================
    for (VehicleState* vhcl : vehicleList)
    {
        // ---> CRITICAL FIX 1: Guard against missing edges <---
        // If a car spawned with a bad route and has no edge, trying to get lanes will segfault!
        if (vhcl->getCurrentEdge() == nullptr) continue; 

        int currentLane = vhcl->getLane();
        int totalLanes = vhcl->getCurrentEdge()->getLanes(); 
        int bestLane = currentLane;

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
        if (vhcl->getDesiredSpeed() == 0.0f && vhcl->getSpeed() < 0.5f) {
            vehicleUpdates.push_back(-vhcl->getSpeed()); 
            vhcl->setAcceleration(0.0f);
            vhcl->updateWaitTime(dt);
            continue; 
        }

        vhcl->setLeader(getLeader(vhcl, vhcl->getLane()));
        float acceleration = IDM(vhcl, vhcl->getLeader(), false);
        float dv = acceleration * dt;

        vhcl->setAcceleration(acceleration); 
        vhcl->updateWaitTime(dt); 

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
        if (calculateDistanceToDestination(vhcl) < 12.0f) 
        {
            vehiclesToRemove.push_back(vhcl);

            for (VehicleState* otherCar : vehicleList) 
            {
                if (otherCar->getLeader() == vhcl) {
                    otherCar->setLeader(nullptr);
                }
            }
            i++; 
            continue; 
        }

        vhcl->accelerate(vehicleUpdates[i]);
        vhcl->move(vhcl->getSpeed() * dt);

        bool routeAdvanced = true;
        while (routeAdvanced && !vhcl->currentRoute.empty() && vhcl->currentRouteIndex < vhcl->currentRoute.size() - 1) 
        {
            routeAdvanced = false;

            int currentNodeId = vhcl->currentRoute[vhcl->currentRouteIndex];
            int nextNodeId = vhcl->currentRoute[vhcl->currentRouteIndex + 1];

            Node* currentNode = network->getNode(currentNodeId);
            
            // ---> CRITICAL FIX 2: Null Check Node Lookups <---
            if (!currentNode) break; 
            
            double currentRoadLength = 0.0;
            for (Road& edge : currentNode->outgoingEdges) {
                if (edge.getDest() == nextNodeId) {
                    currentRoadLength = edge.getLength();
                    break;
                }
            }

            // ---> CRITICAL FIX 3: Prevent Infinite Loops <---
            // If the edge was missing, currentRoadLength is 0.0.
            // getPos() >= 0.0 is always true, causing an infinite while loop!
            if (currentRoadLength <= 0.001f) break;

            if (vhcl->getPos() >= currentRoadLength) 
            {
                vhcl->setPos(vhcl->getPos() - currentRoadLength); 
                vhcl->currentRouteIndex++;
                routeAdvanced = true;

                if (vhcl->currentRouteIndex >= vhcl->currentRoute.size() - 1) 
                {
                    vhcl->setDesiredSpeed(0.0f);
                } 
                else 
                {
                    int newCurrentNodeId = vhcl->currentRoute[vhcl->currentRouteIndex];
                    int newNextNodeId = vhcl->currentRoute[vhcl->currentRouteIndex + 1];
                    Node* newCurrentNode = network->getNode(newCurrentNodeId);
                    
                    // ---> CRITICAL FIX 4: Null check the next node <---
                    if (newCurrentNode) 
                    {
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
        vehicleList.erase(
            std::remove(vehicleList.begin(), vehicleList.end(), parkedVehicle), 
            vehicleList.end()
        );

        vehiclesToDestroy.push_back(parkedVehicle); 
    }
    
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