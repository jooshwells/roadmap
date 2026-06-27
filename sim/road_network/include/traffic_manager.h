#ifndef TRAFFIC_MANAGER_H
#define TRAFFIC_MANAGER_H

#include <random>
#include <vector>
#include "network.h"
#include "physics_processor.h"
#include "dstarlite.h"

class TrafficManager 
{
    private:
        Network* network;
        PhysicsProcessor* physicsLoop;
        
        int targetVehicleCount; 
        
        std::mt19937 rng;

        // New probability distribution for the 70/30 split
        std::uniform_real_distribution<float> routingProbability{0.0f, 1.0f};

        // Containers for through-traffic nodes
        std::vector<uint64_t> sourceNodes;
        std::vector<uint64_t> sinkNodes;

    public:
        TrafficManager(Network* net, PhysicsProcessor* phys, int targetCount);
        
        // Setter to populate the through-traffic nodes
        void setThroughTrafficNodes(const std::vector<uint64_t>& sources, const std::vector<uint64_t>& sinks);
        
        void update(float dt);
        bool spawnRandomVehicle(); 
};

#endif