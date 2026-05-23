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

int main()
{
    Network orlandoMap = NetworkBuilder::buildNetworkFromJSONL(
        "../python_pipeline/sample_out/nodes_orange_allroads_offline_xy.jsonl", 
        "../python_pipeline/sample_out/edges_orange_allroads_offline_xy.jsonl"
    );

    float dt = 0.1;           // should be a set time step, before we were technically doing update(0.1) then udpate(0.2) etc.. oops
    float currentTime = 0.0f; // total elapsed time
    float maxT = 500;         // runtime of the sim, currently set for 500 seconds (~8.5 mins)
    
    PhysicsProcessor controller(&orlandoMap);

    TelemetryLogger logger("simulation_output.csv");
    TrafficManager spawner(&orlandoMap, &controller);

    while (currentTime < maxT)
    {        
        spawner.update(dt);
        controller.update(dt);
        logger.logFrame(currentTime, controller.getActiveVehicles());

        currentTime += dt;
    }

    return 0;
}