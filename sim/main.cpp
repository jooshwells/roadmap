#include "vehicle_state.h"
#include <memory>
#include <iostream>

int main()
{
    /* Start with vehicles positioned at x = 5 and 55 meters respectively */
    /* Also assume our test road has a speed limit of 70 mph (31.2928 m/s) */
    

    VehicleState* vhcl1 = new VehicleState(31.2928, 5);
    // std::cout << "We have " << vhcl1->getCount() << " vehicles." << std::endl;
    VehicleState* vhcl2 = new VehicleState(31.2928, 55);
    // std::cout << "We have " << vhcl1->getCount() << " vehicles." << std::endl;

    vhcl1->setLeader(vhcl2);
    float dt = 0;       // delta t, our time variable for the physics equations
    float maxT = 1200;  // runtime of the sim, currently set for 1200 seconds (20 mins)
    
    /**
     * This loop simulates physics every 0.1 seconds of sim time. Note this
     * does not equate to 0.1 seconds of real time, as this loop will run
     * as fast as your cpu will let it (so pretty fast for just 12,000 iterations).
     */
    while (dt < maxT)
    {
        

        dt += 0.1;
    }
    return 0;
}