#include "traffic_manager.h"
#include "dstarlite.h"
#include "heuristics3d.h"
#include "idm_profiles.h"
#include "physics_processor.h"

TrafficManager::TrafficManager(Network* net, PhysicsProcessor* phys) 
    : network(net), physicsLoop(phys) 
{
    // Initialize RNG seed
    rng.seed(std::random_device{}());
}

void TrafficManager::update(float dt) 
{
    timeSinceLastSpawn += dt;
    
    if (timeSinceLastSpawn >= spawnInterval) 
    {
        spawnRandomVehicle();
        timeSinceLastSpawn = 0.0f; // Reset timer
    }
}

void TrafficManager::spawnRandomVehicle() 
{
    // Pick a random origin and destination from your network
    Node* origin = network->getRandomNode(rng);
    Node* destination = network->getRandomNode(rng);

    if (origin == destination) return; // Prevent 0-length routes

    // Compute the route
    DStarLite router(network, origin, destination, Heuristics3D::Euclidean);
    router.ComputeShortestPath();
    std::vector<uint64_t> route = DStarLite::ExtractRoute(*network, origin, destination);

    if (route.empty()) return; // Map is disconnected, no route found

    // Extract the IDs for telemetry and routing checks
    uint64_t originId = origin->getId();
    uint64_t destId = destination->getId();

    // spawn safety check
    bool isSpawnClear = true;
    for (VehicleState* vhcl : physicsLoop->getActiveVehicles())
    {
        // Check if an existing vehicle is currently on our starting node
        if (!vhcl->currentRoute.empty() && vhcl->currentRoute[vhcl->currentRouteIndex] == originId)
        {
            // Check if they are physically too close to the spawn line (0.0m)
            if (vhcl->getPos() < (vhcl->getLength() + 5.0f)) 
            {
                isSpawnClear = false;
                break;
            }
        }
    }

    // Abort spawn if the intersection is blocked
    if (!isSpawnClear) return; 

    // 3. Create the vehicle
    VehicleState* newCar = new VehicleState(
        originId, 
        destId, 
        30.0f, // Initial Speed
        0.0f,  // Initial Pos
        0,     // Starting Lane
        IDM_Profiles::getBasicDriverProfile()
    );
    
    newCar->currentRoute = route;
    newCar->currentRouteIndex = 0;

    physicsLoop->addVehicle(newCar);
}