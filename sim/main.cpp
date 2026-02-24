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
        1.5f        // safeTimeHeadway (T)
    };
    // testing with more "agressive driver" behind first car
    IDMParameters aggressiveDriver = {
        4.0f,       // accelExp
        2.0f,       // maxAccel
        40.0f,      // desiredSpeed ~90mph
        1.0f,       // minGap
        2.5f,       // safeBrakePower
        0.8f        // safeTimeHeadway
    };
    /* Start with vehicles positioned at x = 5 and 55 meters respectively */
    /* Also assume our test road has a speed limit of 70 mph (31.2928 m/s) */
    
    // testing faster car to test braking
    VehicleState* vhcl1 = new VehicleState(32, 0, aggressiveDriver);
    // std::cout << "We have " << vhcl1->getCount() << " vehicles." << std::endl;
    VehicleState* vhcl2 = new VehicleState(29, 250, basicDriver);
    // std::cout << "We have " << vhcl1->getCount() << " vehicles." << std::endl;

    vhcl1->setLeader(vhcl2);
    float dt = 0.1;       // should be a set time step, before we were technically doing update(0.1) then udpate(0.2) etc.. oops
    float currentTime = 0.0f; // total elapsed time
    float maxT = 100;  // runtime of the sim, currently set for 1200 seconds (20 mins)
    
    PhysicsProcessor controller;
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
        float gap = vhcl2->getPos() - vhcl1->getPos();

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