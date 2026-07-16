#ifndef IDM_PROFILES_H
#define IDM_PROFILES_H

#include "vehicle_state.h"

class IDM_Profiles
{
    public:
        static IDMParameters getBasicDriverProfile()
        {
            return 
            {
                4.0f,       // accelExp (delta)
                1.5f,       // maxAccel (a)
                32.0f,      // desiredSpeed (v0) ~70mph
                2.0f,       // minGap (s0)
                1.5f,       // safeBrakePower (b)
                1.5f,       // safeTimeHeadway (T)
                4.5f,       // car length
                0.3f,       // politeness (p)
                1.0f,       // speedFactor (jittered per driver at spawn)
                2.0f        // bSafeMobil (comfortable b_safe, Kesting et al. 2007)
            };
        }
        
        static IDMParameters getAggressiveDriverProfile()
        {
            return 
            {
                4.0f,       // accelExp
                2.0f,       // maxAccel
                40.0f,      // desiredSpeed ~90mph
                1.0f,       // minGap
                2.5f,       // safeBrakePower
                0.8f,       // safeTimeHeadway
                4.5f,       // car lenght
                0.05f,      // politeness (darts across lanes)
                1.0f,       // speedFactor (jittered per driver at spawn)
                3.0f        // bSafeMobil (forces harder braking on others)
            };
        }

        static IDMParameters getSemiTruckProfile()
        {
            return
            {
                4.0f,       // accelExp
                0.8f,       // maxAccel 
                25.0f,      // desiredSpeed abt 55mph
                4.0f,       // minGap 
                1.0f,       // safeBrakePower
                2.5f,       // safeTimeHeadway
                18.0f,      // car length
                0.6f,       // politeness (slow, deliberate merges)
                1.0f,       // speedFactor (jittered per driver at spawn)
                1.5f        // bSafeMobil (conservative merges)
            };
        }
};

#endif