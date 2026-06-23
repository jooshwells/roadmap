#include "traffic_manager.h"
#include "dstarlite.h"
#include "heuristics3d.h"
#include "idm_profiles.h"
#include "physics_processor.h"

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

void TrafficManager::update(float dt) 
{
    // Check how many cars are currently alive
    int currentCars = physicsLoop->getActiveVehicles().size();
    
    // We cap the attempts per frame to prevent freezing Unreal 
    // if it struggles to find an empty node on a highly congested map.
    int maxAttemptsThisFrame = 15; 
    int attempts = 0;

    // Loop until we reach 1000 cars OR we run out of safe attempts for this frame
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
    
    if (route.empty()) return false; 

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

    if (!isSpawnClear) return false; 

    VehicleState* newCar = new VehicleState(
        originId, 
        destId, 
        30.0f, 
        0.0f,  
        0,     
        IDM_Profiles::getBasicDriverProfile()
    );
    
    newCar->currentRoute = route;
    newCar->currentRouteIndex = 0;

    physicsLoop->addVehicle(newCar);
    
    return true; 
}