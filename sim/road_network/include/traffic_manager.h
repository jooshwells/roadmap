#ifndef TRAFFIC_MANAGER_H
#define TRAFFIC_MANAGER_H

#include <random>
#include "network.h"
#include "physics_processor.h"
#include "dstarlite.h"

class TrafficManager 
{
    private:
        Network* network;
        PhysicsProcessor* physicsLoop;
        
        float timeSinceLastSpawn = 0.0f;
        float spawnInterval = 1.0f; // Spawn a new car every 2.0 seconds
        
        // RNG setup for random origins/destinations
        std::mt19937 rng;

    public:
        TrafficManager(Network* net, PhysicsProcessor* phys);
        void update(float dt);
        void spawnRandomVehicle();
};

#endif