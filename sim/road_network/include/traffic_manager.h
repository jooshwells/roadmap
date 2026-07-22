#ifndef TRAFFIC_MANAGER_H
#define TRAFFIC_MANAGER_H

#include <random>
#include <vector>
#include "network.h"
#include "physics_processor.h"
#include "dstarlite.h"

// Density-based spawn scaling. The number of concurrently active vehicles a
// map targets is derived from its total road storage capacity (directed
// lane-meters) instead of one fixed constant, so a compact map and a
// sprawling one both settle at the same visual density instead of the same
// raw count.
namespace SpawnScaling
{
    // Average meters of lane per active vehicle at the target density
    // (~7 vehicles per lane-km -- busy but free-flowing). Lower = denser.
    // Calibrated against the bundled all-roads map (~422k lane-m -> ~2960
    // vehicles). This is the one knob to turn for global density.
    constexpr double LaneMetersPerVehicle = 1000.0 / 7.0; // ~142.9 m/vehicle

    // Clamp bounds: the floor keeps a tiny map from feeling deserted; the
    // ceiling is the render/physics budget so a huge map can't spawn its way
    // into a slideshow.
    constexpr int MinVehicles = 200;
    constexpr int MaxVehicles = 3500;

    // Storage-capacity target for one network:
    // clamp(totalLaneMeters / LaneMetersPerVehicle, Min, Max).
    int computeTargetVehicleCount(const Network& network);
}

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

        // Retarget the active-vehicle count, e.g. after a runtime road edit
        // changed the network's storage capacity. Negatives are floored at 0.
        void setTargetVehicleCount(int count);
        int getTargetVehicleCount() const { return targetVehicleCount; }

        void update(float dt);
        bool spawnRandomVehicle(); 
};

#endif