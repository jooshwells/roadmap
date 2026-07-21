#include "traffic_manager.h"
#include "dstarlite.h"
#include "heuristics3d.h"
#include "idm_profiles.h"
#include "physics_processor.h"
#include "intersection_geometry.h"
#include <algorithm>

int SpawnScaling::computeTargetVehicleCount(const Network& network)
{
    const double laneMeters = network.getTotalLaneMeters();
    const int raw = static_cast<int>(laneMeters / LaneMetersPerVehicle);
    return std::clamp(raw, MinVehicles, MaxVehicles);
}

// Update constructor to take targetCount
TrafficManager::TrafficManager(Network* net, PhysicsProcessor* phys, int targetCount)
    : network(net), physicsLoop(phys) , targetVehicleCount(targetCount)
{
    rng.seed(std::random_device{}());
}

void TrafficManager::setThroughTrafficNodes(const std::vector<uint64_t>& sources, const std::vector<uint64_t>& sinks)
{
    sourceNodes = sources;
    sinkNodes = sinks;
}

void TrafficManager::setTargetVehicleCount(int count)
{
    targetVehicleCount = std::max(0, count);
}

void TrafficManager::update(float dt) 
{
    // Check how many cars are currently alive
    int currentCars = physicsLoop->getActiveVehicles().size();
    
    // We cap the attempts per frame to prevent freezing Unreal 
    // if it struggles to find an empty node on a highly congested map.
    int maxAttemptsThisFrame = 15; 
    int attempts = 0;

    // Loop until we reach the capacity-scaled target OR we run out of safe
    // attempts for this frame
    while (currentCars < targetVehicleCount && attempts < maxAttemptsThisFrame)
    {
        if (spawnRandomVehicle()) 
        {
            currentCars++; // Spawn successful, increment the count
        }
        attempts++; // Always increment attempts whether it succeeded or failed
    }
}

// Update to return bool instead of void
bool TrafficManager::spawnRandomVehicle() 
{
    Node* origin = nullptr;
    Node* destination = nullptr;

    // Roll the dice: 70% chance for through-traffic (if source/sink lists are populated)
    if (routingProbability(rng) <= 0.70f && !sourceNodes.empty() && !sinkNodes.empty()) 
    {
        std::uniform_int_distribution<std::size_t> sourceDist(0, sourceNodes.size() - 1);
        std::uniform_int_distribution<std::size_t> sinkDist(0, sinkNodes.size() - 1);
        
        uint64_t originId = sourceNodes[sourceDist(rng)];
        uint64_t destId = sinkNodes[sinkDist(rng)];

        origin = network->getNode(originId);
        destination = network->getNode(destId);
    }
    else 
    {
        // 30% chance (or fallback) for purely random residential/local traffic
        origin = network->getRandomNode(rng);
        destination = network->getRandomNode(rng);
    }

    // Safety checks: ensure pointers are valid and we aren't routing a node to itself
    if (!origin || !destination || origin == destination) return false; 

    // ---> RESTORED: Your pathfinding reset! Without this, routes will break. <---
    network->resetPathfindingState();

    uint64_t originId = origin->getId();
    uint64_t destId = destination->getId();

    // -- The rest of your existing logic remains exactly the same --
    
    int carsHeadingToDest = 0;
    for (VehicleState* v : physicsLoop->getActiveVehicles()) 
    {
        if (!v->currentRoute.empty() && v->currentRoute.back() == destId) 
        {
            carsHeadingToDest++;
        }
    }

    if (carsHeadingToDest >= 3) return false; // cap max vehicles going to one dest at 3

    DStarLite router(network, origin, destination, Heuristics3D::Euclidean);
    router.ComputeShortestPath();
    std::vector<uint64_t> route = router.ExtractRoute(*network, origin, destination);
    
    // ---> RESTORED: Your dead-end safety check! <---
    // change to 2 to fix dead end node bug, if D* spawns car on dead end, there is no outgoing edges
    if (route.size() < 2) return false; 

    bool isSpawnClear = true;
    for (VehicleState* vhcl : physicsLoop->getActiveVehicles())
    {
        if (!vhcl->currentRoute.empty() && vhcl->currentRoute[vhcl->currentRouteIndex] == originId)
        {
            // Using IDM length check
            if (vhcl->getPos() < (vhcl->getLength() + 5.0f)) 
            {
                isSpawnClear = false;
                break;
            }
        }
    }

    // Abort spawn if the intersection is blocked
    if (!isSpawnClear) return false; 

    // Roll a driver personality (cautious / average / aggressive) and sample
    // every IDM parameter from that archetype's range, so the population has
    // real spread: tailgating speeders, textbook drivers, and slowpokes who
    // ease away from every light.
    const DriverType driverType = IDM_Profiles::rollDriverType(rng);
    IDMParameters params = IDM_Profiles::sampleProfile(driverType, rng);

    // Tune the spawn to the road being entered instead of materializing at a
    // hardcoded 30 m/s in lane 0: enter at half the first edge's speed limit
    // (spawn nodes are usually intersections, and a car blasting through the
    // box at full arterial speed shoves everyone else aside), and start in a
    // lane the first movement is actually allowed from so short first hops
    // don't force a wrong-lane turn.
    Road* spawnEdge = nullptr;
    for (Road& e : origin->outgoingEdges) {
        if (e.getDest() == route[1]) { spawnEdge = &e; break; }
    }

    float spawnSpeed = 10.0f;
    int spawnLane = 0;
    if (spawnEdge != nullptr)
    {
        const float limit = static_cast<float>(spawnEdge->getSpeedLimit());
        spawnSpeed = 0.5f * limit;
        params.desiredSpeed = std::min(params.desiredSpeed, limit);

        if (route.size() >= 3)
        {
            Node* n1 = network->getNode(route[1]);
            Node* n2 = network->getNode(route[2]);
            if (n1 && n2)
            {
                const RoadIntersectionUtil::TurnDir firstTurn =
                    RoadIntersectionUtil::ClassifyTurnAtNodeTangent(
                        network, origin->getId(), n1->getId(), n2->getId());
                const uint8_t movement =
                      (firstTurn == RoadIntersectionUtil::TurnDir::Left)  ? TurnLane::Left
                    : (firstTurn == RoadIntersectionUtil::TurnDir::Right) ? TurnLane::Right
                                                                          : TurnLane::Through;
                const int allowed = spawnEdge->nearestLaneAllowing(0, movement);
                if (allowed >= 0) {
                    spawnLane = allowed;
                } else if (firstTurn == RoadIntersectionUtil::TurnDir::Right) {
                    spawnLane = std::max(0, spawnEdge->getLanes() - 1);
                }
            }
        }
    }

    VehicleState* newCar = new VehicleState(
        originId,
        destId,
        spawnSpeed,
        0.0f,
        spawnLane,
        params
    );
    
    newCar->currentRoute = route;
    newCar->currentRouteIndex = 0;

    physicsLoop->addVehicle(newCar);
    
    return true; 
}