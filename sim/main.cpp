#include "vehicle_state.h"
#include "physics_processor.h"
#include <memory>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <iomanip> // Needed for std::setw and std::fixed
#include <cstdint>
#include <chrono>

#include "node.h"
#include "network_builder.h"
#include "network.h"
#include "dstarlite.h"
#include "heuristics3d.h"
#include "traffic_manager.h"
#include "tm_logger.h"
#include "spatial_hash.h"

// int main()
// {
//     auto sT = std::chrono::high_resolution_clock::now();
//     Network orlandoMap = NetworkBuilder::buildNetworkFromJSONL(
//         "../python_pipeline/sample_out/nodes_orange_allroads_offline_xy.jsonl", 
//          "../python_pipeline/sample_out/edges_orange_allroads_offline_xy.jsonl"
//     );

//     float dt = 0.1;           // should be a set time step, before we were technically doing update(0.1) then udpate(0.2) etc.. oops
//     float currentTime = 0.0f; // total elapsed time
//     float maxT = 1000;        // runtime of the sim, currently set for 500 seconds (~8.5 mins)
    

//     TelemetryLogger logger("simulation_output.csv");
//     VehicleSpatialHash* spatialHash = new VehicleSpatialHash();
//     PhysicsProcessor controller(&orlandoMap, spatialHash);
//     TrafficManager spawner(&orlandoMap, &controller);

//     while (currentTime < maxT)
//     {        
//         spawner.update(dt);
//         controller.update(dt);
//         logger.logFrame(currentTime, controller.getActiveVehicles());

//         currentTime += dt;
//     }

//     auto eT = std::chrono::high_resolution_clock::now();
//     std::chrono::duration<double, std::milli> dur = eT - sT;
//     std::cout << "Program duration: " << dur.count() << "\n";
//     return 0;
// }

int main()
{
    auto sT = std::chrono::high_resolution_clock::now();
    
    std::cout << "Loading Waterford Map...\n";
    Network orlandoMap = NetworkBuilder::buildNetworkFromJSONL(
        "../python_pipeline/sample_out/waterford_nodes_orange_allroads_offline_xy.jsonl", 
        "../python_pipeline/sample_out/waterford_edges_orange_allroads_offline_xy.jsonl"
    );

    float dt = 0.1f;          
    float currentTime = 0.0f; 
    float maxT = 1000.0f; 
    
    TelemetryLogger logger("simulation_output.csv");
    VehicleSpatialHash* spatialHash = new VehicleSpatialHash();
    PhysicsProcessor controller(&orlandoMap, spatialHash);
    TrafficManager spawner(&orlandoMap, &controller, 200);
    std::cout << "Starting Pulse Debugger Loop...\n";
    int lastPrintedTime = -1;

    while (currentTime < maxT)
    {        
        // 1. Spawner Pulse
        std::cout << "[PULSE] Time: " << currentTime << " - Starting Spawner...\n";
        spawner.update(dt);
        
        // 2. Physics Pulse
        std::cout << "[PULSE] Time: " << currentTime << " - Starting Physics...\n";
        controller.update(dt);
        
        // 3. Logger Pulse
        std::cout << "[PULSE] Time: " << currentTime << " - Logging Frame...\n";
        logger.logFrame(currentTime, controller.getActiveVehicles());

        currentTime += dt;

        // Progress Summary
        int currentIntTime = static_cast<int>(currentTime);
        if (currentIntTime % 10 == 0 && currentIntTime != lastPrintedTime) {
            std::cout << "====================================================\n";
            std::cout << ">>> Sim Time: " << std::setw(3) << currentIntTime << "s / " << maxT 
                      << "s | Active Cars: " << controller.getActiveVehicles().size() << "\n";
            std::cout << "====================================================\n";
            lastPrintedTime = currentIntTime;
        }
    }

    auto eT = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> dur = eT - sT;
    std::cout << "Program duration: " << dur.count() << " ms\n";
    
    delete spatialHash;
    return 0;
}