#define _USE_MATH_DEFINES
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
    // Safety bounds check (Fixed to prevent unsigned underflow)
    if (routeIndex < 0 || routeIndex + 1 >= vhcl->currentRoute.size()) {
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
    // hide cars marked for deletion
    std::vector<VehicleState*> livingVehicles;
    for(VehicleState* v : vehicleList) {
        if(!v->isMarkedForDeletion) {
            livingVehicles.push_back(v);
        }
    }

    // Rebuild hash ONLY with living vehicles to prevent dead-pointer reads
    spatialHash->rebuild(livingVehicles);
    
    updateIntersections(dt);

    // ==========================================
    // PASS 0: MOBIL & LANE CHANGING
    // ==========================================
    for (VehicleState* vhcl : vehicleList)
    {
        Road* currentEdge = vhcl->getCurrentEdge();
        
        // ---> CRITICAL FIX 1: Guard against missing edges <---
        if (currentEdge == nullptr) continue; 

        int currentLane = vhcl->getLane();
        int totalLanes = currentEdge->getLanes(); 

        // if within 150 meters of intersection, check if car needs to turn/move lanes
        float distanceToIntersection = currentEdge->getLength() - vhcl->getPos();
        std::string upcomingTurn = "through";
        if (distanceToIntersection < 150.0f) {
            upcomingTurn = getUpcomingTurnDirection(vhcl);
        }

        int bestLane = currentLane;
        float threshold = 0.1f; 
        float bestIncentive = threshold;

        //check left
        if (currentLane > 0) {
            float leftIncentive = MOBIL(vhcl, currentLane - 1);

            // bias lane changing if needed and wont cause crash, might need to adjust -500 crash bias
            if (leftIncentive > -500.0f) {
                if (upcomingTurn == "left") leftIncentive += 100.0f;  // need to turn left
                if (upcomingTurn == "right") leftIncentive -= 100.0f; // right turn, dont go left
            }
            if (leftIncentive > bestIncentive) {
               bestLane = currentLane - 1;
               bestIncentive = leftIncentive;
           }
       }
    
       //check right
       if (currentLane < totalLanes - 1) {
            float rightIncentive = MOBIL(vhcl, currentLane + 1);

            // bias lane changing if needed and wont cause crash, might need to adjust -500 crash bias
            if (rightIncentive > -500.0f) {
                if (upcomingTurn == "left") rightIncentive -= 100.0f;  // left turn, dont go right
                if (upcomingTurn == "right") rightIncentive += 100.0f; // need to turn right
            }

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
        // ---> ARRAY DESYNC FIX <---
        if (vhcl == nullptr) {
            vehicleUpdates.push_back(0.0f);
            continue;
        }

        // ---> THE PARKING BRAKE <---
        // If the car has reached its destination and is barely moving, force a hard stop.
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
        if (vhcl == nullptr) {
            i++; 
            continue;
        }

        // ---> CONTINUOUS DESTINATION CHECK <---
        // IDM brings the car to a halt perfectly on the line, so it never crosses it. 
        // Check if remaining distance is within a tiny tolerance (e.g., 0.5 meters) and speed is near zero.
        if (calculateDistanceToDestination(vhcl) < 0.5f && vhcl->getSpeed() < 0.1f) 
        {
            // Flag for removal from the active physics loop
            vhcl->isMarkedForDeletion = true;
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

        // Apply physics
        if (!vhcl->isMarkedForDeletion) {
            vhcl->accelerate(vehicleUpdates[i]);
            vhcl->move(vhcl->getSpeed() * dt);
        }

        bool routeAdvanced = true;
        while (routeAdvanced && !vhcl->currentRoute.empty() && vhcl->currentRouteIndex < vhcl->currentRoute.size() - 1) 
        {
            routeAdvanced = false;

            int currentNodeId = vhcl->currentRoute[vhcl->currentRouteIndex];
            int nextNodeId = vhcl->currentRoute[vhcl->currentRouteIndex + 1];

            Node* currentNode = network->getNode(currentNodeId);
            
            // ---> CRITICAL FIX 2: Null Check Node Lookups <---
            if (!currentNode) break; 
            
            // Look up the length of the road we are currently driving on
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
                // Prevent Zero-Length Infinite Loop
                if (currentRoadLength <= 0.01f) {
                    vhcl->setPos(0.0f);
                } else {
                    vhcl->setPos(vhcl->getPos() - currentRoadLength); 
                }

                vhcl->currentRouteIndex++;
                routeAdvanced = true;

                if (vhcl->currentRouteIndex >= vhcl->currentRoute.size() - 1) 
                {
                    vhcl->setDesiredSpeed(0.0f); // Apply brakes
                    break; // EXIT THE WHILE LOOP IMMEDIATELY!
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
                                
                                // NEW: Volume swapping
                                Road* oldEdge = vhcl->getCurrentEdge();
                                if (oldEdge) oldEdge->removeVehicle();
                                edge.addVehicle(); 
                                
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
        if (network != nullptr) {
            for (auto& pair : intersections) {
                if (pair.second.currentOccupant == deadVhcl) {
                    pair.second.currentOccupant = nullptr;
                }
                
                // clear dead car from wait queues
                std::queue<VehicleState*> safeQueue;
                while (!pair.second.waitQueue.empty()) {
                    VehicleState* waitingCar = pair.second.waitQueue.front();
                    pair.second.waitQueue.pop();
                    
                    // Only keep cars that aren't about to be deleted
                    if (waitingCar != deadVhcl) {
                        safeQueue.push(waitingCar);
                    }
                }
                pair.second.waitQueue = safeQueue;
            }
        }
        delete deadVhcl;
    }
    vehiclesToDestroy.clear();

    // ==========================================
    // PARKED CAR COLLECTION PHASE
    // ==========================================
    for (VehicleState* parkedVehicle : vehiclesToRemove) 
    {
        // Unregister the vehicle from the road before deleting it
        if (parkedVehicle->getCurrentEdge()) {
            parkedVehicle->getCurrentEdge()->removeVehicle();
        }

        vehicleList.erase(
            std::remove(vehicleList.begin(), vehicleList.end(), parkedVehicle), 
            vehicleList.end()
        );

        vehiclesToDestroy.push_back(parkedVehicle); // Destroy it next frame
    }
    
    vehiclesToRemove.clear();
}

float PhysicsProcessor::IDM(VehicleState* vhcl, VehicleState* leader, bool mobil )
{
    if (vhcl == nullptr || vhcl->isMarkedForDeletion) return 0.0f;

    if (vhcl->getCurrentEdge() == nullptr) {
        return 0.0f; 
    }
    if (leader != nullptr && leader->isMarkedForDeletion) {
        leader = nullptr; // Pretend the road is clear
    }
    
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
                    edge.addVehicle();
                    break;
                }
            }
        }
        // debugging
        if (vhcl->getCurrentEdge() == nullptr) {
            std::cerr << "[WARNING] Vehicle " << vhcl->getId() 
                      << " failed to bind to Edge! Route: " 
                      << currentNodeId << " -> " << nextNodeId << ". Aborting spawn." << std::endl;
            
            // Delete the car and remove it from the list immediately
            vehicleList.pop_back(); 
            delete vhcl;
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
    // 1. Get the physical car ahead using the optimized spatial hash (from your teammate)
    VehicleState* physicalLeader = spatialHash->getLeader(vhcl, targetLane, network);
    
    // another safety check 
    if (!VehicleState::isSafe(physicalLeader)) {
        physicalLeader = nullptr;
    }
    if (physicalLeader != nullptr) {
        if(physicalLeader->isMarkedForDeletion) {
            return  nullptr;
        }
    }
    // Calculate distance to the physical leader
    float physicalDistance = std::numeric_limits<float>::max();
    if (physicalLeader != nullptr) {
        physicalDistance = calculateTrueGap(vhcl, physicalLeader);
    }

    // 2. Check intersection and place virtual/ghost vehicle if needed (from your branch)
    if (network != nullptr) {
        Road* currentRoad = vhcl->getCurrentEdge();
        
        if (currentRoad != nullptr) {
            uint64_t destNodeId = currentRoad->getDest();
            Node* destNode = network->getNode(destNodeId);

            // If the vehicle cannot enter the intersection...
            if (destNode != nullptr && !canVehicleEnter(vhcl, destNode)) {
    
                float distanceToStopLine = currentRoad->getLength() - vhcl->getPos();
                
                // If the stop line is closer than the physical leader, yield to the stop line
                if (distanceToStopLine > 0.0f && distanceToStopLine < physicalDistance) {
                    
                    std::pair<Road*, int> laneKey = std::make_pair(currentRoad, vhcl->getLane());
                    
                    if (ghostVehicles.find(laneKey) == ghostVehicles.end()) {
                        IDMParameters dummyParams = {1.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f};
                        ghostVehicles[laneKey] = new VehicleState(0, 0, 0.0f, (float)currentRoad->getLength(), vhcl->getLane(), dummyParams);
                    }
                
                    VehicleState* ghost = ghostVehicles[laneKey];
                    ghost->currentRouteIndex = vhcl->currentRouteIndex;
                    
                    return ghost;
                }
            }
        }
    }

    // 3. Otherwise, return the physical leader (or nullptr if the road is completely clear)
    return physicalLeader;
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
    
    // --- Memory Cleanup from main ---
    for (VehicleState* v : vehiclesToDestroy)
    {
        delete(v);
    }

    // --- Delete ghost vehicles from intersection branch ---
    for (auto& pair : ghostVehicles) 
    {
        delete pair.second;
    }
}

// --- Intersection Logic ---

// gets called in update(), maybe need to look at performance 
void PhysicsProcessor::updateIntersections(float dt)
{
     if (network == nullptr) return;

    for (auto& pair : intersections) {
        uint64_t nodeId = pair.first; 
        IntersectionState& state = pair.second;
        Node* node = network->getNode(nodeId);

        if (node == nullptr) continue;

    // 4 way stop logic
    if (node->type == Node::FOUR_WAY_STOP) {

        // safety check
        if (state.currentOccupant != nullptr && !state.currentOccupant->isAlive()) {
            state.currentOccupant = nullptr; 
        }
        // check if there is a car already in the intersection
        if (state.currentOccupant != nullptr) {
            
            // deletion check
            if (state.currentOccupant->isMarkedForDeletion) {
                state.currentOccupant = nullptr; 
            } 
            // check if car cleared intersection
            else {
                Road* currentEdge = state.currentOccupant->getCurrentEdge();
                if (currentEdge) {
                    if (currentEdge->getDest() != nodeId || state.currentOccupant->getPos() > currentEdge->getLength() + 5.0f) {
                        if (state.currentOccupant->getPos() > 5.0f) {
                            state.currentOccupant = nullptr; 
                        }
                    }
                }
            }
        }

        // check queue for deleted cars
        while (!state.waitQueue.empty() && state.waitQueue.front()->isMarkedForDeletion) {
            state.waitQueue.pop(); 
        }
        // pop queue
        if (state.currentOccupant == nullptr && !state.waitQueue.empty()) {
            state.currentOccupant = state.waitQueue.front();
            state.waitQueue.pop();
        }
    }
        
        // traffic light logic, working on multi directional phases
        else if (node->type == Node::TRAFFIC_LIGHT) {
            
            if (!state.isInitialized) {
                state.isInitialized = true;
                
                std::vector<std::pair<Road*, double>> edgeAngles;
                
                for (uint64_t predNodeId : node->incomingEdgeNodeIds) {
                    Node* predNode = network->getNode(predNodeId);
                    if (!predNode) continue;
                    for (Road& edge : predNode->outgoingEdges) {
                        if (edge.getDest() == nodeId) {
                            // Calculate incoming compass angle using atan2
                            double dx = node->getX() - predNode->getX();
                            double dy = node->getY() - predNode->getY();
                            double angle = atan2(dy, dx) * 180.0 / M_PI; 
                            if (angle < 0) angle += 360.0;
                            
                            edgeAngles.push_back({&edge, angle});
                        }
                    }
                }
                
                // Group roads into Axis 0 (Main Street) and Axis 1 (Cross Streets / T-Stems)
                if (!edgeAngles.empty()) {
                    double baselineAngle = edgeAngles[0].second;
                    state.axisEdges[0].push_back(edgeAngles[0].first);
                    
                    for (size_t i = 1; i < edgeAngles.size(); i++) {
                        double diff = std::abs(baselineAngle - edgeAngles[i].second);
                        if (diff > 180.0) diff = 360.0 - diff;
                        
                        if (diff > 135.0) {
                            state.axisEdges[0].push_back(edgeAngles[i].first); // Opposite direction
                        } else {
                            state.axisEdges[1].push_back(edgeAngles[i].first); // Cross street
                        }
                    }
                }
            }

            // new 10 phase traffic lights
            state.lightTimer += dt;
            
            // Phase 0: N/S Protected Left (Green)
            if (state.currentPhase == 0 && state.lightTimer >= 6.0f) {
                state.currentPhase = 1; state.lightTimer = 0.0f;
            } 
            // Phase 1: N/S Protected Left (Yellow)
            else if (state.currentPhase == 1 && state.lightTimer >= 3.0f) {
                state.currentPhase = 2; state.lightTimer = 0.0f;
            }
            // Phase 2: N/S Straight/Right (Green)
            else if (state.currentPhase == 2 && state.lightTimer >= 15.0f) {
                state.currentPhase = 3; state.lightTimer = 0.0f;
            }
            // Phase 3: N/S Straight/Right (Yellow)
            else if (state.currentPhase == 3 && state.lightTimer >= 4.0f) {
                state.currentPhase = 4; state.lightTimer = 0.0f;
            }
            // Phase 4: All Red Clearance
            else if (state.currentPhase == 4 && state.lightTimer >= 2.0f) {
                // SENSOR CHECK: Is anyone waiting to turn left on East/West?
                if (checkLeftTurnDemand(node)) state.currentPhase = 5; 
                else state.currentPhase = 7; // Skip protected left!
                state.lightTimer = 0.0f;
            }
            // Phase 5: E/W Protected Left (Green)
            else if (state.currentPhase == 5 && state.lightTimer >= 6.0f) {
                state.currentPhase = 6; state.lightTimer = 0.0f;
            }
            // Phase 6: E/W Protected Left (Yellow)
            else if (state.currentPhase == 6 && state.lightTimer >= 3.0f) {
                state.currentPhase = 7; state.lightTimer = 0.0f;
            }
            // Phase 7: E/W Straight/Right (Green)
            else if (state.currentPhase == 7 && state.lightTimer >= 15.0f) {
                state.currentPhase = 8; state.lightTimer = 0.0f;
            }
            // Phase 8: E/W Straight/Right (Yellow)
            else if (state.currentPhase == 8 && state.lightTimer >= 4.0f) {
                state.currentPhase = 9; state.lightTimer = 0.0f;
            }
            // Phase 9: All Red Clearance
            else if (state.currentPhase == 9 && state.lightTimer >= 2.0f) {
                // check left turns
                if (checkLeftTurnDemand(node)) state.currentPhase = 0; 
                else state.currentPhase = 2; // skip left phase
                state.lightTimer = 0.0f;
            }
        }
    }
}
bool PhysicsProcessor::canVehicleEnter(VehicleState* vhcl, Node* destNode)
{   
    // destNode represents intersection at end of a road
    if (destNode == nullptr || destNode->type == Node::PASS_THROUGH) return true;
    
    uint64_t nodeId = destNode->getId(); 
    IntersectionState& state = intersections[nodeId];

    // four way stop
    if (destNode->type == Node::FOUR_WAY_STOP) {
        // allow car to move thru intersection
        if (state.currentOccupant == vhcl) return true;

        // setup queue for cars approaching stop
        bool inQueue = false;
        std::queue<VehicleState*> tempQueue = state.waitQueue;
        while (!tempQueue.empty()) {
            if (tempQueue.front() == vhcl) {
                inQueue = true;
                break;
            }
            tempQueue.pop();
        }

        if (!inQueue) {
            state.waitQueue.push(vhcl);
        }
        return false; // car cannot enter
    }

    if (destNode->type == Node::TRAFFIC_LIGHT) {
        
        Road* myRoad = vhcl->getCurrentEdge();
        std::string turn = getUpcomingTurnDirection(vhcl);
        
        // Find which geometric axis my road belongs to
        int myAxis = -1;
        if (std::find(state.axisEdges[0].begin(), state.axisEdges[0].end(), myRoad) != state.axisEdges[0].end()) myAxis = 0;
        else if (std::find(state.axisEdges[1].begin(), state.axisEdges[1].end(), myRoad) != state.axisEdges[1].end()) myAxis = 1;

        // Is my axis currently green?
        bool isNSGreen = (state.currentPhase == 0 || state.currentPhase == 2);
        bool isEWGreen = (state.currentPhase == 5 || state.currentPhase == 7);

        if (myAxis == 0 && isNSGreen) {
            if (state.currentPhase == 0) { // Protected Left Only
                return (turn == "left"); 
            } else if (state.currentPhase == 2) { // Straight/Right Green
                if (turn == "left") return hasSafeGap(vhcl, destNode, 5.0f); // Yield left
                return true;
            }
        } 
        else if (myAxis == 1 && isEWGreen) {
            if (state.currentPhase == 5) { // Protected Left Only
                return (turn == "left");
            } else if (state.currentPhase == 7) { // Straight/Right Green
                if (turn == "left") return hasSafeGap(vhcl, destNode, 5.0f); // Yield left
                return true;
            }
        }

        // RED LIGHT FALLBACK: Check for Right-on-Red
        if (turn == "right" && vhcl->getSpeed() < 1.0f) {
            return hasSafeGap(vhcl, destNode, 4.5f);
        }
        
        return false; // Wait for green
    }
    // yield stops, uses major and minor road classification
    if (destNode->type == Node::YIELD_STOP) {
        
        uint64_t comingFromId = vhcl->getCurrentEdge()->getOriginId(); 
        
        // check origin road to see if on minor road (yielding)
        bool isOnMinorRoad = std::find(destNode->minorRoadOriginIds.begin(), destNode->minorRoadOriginIds.end(), comingFromId) != destNode->minorRoadOriginIds.end();
        
        // major road, continue through intersection
        if (!isOnMinorRoad) return true;

        // minor road, slow down
        if (vhcl->getSpeed() > 1.0f) return false; 

        // yield and get safe gap
        if (hasSafeGap(vhcl, destNode, 4.5f)) {
            state.currentOccupant = vhcl; 
            return true;
        } else {
            return false;
        }
    }
    return true;
}

// looking into using cross product for determining when to turn
std::string PhysicsProcessor::getUpcomingTurnDirection(VehicleState* vhcl) 
{
    // end of route, no turns (Safely preventing unsigned underflow)
    if (vhcl->currentRouteIndex + 2 >= vhcl->currentRoute.size()) return "through";

    uint64_t prevNodeId = vhcl->currentRoute[vhcl->currentRouteIndex];
    uint64_t currNodeId = vhcl->currentRoute[vhcl->currentRouteIndex + 1];
    uint64_t nextNodeId = vhcl->currentRoute[vhcl->currentRouteIndex + 2];

    Node* prev = network->getNode(prevNodeId);
    Node* curr = network->getNode(currNodeId);
    Node* next = network->getNode(nextNodeId);

    if (!prev || !curr || !next) return "through";

    // current road
    double v1x = curr->getX() - prev->getX();
    double v1y = curr->getY() - prev->getY();

    // road after intersection
    double v2x = next->getX() - curr->getX();
    double v2y = next->getY() - curr->getY();

    double crossProduct = (v1x * v2y) - (v1y * v2x);

    // use product to dtermine left or right
    if (crossProduct > 10.0) return "left";
    if (crossProduct < -10.0) return "right";
    
    return "through"; // default to go straight
}

// updated with spatial hash
bool PhysicsProcessor::hasSafeGap(VehicleState* yieldingCar, Node* destNode, float criticalGapSeconds) 
{
    std::string myTurn = getUpcomingTurnDirection(yieldingCar);

    // Only look at roads that physically connect to this intersection
    for (uint64_t predNodeId : destNode->incomingEdgeNodeIds) {
        Node* predNode = network->getNode(predNodeId);
        if (!predNode) continue;

        for (Road& oncomingRoad : predNode->outgoingEdges) {
            
            // Skip the road the yielding car is currently on
            if (oncomingRoad.getEdgeId() == yieldingCar->getEdgeId() || oncomingRoad.getDest() != destNode->getId()) {
                continue;
            }

            // Retrieve only the cars on this specific oncoming road from the spatial hash
            // (Assumes you have a getter in spatialHash or you make edgeBuckets accessible)
            std::vector<VehicleState*> oncomingCars = spatialHash->getVehiclesOnRoad(&oncomingRoad); 

            for (VehicleState* otherCar : oncomingCars) {
                if (otherCar == nullptr || otherCar->isMarkedForDeletion) continue; // check for deleted cars
                float distToIntersection = oncomingRoad.getLength() - otherCar->getPos();
                float speed = std::max(otherCar->getSpeed(), 0.1f);
                
                if (distToIntersection > 0.0f) {
                    float timeToArrival = distToIntersection / speed;
                    
                    bool pathsConflict = true; 
                    if (myTurn == "right") {
                        uint64_t yieldingNextId = (yieldingCar->currentRouteIndex + 2 < yieldingCar->currentRoute.size()) ? yieldingCar->currentRoute[yieldingCar->currentRouteIndex + 2] : 0;
                        uint64_t otherNextId = (otherCar->currentRouteIndex + 2 < otherCar->currentRoute.size()) ? otherCar->currentRoute[otherCar->currentRouteIndex + 2] : 0;
                        
                        if (yieldingNextId != otherNextId) pathsConflict = false; 
                    }

                    if (pathsConflict && timeToArrival < criticalGapSeconds) {
                        return false; 
                    }
                } 
                else if (distToIntersection < 5.0f && distToIntersection > -15.0f) {
                    return false;
                }
            }
        }
    }
    return true; 
}

//sensor to activate protected left turn when needed
bool PhysicsProcessor::checkLeftTurnDemand(Node* node) {
    uint64_t nodeId = node->getId();
    IntersectionState& state = intersections[nodeId];
    
    // phase check
    int axisToCheck = (state.currentPhase == 9) ? 0 : 1;

    for (Road* incomingRoad : state.axisEdges[axisToCheck]) {
        std::vector<VehicleState*> cars = spatialHash->getVehiclesOnRoad(incomingRoad);
        
        for (VehicleState* car : cars) {
            float distToStopLine = incomingRoad->getLength() - car->getPos();
            
            // check if car is close to intersection and stopped
            if (distToStopLine > 0.0f && distToStopLine < 40.0f && car->getSpeed() < 1.0f) {
                
                // check if car has been waiting 5 seconds
                if (getUpcomingTurnDirection(car) == "left" && car->getWaitTime() > 5.0f) {
                    return true;
                }
            }
        }
    }
    return false;
}