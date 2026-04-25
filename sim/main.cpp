#include "vehicle_state.h"
#include "physics_processor.h"
#include <memory>
#include <iostream>
#include <fstream>
#include <iomanip> // Needed for std::setw and std::fixed
#include <cstdint>

#include "node.h"
#include "network_builder.h"
#include "network.h"
#include "dstarlite.h"
#include "heuristics3d.h"

std::vector<uint64_t> ExtractRoute(Network& map, Node* start, Node* goal) {
    std::vector<uint64_t> path;
    Node* current = start;

    std::cout << "\nExtracting computed route...\n";
    
    // Safety check in case no path exists (start->g == infinity)
    if (start->g == std::numeric_limits<double>::infinity()) {
        std::cout << "Error: No path exists to the goal!\n";
        return path; 
    }

    while (current != goal && current != nullptr) {
        path.push_back(current->getId());
        
        double min_cost = std::numeric_limits<double>::infinity();
        Node* best_next = nullptr;

        // D* Lite calculates gradients. To find the next step, we pick the 
        // outgoing edge that minimizes: (Edge Cost + Successor's g-value)
        for (Road& edge : current->outgoingEdges) {
            Node* succ = map.getNode(edge.getDest());
            if (!succ) continue;

            double cost = edge.getLength() + succ->g;
            if (cost < min_cost) {
                min_cost = cost;
                best_next = succ;
            }
        }

        if (!best_next) {
            std::cout << "Path dead-ended unexpectedly!\n";
            break; 
        }
        current = best_next;
    }
    
    path.push_back(goal->getId());
    return path;
}

int main()
{

    Network orlandoMap = NetworkBuilder::buildNetworkFromJSONL(
        "../python_pipeline/sample_out/nodes_orange_allroads_offline_xy.jsonl", 
        "../python_pipeline/sample_out/edges_orange_allroads_offline_xy.jsonl"
    );

    // 2. Verify it worked (Optional)
    Node* origin = orlandoMap.getNode(1);
    Node* dest = orlandoMap.getNode(100);
    // if (origin) {
    //     std::cout << "Origin loaded at X: " << origin->getX() << " Y: " << origin->getY() << "\n";
    //     std::cout << "Origin has " << origin->outgoingEdges.size() << " connected roads.\n";
    // }

    DStarLite router(&orlandoMap, origin, dest, Heuristics3D::Euclidean);
    router.ComputeShortestPath();

    std::vector<uint64_t> vehicleRoute = ExtractRoute(orlandoMap, origin, dest);

    for (uint64_t nodeId : vehicleRoute)
    {
        std::cout << nodeId << " -> ";
    }
    std::cout << "GOAL\n\n";

    /**
     * Debug
     */
    // orlandoMap.visualizeNetwork();
    // orlandoMap.visualizeNetworkForPython();

    // generic IDM parameters for new driver struct, 
    IDMParameters basicDriver = {
        4.0f,       // accelExp (delta)
        1.5f,       // maxAccel (a)
        32.0f,      // desiredSpeed (v0) ~70mph
        2.0f,       // minGap (s0)
        1.5f,       // safeBrakePower (b)
        1.5f,       // safeTimeHeadway (T)
        4.5f        // car length
    };
    // testing with more "agressive driver" behind first car
    IDMParameters aggressiveDriver = {
        4.0f,       // accelExp
        2.0f,       // maxAccel
        40.0f,      // desiredSpeed ~90mph
        1.0f,       // minGap
        2.5f,       // safeBrakePower
        0.8f,        // safeTimeHeadway
        4.5f        // car lenght
    };
    /* Start with vehicles positioned at x = 5 and 55 meters respectively */
    /* Also assume our test road has a speed limit of 70 mph (31.2928 m/s) */
    
    // testing faster car to test braking
    VehicleState* vhcl1 = new VehicleState(32, 0, aggressiveDriver);
    // std::cout << "We have " << vhcl1->getCount() << " vehicles." << std::endl;
    VehicleState* vhcl2 = new VehicleState(29, 50, basicDriver);
    // std::cout << "We have " << vhcl1->getCount() << " vehicles." << std::endl;

    vhcl1->currentRoute = vehicleRoute;
    vhcl1->currentRouteIndex = 0;

    vhcl2->currentRoute = vehicleRoute;
    vhcl2->currentRouteIndex = 0;

    vhcl1->setLeader(vhcl2);
    float dt = 0.1;       // should be a set time step, before we were technically doing update(0.1) then udpate(0.2) etc.. oops
    float currentTime = 0.0f; // total elapsed time
    float maxT = 100;  // runtime of the sim, currently set for 1200 seconds (20 mins)
    
    PhysicsProcessor controller(&orlandoMap);
    controller.addVehicle(vhcl1);
    controller.addVehicle(vhcl2);

    // testing output
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "===============================================================\n";
    std::cout << "Time (s) | Car 1 (Trailing)  | Car 2 (Leading)   | Gap (m) \n";
    std::cout << "         | Speed   | Pos     | Speed   | Pos     |         \n";
    std::cout << "===============================================================\n";

    /**
     * This loop simulates physics every 0.1 seconds of sim time. Note this
     * does not equate to 0.1 seconds of real time, as this loop will run
     * as fast as your cpu will let it (so pretty fast for just 12,000 iterations).
     */
    while (currentTime < maxT)
    {
        controller.update(dt);
        
        // Use the controller's multi-edge gap calculator instead of naive subtraction
        float gap = controller.calculateTrueGap(vhcl1, vhcl2);

        // Print formatted row
        std::cout << std::setw(8)  << currentTime << " | "
                  << std::setw(7)  << vhcl1->getSpeed() << " | "
                  << std::setw(7)  << vhcl1->getPos() << " | "
                  << std::setw(7)  << vhcl2->getSpeed() << " | "
                  << std::setw(7)  << vhcl2->getPos() << " | "
                  << std::setw(7)  << gap << "\n";

        currentTime += dt;
    }

    return 0;
}