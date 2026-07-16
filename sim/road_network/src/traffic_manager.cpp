#include "traffic_manager.h"
#include "dstarlite.h"
#include "heuristics3d.h"
#include "idm_profiles.h"
#include "physics_processor.h"
#include <algorithm>

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

    // Pick a driver class, then jitter its parameters so no two drivers are
    // identical (spread justified in traffic_manager.h next to the dists).
    IDMParameters params;
    switch (profileDist(rng)) {
        case 1:  params = IDM_Profiles::getAggressiveDriverProfile(); break;
        case 2:  params = IDM_Profiles::getSemiTruckProfile();        break;
        default: params = IDM_Profiles::getBasicDriverProfile();      break;
    }
    params.safeTimeHeadway = std::max(0.6f, params.safeTimeHeadway * paramJitter(rng));
    params.maxAccel        = std::max(0.3f, params.maxAccel        * paramJitter(rng));
    params.safeBrakePower  = std::max(0.5f, params.safeBrakePower  * paramJitter(rng));
    params.minGap          = std::max(0.5f, params.minGap          * paramJitter(rng));
    params.politeness      = std::clamp(params.politeness + politenessSpread(rng), 0.0f, 1.0f);
    params.speedFactor     = std::clamp(speedFactorDist(rng), 0.75f, 1.30f);

    // Enter at the first edge's (compliance-scaled) speed limit, capped by
    // the vehicle's own top speed — a truck must not spawn doing 30 m/s.
    float initialSpeed = params.desiredSpeed * 0.8f; // fallback if edge lookup fails
    if (route.size() > 1) {
        for (Road& edge : origin->outgoingEdges) {
            if (edge.getDest() == route[1]) {
                initialSpeed = std::min(
                    static_cast<float>(edge.getSpeedLimit()) * params.speedFactor,
                    params.desiredSpeed);
                break;
            }
        }
    }

    VehicleState* newCar = new VehicleState(
        originId,
        destId,
        initialSpeed,
        0.0f,
        0,
        params
    );
    
    newCar->currentRoute = route;
    newCar->currentRouteIndex = 0;

    physicsLoop->addVehicle(newCar);
    
    return true; 
}