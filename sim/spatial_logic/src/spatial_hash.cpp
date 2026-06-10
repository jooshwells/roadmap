#include "spatial_hash.h"

void VehicleSpatialHash::rebuild(const std::vector<VehicleState*>& activeVehicles) {
    edgeBuckets.clear(); // Clear last frame's spatial data

    // Populate the buckets
    for (VehicleState* v : activeVehicles) {
        Road* currentRoad = v->getCurrentEdge();
        if (!currentRoad) continue;

        int lane = v->getLane();

        // Failsafe: Prevent negative lane indexes
        if (lane < 0) lane = 0; 
        
        // Failsafe: Resize the road's lane vector to guarantee the requested lane index exists
        if (edgeBuckets[currentRoad].size() <= lane) {
            edgeBuckets[currentRoad].resize(lane + 1);
        }
        
        // Ensure the 2D vector is sized correctly for the lanes on this road
        if (edgeBuckets[currentRoad].size() <= lane) {
            edgeBuckets[currentRoad].resize(lane + 1);
        }
        
        edgeBuckets[currentRoad][lane].push_back(v);
    }

    // Sort each lane's vehicles by position (distance along the edge)
    for (auto& [road, lanes] : edgeBuckets) {
        for (auto& lane_vehicles : lanes) {
            std::sort(lane_vehicles.begin(), lane_vehicles.end(), 
                [](VehicleState* a, VehicleState* b) {
                    return a->getPos() < b->getPos(); 
                });
        }
    }
}

VehicleState* VehicleSpatialHash::getLeader(VehicleState* vhcl, int targetLane, Network* network) {
    Road* currentRoad = vhcl->getCurrentEdge();
    if (!currentRoad) return nullptr;
    
    // Check the current road first
    if (edgeBuckets.count(currentRoad) && edgeBuckets[currentRoad].size() > targetLane) {
        const auto& laneVehicles = edgeBuckets[currentRoad][targetLane];
        
        for (VehicleState* other : laneVehicles) {
            if (other != vhcl && other->getPos() > vhcl->getPos()) {
                return other; // Found the immediate leader
            }
        }
    }

    // Start with the distance remaining on the current road
    float accumulatedDistance = currentRoad->getLength() - vhcl->getPos();
    
    // Define a maximum sensor range (e.g., 150 meters). 
    float maxSensorRange = std::max(150.0f, vhcl->getSpeed() * 8.0f);

    for (size_t i = vhcl->currentRouteIndex + 1; i < vhcl->currentRoute.size() - 1; i++) {
        // If we have searched far enough ahead without finding a car, the road is clear
        if (accumulatedDistance > maxSensorRange) break; 

        int startNodeId = vhcl->currentRoute[i];
        int endNodeId = vhcl->currentRoute[i + 1];
        Node* startNode = network->getNode(startNodeId);
        
        Road* nextRoad = nullptr;
        for (Road& edge : startNode->outgoingEdges) {
            if (edge.getDest() == endNodeId) {
                nextRoad = &edge;
                break;
            }
        }

        if (!nextRoad) break; // Reached end of known route

        if (edgeBuckets.count(nextRoad) && edgeBuckets[nextRoad].size() > targetLane) {
            const auto& nextRoadLanes = edgeBuckets[nextRoad][targetLane];
            if (!nextRoadLanes.empty()) {
                // The leader is the first car on this next road
                return nextRoadLanes.front();
            }
        }

        // Add this road's full length to our search distance for the next iteration
        accumulatedDistance += nextRoad->getLength();
    }

    return nullptr; // Open road ahead within sensor range
}

VehicleState* VehicleSpatialHash::getFollower(VehicleState* vhcl, int targetLane, Network* network) {
    Road* currentRoad = vhcl->getCurrentEdge();
    if (!currentRoad) return nullptr;
    
    // Check current road
    if (edgeBuckets.count(currentRoad) && edgeBuckets[currentRoad].size() > targetLane) {
        const auto& laneVehicles = edgeBuckets[currentRoad][targetLane];
        
        // Iterate backwards through the lane array to find the closest car behind
        for (auto it = laneVehicles.rbegin(); it != laneVehicles.rend(); ++it) {
            VehicleState* other = *it;
            if (other != vhcl && other->getPos() < vhcl->getPos()) {
                return other; 
            }
        }
    }

    // Start with the distance from the vehicle to the beginning of its current road
    float accumulatedDistance = vhcl->getPos();
    
    // Keep this consistent with the leader's sensor range
    float maxSensorRange = std::max(150.0f, vhcl->getSpeed() * 8.0f);
    
    // Iterate backwards through the route safely
    for (int i = (int)vhcl->currentRouteIndex - 1; i >= 0; i--) {
        // If we have searched far enough behind, stop checking
        if (accumulatedDistance > maxSensorRange) break;

        int startNodeId = vhcl->currentRoute[i];
        int endNodeId = vhcl->currentRoute[i + 1];
        Node* startNode = network->getNode(startNodeId);
        
        Road* prevRoad = nullptr;
        for (Road& edge : startNode->outgoingEdges) {
            if (edge.getDest() == endNodeId) {
                prevRoad = &edge;
                break;
            }
        }

        if (!prevRoad) break; // Route breaks or missing edge

        if (edgeBuckets.count(prevRoad) && edgeBuckets[prevRoad].size() > targetLane) {
            const auto& prevRoadLanes = edgeBuckets[prevRoad][targetLane];
            if (!prevRoadLanes.empty()) {
                // The follower is the last car on this previous road (highest pos)
                return prevRoadLanes.back(); 
            }
        }

        // Add this previous road's full length to our search distance
        accumulatedDistance += prevRoad->getLength();
    }

    return nullptr; // No follower found within sensor range
}

// return list of all vehicles on roads connecting to interseection
std::vector<VehicleState*> VehicleSpatialHash::getVehiclesOnRoad(Road* road) 
{
    std::vector<VehicleState*> vehiclesOnRoad;
    
    // Safety check
    if (!road) return vehiclesOnRoad;

    // Check if this road currently has any vehicles mapped to it
    if (edgeBuckets.find(road) != edgeBuckets.end()) 
    {
        // Loop through all the lanes on this road
        for (const auto& laneVehicles : edgeBuckets[road]) 
        {
            // Append all vehicles in this lane to our master list
            vehiclesOnRoad.insert(vehiclesOnRoad.end(), laneVehicles.begin(), laneVehicles.end());
        }
    }
    
    return vehiclesOnRoad;
}