#include "vehicle_state.h"
#include "physics_processor.h"
#include <memory>
#include <iostream>
#include <iomanip> // Needed for std::setw and std::fixed
#include "node.h"
#include "network_builder.h"
#include "network.h"

int main()
{

    Network orlandoMap = NetworkBuilder::buildNetworkFromJSONL(
        "../python_pipeline/sample_output/nodes_motorways_simplified.jsonl", 
        "../python_pipeline/sample_output/edges_motorways_simplified.jsonl"
    );

    // // 2. Verify it worked (Optional)
    // Node* origin = orlandoMap.getNode(1);
    // if (origin) {
    //     std::cout << "Origin loaded at X: " << origin->getX() << " Y: " << origin->getY() << "\n";
    //     std::cout << "Origin has " << origin->outgoingEdges.size() << " connected roads.\n";
    // }

   //orlandoMap.visualizeNetwork();

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
    // Semi-truck, slower accel, max speed, needs more braking room
    IDMParameters semiTruck = {
        4.0f,       // accelExp
        0.8f,       // maxAccel 
        25.0f,      // desiredSpeed abt 55mph
        4.0f,       // minGap 
        1.0f,       // safeBrakePower 
        2.5f,       // safeTimeHeadway 
        18.0f       // car length 
    };
   
    
    // testing 3 cars now, normal driver, aggressive, then semi truck
    // scenario 1, car1 is "polite" moves so car2 can pass
    // VehicleState* vhcl1 = new VehicleState(29, 32, 0, basicDriver);
    // VehicleState* vhcl2 = new VehicleState(32, 0, 0, aggressiveDriver);
    // VehicleState* vhcl3 = new VehicleState(20, 30, 1, semiTruck);

    // scenario 2, car2 stuck behind truck, moves behind car1, then passes truck
    VehicleState* vhcl1 = new VehicleState(29, 32, 1, basicDriver);
    VehicleState* vhcl2 = new VehicleState(32, 0, 0, aggressiveDriver);
    VehicleState* vhcl3 = new VehicleState(20, 30, 0, semiTruck);


    vhcl2->setLeader(vhcl1); // aggressive follows basic
    vhcl3->setLeader(vhcl2); //truck follows aggressive

    float dt = 0.1;       // should be a set time step, before we were technically doing update(0.1) then udpate(0.2) etc.. oops
    float currentTime = 0.0f; // total elapsed time
    float maxT =20;  // runtime of the sim,
    PhysicsProcessor controller;
    controller.addVehicle(vhcl1);
    controller.addVehicle(vhcl2);
    controller.addVehicle(vhcl3);

    // testing output with 3 cars
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "==========================================================================================\n";
    std::cout << "Time (s) | Car 1 (Norm/Front)   | Car 2 (Aggr/Mid)     | Car 3 (Semi/Back)    |\n";
    std::cout << "         | Spd    | Pos   | Ln  | Spd    | Pos   | Ln  | Spd    | Pos   | Ln  |\n";
    std::cout << "==========================================================================================\n";

    /**
     * This loop simulates physics every 0.1 seconds of sim time. Note this
     * does not equate to 0.1 seconds of real time, as this loop will run
     * as fast as your cpu will let it (so pretty fast for just 12,000 iterations).
     */
    while (currentTime < maxT)
    {
        controller.update(dt);

       // Print formatted row
        std::cout << std::setw(8)  << currentTime << " | "
                  << std::setw(6)  << vhcl1->getSpeed() << " | "
                  << std::setw(5)  << vhcl1->getPos() << " | "
                  << std::setw(3)  << vhcl1->getLane() << " | "
                  << std::setw(6)  << vhcl2->getSpeed() << " | "
                  << std::setw(5)  << vhcl2->getPos() << " | "
                  << std::setw(3)  << vhcl2->getLane() << " | "
                  << std::setw(6)  << vhcl3->getSpeed() << " | "
                  << std::setw(5)  << vhcl3->getPos() << " | "
                  << std::setw(3)  << vhcl3->getLane() << " |\n";
        currentTime += dt;
  
    }

    return 0;
}