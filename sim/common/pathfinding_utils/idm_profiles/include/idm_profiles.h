#ifndef IDM_PROFILES_H
#define IDM_PROFILES_H

#include "vehicle_state.h"
#include <random>

// Driver personality archetypes. Each spawn rolls an archetype from a weighted
// distribution, then samples every IDM/MOBIL parameter uniformly from that
// archetype's realistic range, so no two drivers are exactly alike but the
// population still splits into recognizable aggressive / average / cautious
// behavior. Ranges are anchored on the IDM literature values (Treiber:
// a 1.0-1.5 m/s^2, b 1.5-2.0 m/s^2, T 1.0-2.0 s, s0 2 m) and stretched
// outward for the two personality extremes.
enum class DriverType
{
    Cautious,   // slow, long gaps, sluggish off the line
    Average,    // textbook IDM driver
    Aggressive  // fast, tailgates, jumps the green
};

class IDM_Profiles
{
    public:
        // Population mix: ~20% cautious, ~60% average, ~20% aggressive.
        static DriverType rollDriverType(std::mt19937& rng)
        {
            std::uniform_real_distribution<float> roll(0.0f, 1.0f);
            const float r = roll(rng);
            if (r < 0.20f) return DriverType::Cautious;
            if (r < 0.80f) return DriverType::Average;
            return DriverType::Aggressive;
        }

        // One concrete driver drawn from the archetype's parameter ranges.
        // desiredSpeed is a free-road cap; the live target is the road limit
        // scaled by speedFactor (aggressive drivers run over the limit,
        // cautious ones under it).
        static IDMParameters sampleProfile(DriverType type, std::mt19937& rng)
        {
            auto range = [&rng](float lo, float hi) {
                return std::uniform_real_distribution<float>(lo, hi)(rng);
            };

            IDMParameters p;
            p.accelExp = 4.0f;
            p.length   = 4.5f;

            switch (type)
            {
                case DriverType::Aggressive:
                    p.profileName       = "Aggressive";
                    p.maxAccel          = range(2.2f, 3.0f);   // hard on the throttle
                    p.desiredSpeed      = 40.0f;               // ~90 mph free-road cap
                    p.minGap            = range(1.0f, 1.6f);   // tailgates
                    p.safeBrakePower    = range(2.5f, 3.5f);   // comfortable braking late & hard
                    p.safeTimeHeadway   = range(0.7f, 1.1f);   // short following distance
                    p.politeness        = range(0.0f, 0.15f);  // darts across lanes
                    p.speedFactor       = range(1.08f, 1.22f); // 8-22% over the limit
                    p.reactionTime      = range(0.25f, 0.6f);  // jumps the green
                    p.launchBoostFactor = range(2.2f, 2.7f);
                    break;

                case DriverType::Cautious:
                    p.profileName       = "Cautious";
                    p.maxAccel          = range(0.8f, 1.2f);   // eases away
                    p.desiredSpeed      = 27.0f;               // ~60 mph free-road cap
                    p.minGap            = range(2.5f, 3.5f);   // leaves room
                    p.safeBrakePower    = range(1.0f, 1.5f);   // brakes early & gently
                    p.safeTimeHeadway   = range(1.8f, 2.5f);   // long following distance
                    p.politeness        = range(0.5f, 0.8f);   // slow, deliberate merges
                    p.speedFactor       = range(0.82f, 0.95f); // 5-18% under the limit
                    p.reactionTime      = range(1.2f, 2.0f);   // slow off the line
                    p.launchBoostFactor = range(1.3f, 1.7f);
                    break;

                case DriverType::Average:
                default:
                    p.profileName       = "Average";
                    p.maxAccel          = range(1.3f, 1.8f);
                    p.desiredSpeed      = 32.0f;               // ~70 mph free-road cap
                    p.minGap            = range(1.8f, 2.6f);
                    p.safeBrakePower    = range(1.5f, 2.1f);
                    p.safeTimeHeadway   = range(1.2f, 1.7f);
                    p.politeness        = range(0.2f, 0.5f);
                    p.speedFactor       = range(0.95f, 1.08f); // hovers around the limit
                    p.reactionTime      = range(0.6f, 1.2f);
                    p.launchBoostFactor = range(1.8f, 2.2f);
                    break;
            }
            return p;
        }

        // Fixed archetype centers, kept for callers that want a deterministic
        // driver (tests, headless experiments).
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
                0.3f        // politeness (p)
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
                "Aggressive"
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
                "Semi truck"
            };
        }
};

#endif
