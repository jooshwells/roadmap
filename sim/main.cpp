#include "vehicle_state.h"
#include "physics_processor.h"
#include <memory>
#include <iostream>
#include <iomanip> // Needed for std::setw and std::fixed

int main()
{
    // generic IDM parameters for new driver struct, 
    IDMParameters basicDriver = {
        4.0f,       // accelExp (delta)
        1.5f,       // maxAccel (a)
        32.0f,   // desiredSpeed (v0) ~70mph
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
    VehicleState* vhcl1 = new VehicleState(29, 300, basicDriver);
    VehicleState* vhcl2 = new VehicleState(32, 150, aggressiveDriver);
    VehicleState* vhcl3 = new VehicleState(20, 0, semiTruck);

    vhcl2->setLeader(vhcl1); // aggressive follows basic
    vhcl3->setLeader(vhcl2); //truck follows aggressive

    float dt = 0.1;       // should be a set time step, before we were technically doing update(0.1) then udpate(0.2) etc.. oops
    float currentTime = 0.0f; // total elapsed time
    float maxT = 75;  // runtime of the sim,
    PhysicsProcessor controller;
    controller.addVehicle(vhcl1);
    controller.addVehicle(vhcl2);
    controller.addVehicle(vhcl3);

    // testing output with 3 cars
    std::cout << std::fixed << std::setprecision(2);
  std::cout << "=========================================================================================================\n";
    std::cout << "Time (s) | Car 1 (Norm/Front)| Car 2 (Aggr/Mid)  | Car 3 (Semi/Back)    | Gaps (m) \n";
    std::cout << "         | Speed   | Pos     | Speed   | Pos     | Speed      | Pos     | 2->1    | 3->2 \n";
    std::cout << "=========================================================================================================\n";

    /**
     * This loop simulates physics every 0.1 seconds of sim time. Note this
     * does not equate to 0.1 seconds of real time, as this loop will run
     * as fast as your cpu will let it (so pretty fast for just 12,000 iterations).
     */
    while (currentTime < maxT)
    {
        controller.update(dt);
        float gap21 = vhcl1->getPos() - vhcl2->getPos();
        float gap32 = vhcl2->getPos() - vhcl3->getPos();

       // Print formatted row
        std::cout << std::setw(8)  << currentTime << " | "
                  << std::setw(7)  << vhcl1->getSpeed() << " | "
                  << std::setw(7)  << vhcl1->getPos() << " | "
                  << std::setw(7)  << vhcl2->getSpeed() << " | "
                  << std::setw(7)  << vhcl2->getPos() << " | "
                  << std::setw(7)  << vhcl3->getSpeed() << " | "
                  << std::setw(7)  << vhcl3->getPos() << " | "
                  << std::setw(7)  << gap21 << " | "
                  << std::setw(7)  << gap32 << "\n";

        currentTime += dt;
  
    }
    return 0;
}