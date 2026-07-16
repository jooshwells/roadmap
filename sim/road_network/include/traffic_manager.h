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

        // Per-driver politeness spread so lane-change speed varies car to car
        std::uniform_real_distribution<float> politenessSpread{-0.2f, 0.4f};

        // Driver population mix: Basic / Aggressive / SemiTruck.
        // ~10% trucks matches FHWA urban-arterial classification counts;
        // ~20% aggressive matches the sub-1s tail of observed headways.
        std::discrete_distribution<int> profileDist{70, 20, 10};

        // Speed-limit compliance: free-flow speed studies put the spread at
        // ~8-12% of the mean. Clamped to [0.75, 1.30] at spawn.
        std::normal_distribution<float> speedFactorDist{1.0f, 0.08f};

        // +/-15% multiplicative jitter on core IDM params (Treiber & Kesting
        // 2013 ch. 12.3 model heterogeneity as 15-20% spread on class means)
        std::uniform_real_distribution<float> paramJitter{0.85f, 1.15f};

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