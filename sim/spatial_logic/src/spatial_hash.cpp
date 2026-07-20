#include "spatial_hash.h"
#include "intersection_geometry.h"

void VehicleSpatialHash::rebuild(const std::vector<VehicleState*>& activeVehicles) {
    edgeBuckets.clear(); // Clear last frame's spatial data

    // Populate the buckets
    for (VehicleState* v : activeVehicles) {
        Road* currentRoad = v->getCurrentEdge();
        if (!currentRoad) continue;

        int lane = v->getLane();

        // Failsafe: Prevent negative lane indexes
        if (lane < 0) lane = 0;

        // A car mid-lane-change physically straddles both lanes for the
        // whole transition (m_lane commits to the target immediately), so it
        // must occupy both buckets. Hashing it only in the target lane makes
        // the lane it is still sliding out of look empty, inviting other
        // cars to merge into the straddling body.
        int laneFrom = v->isChangingLanes() ? v->getPreviousLane() : lane;
        if (laneFrom < 0) laneFrom = 0;

        // Failsafe: Resize the road's lane vector to guarantee the requested lane index exists
        auto& lanes = edgeBuckets[currentRoad];
        int maxLane = std::max(lane, laneFrom);
        if ((int)lanes.size() <= maxLane) {
            lanes.resize(maxLane + 1);
        }

        lanes[lane].push_back(v);
        if (laneFrom != lane) lanes[laneFrom].push_back(v);
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
            if (other == vhcl) continue;
            // Id-tiebreak on exactly equal positions: without it a car dead
            // alongside is neither leader nor follower and MOBIL's safety
            // veto never sees it.
            if (other->getPos() > vhcl->getPos() ||
                (other->getPos() == vhcl->getPos() && other->getId() > vhcl->getId())) {
                return other; // Found the immediate leader
            }
        }
    }

    // Start with the distance remaining on the current road
    float accumulatedDistance = currentRoad->getLength() - vhcl->getPos();

    // Define a maximum sensor range (e.g., 150 meters).
    float maxSensorRange = std::max(150.0f, vhcl->getSpeed() * 8.0f);

    // The lane to scan shifts at every node crossing: the edge transition
    // re-seats the car (right turns land in the rightmost lane, left turns
    // in lane 0, through clamps), so the sensor must look at the lane the
    // car will actually land in, not the index it occupies now.
    int searchLane = targetLane;
    // Edge the car crosses each node FROM -- the rank-aware arrival mapping
    // needs the approach's turn map to know which of two side-by-side turn
    // lanes this one is.
    Road* approachRoad = currentRoad;

    for (size_t i = vhcl->currentRouteIndex + 1; i < vhcl->currentRoute.size() - 1; i++) {
        // If we have searched far enough ahead without finding a car, the road is clear
        if (accumulatedDistance > maxSensorRange) break;

        int startNodeId = vhcl->currentRoute[i];
        int endNodeId = vhcl->currentRoute[i + 1];
        Node* startNode = network->getNode(startNodeId);
        if (!startNode) break;

        Road* nextRoad = nullptr;
        for (Road& edge : startNode->outgoingEdges) {
            if (edge.getDest() == endNodeId) {
                nextRoad = &edge;
                break;
            }
        }

        if (!nextRoad) break; // Reached end of known route

        // Map the lane across the node with the same rule the transition uses
        Node* prevNode = network->getNode(vhcl->currentRoute[i - 1]);
        Node* endNode = network->getNode(endNodeId);
        RoadIntersectionUtil::TurnDir turn = RoadIntersectionUtil::TurnDir::Through;
        if (prevNode && endNode) {
            turn = RoadIntersectionUtil::ClassifyTurnAtNode(*prevNode, *startNode, *endNode);
        }
        searchLane = RoadIntersectionUtil::GetArrivalLane(turn, searchLane, nextRoad->getLanes(), approachRoad);
        approachRoad = nextRoad;

        if (edgeBuckets.count(nextRoad) && edgeBuckets[nextRoad].size() > searchLane) {
            const auto& nextRoadLanes = edgeBuckets[nextRoad][searchLane];
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
            if (other == vhcl) continue;
            // Mirror of the leader tiebreak: the equal-position car with the
            // lower id counts as the follower.
            if (other->getPos() < vhcl->getPos() ||
                (other->getPos() == vhcl->getPos() && other->getId() < vhcl->getId())) {
                return other;
            }
        }
    }

    // Start with the distance from the vehicle to the beginning of its current road
    float accumulatedDistance = vhcl->getPos();

    // Keep this consistent with the leader's sensor range
    float maxSensorRange = std::max(150.0f, vhcl->getSpeed() * 8.0f);

    // Walking backwards, invert the arrival-lane rule: traffic that lands in
    // searchLane via a right turn approaches in the previous road's rightmost
    // lane (where turn guidance puts it), via a left turn in its lane 0, and
    // through traffic keeps its index. If the movement at a node can't land
    // in searchLane at all, nothing behind that node can follow into this
    // lane, so the walk stops there.
    int searchLane = targetLane;
    int laterLanes = currentRoad->getLanes();

    // Iterate backwards through the route safely
    for (int i = (int)vhcl->currentRouteIndex - 1; i >= 0; i--) {
        // If we have searched far enough behind, stop checking
        if (accumulatedDistance > maxSensorRange) break;

        int startNodeId = vhcl->currentRoute[i];
        int endNodeId = vhcl->currentRoute[i + 1];
        Node* startNode = network->getNode(startNodeId);
        if (!startNode) break;

        Road* prevRoad = nullptr;
        for (Road& edge : startNode->outgoingEdges) {
            if (edge.getDest() == endNodeId) {
                prevRoad = &edge;
                break;
            }
        }

        if (!prevRoad) break; // Route breaks or missing edge

        // Movement made at node route[i+1], from prevRoad onto the later edge
        Node* endNode = network->getNode(endNodeId);
        Node* afterNode = (i + 2 < (int)vhcl->currentRoute.size())
            ? network->getNode(vhcl->currentRoute[i + 2]) : nullptr;
        RoadIntersectionUtil::TurnDir turn = RoadIntersectionUtil::TurnDir::Through;
        if (endNode && afterNode) {
            turn = RoadIntersectionUtil::ClassifyTurnAtNode(*startNode, *endNode, *afterNode);
        }

        int sourceLane;
        if (turn == RoadIntersectionUtil::TurnDir::Right) {
            if (searchLane != laterLanes - 1) break;
            sourceLane = prevRoad->getLanes() - 1;
        } else if (turn == RoadIntersectionUtil::TurnDir::Left) {
            if (searchLane != 0) break;
            sourceLane = 0;
        } else {
            sourceLane = std::clamp(searchLane, 0, prevRoad->getLanes() - 1);
        }

        if (edgeBuckets.count(prevRoad) && edgeBuckets[prevRoad].size() > sourceLane) {
            const auto& prevRoadLanes = edgeBuckets[prevRoad][sourceLane];
            if (!prevRoadLanes.empty()) {
                // The follower is the last car on this previous road (highest pos)
                return prevRoadLanes.back();
            }
        }

        // Add this previous road's full length to our search distance
        accumulatedDistance += prevRoad->getLength();
        searchLane = sourceLane;
        laterLanes = prevRoad->getLanes();
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
        // A straddling car sits in two buckets, and a car that committed a
        // lane change after the frame-start rebuild sits in buckets that no
        // longer match its live lane at all -- so dedupe by identity rather
        // than by matching the committed lane against the bucket index
        // (which silently dropped those cars from intersection logic).
        std::unordered_set<VehicleState*> seen;
        const auto& lanes = edgeBuckets[road];
        for (size_t laneIdx = 0; laneIdx < lanes.size(); laneIdx++)
        {
            for (VehicleState* v : lanes[laneIdx])
            {
                if (!seen.insert(v).second) continue;
                vehiclesOnRoad.push_back(v);
            }
        }
    }
    
    return vehiclesOnRoad;
}