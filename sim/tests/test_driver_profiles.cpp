// Unit tests for the driver personality layer: IDM_Profiles sampling and the
// per-driver behaviours VehicleState wraps around the raw IDM output
// (desired-speed scaling, standing-start launch boost, reaction delay).

#include "sim_fixtures.h"
#include "test_harness.h"

#include <random>
#include <string>

namespace
{
    // Ranges documented in idm_profiles.h. Kept here as the test's own
    // statement of intent, so a silent widening of a range fails the test
    // instead of quietly redefining what "aggressive" means.
    struct ProfileBounds
    {
        float maxAccelLo,  maxAccelHi;
        float minGapLo,    minGapHi;
        float brakeLo,     brakeHi;
        float headwayLo,   headwayHi;
        float politeLo,    politeHi;
        float speedFacLo,  speedFacHi;
        float reactionLo,  reactionHi;
    };

    const ProfileBounds AggressiveBounds{2.2f, 3.0f, 1.0f, 1.6f, 2.5f, 3.5f,
                                         0.7f, 1.1f, 0.0f, 0.15f, 1.08f, 1.22f, 0.25f, 0.6f};
    const ProfileBounds AverageBounds{1.3f, 1.8f, 1.8f, 2.6f, 1.5f, 2.1f,
                                      1.2f, 1.7f, 0.2f, 0.5f, 0.95f, 1.08f, 0.6f, 1.2f};
    const ProfileBounds CautiousBounds{0.8f, 1.2f, 2.5f, 3.5f, 1.0f, 1.5f,
                                       1.8f, 2.5f, 0.5f, 0.8f, 0.82f, 0.95f, 1.2f, 2.0f};

    void checkInBounds(TestContext& ctx, const IDMParameters& p, const ProfileBounds& b,
                       const std::string& label)
    {
        CHECK_CMP(p.maxAccel, >=, b.maxAccelLo, label + ": maxAccel below its archetype range");
        CHECK_CMP(p.maxAccel, <=, b.maxAccelHi, label + ": maxAccel above its archetype range");
        CHECK_CMP(p.minGap, >=, b.minGapLo, label + ": minGap below its archetype range");
        CHECK_CMP(p.minGap, <=, b.minGapHi, label + ": minGap above its archetype range");
        CHECK_CMP(p.safeBrakePower, >=, b.brakeLo, label + ": safeBrakePower below its archetype range");
        CHECK_CMP(p.safeBrakePower, <=, b.brakeHi, label + ": safeBrakePower above its archetype range");
        CHECK_CMP(p.safeTimeHeadway, >=, b.headwayLo, label + ": safeTimeHeadway below its archetype range");
        CHECK_CMP(p.safeTimeHeadway, <=, b.headwayHi, label + ": safeTimeHeadway above its archetype range");
        CHECK_CMP(p.politeness, >=, b.politeLo, label + ": politeness below its archetype range");
        CHECK_CMP(p.politeness, <=, b.politeHi, label + ": politeness above its archetype range");
        CHECK_CMP(p.speedFactor, >=, b.speedFacLo, label + ": speedFactor below its archetype range");
        CHECK_CMP(p.speedFactor, <=, b.speedFacHi, label + ": speedFactor above its archetype range");
        CHECK_CMP(p.reactionTime, >=, b.reactionLo, label + ": reactionTime below its archetype range");
        CHECK_CMP(p.reactionTime, <=, b.reactionHi, label + ": reactionTime above its archetype range");
    }
}

SIM_TEST(Driver_Profile_Range_Test,
         "Every sampled driver profile stays inside its archetype's documented parameter ranges")
{
    std::mt19937 rng(1234);

    for (int i = 0; i < 2000; i++)
    {
        checkInBounds(ctx, IDM_Profiles::sampleProfile(DriverType::Aggressive, rng),
                      AggressiveBounds, "Aggressive");
        checkInBounds(ctx, IDM_Profiles::sampleProfile(DriverType::Average, rng),
                      AverageBounds, "Average");
        checkInBounds(ctx, IDM_Profiles::sampleProfile(DriverType::Cautious, rng),
                      CautiousBounds, "Cautious");
    }
}

SIM_TEST(Driver_Profile_Ordering_Test,
         "Aggressive drivers out-accelerate, tailgate, and out-run cautious ones in every sample")
{
    std::mt19937 rng(99);

    for (int i = 0; i < 2000; i++)
    {
        const IDMParameters aggressive = IDM_Profiles::sampleProfile(DriverType::Aggressive, rng);
        const IDMParameters average = IDM_Profiles::sampleProfile(DriverType::Average, rng);
        const IDMParameters cautious = IDM_Profiles::sampleProfile(DriverType::Cautious, rng);

        CHECK_CMP(aggressive.maxAccel, >, average.maxAccel, "aggressive drivers must accelerate hardest");
        CHECK_CMP(average.maxAccel, >, cautious.maxAccel, "cautious drivers must accelerate softest");

        CHECK_CMP(aggressive.safeTimeHeadway, <, average.safeTimeHeadway,
                  "aggressive drivers must follow closest");
        CHECK_CMP(average.safeTimeHeadway, <, cautious.safeTimeHeadway,
                  "cautious drivers must leave the longest headway");

        CHECK_CMP(aggressive.speedFactor, >, 1.0f, "aggressive drivers must run over the limit");
        CHECK_CMP(cautious.speedFactor, <, 1.0f, "cautious drivers must run under the limit");

        CHECK_CMP(aggressive.reactionTime, <, cautious.reactionTime,
                  "aggressive drivers must react fastest off the line");
    }
}

SIM_TEST(Driver_Population_Mix_Test,
         "The spawn population splits roughly 20% cautious / 60% average / 20% aggressive")
{
    std::mt19937 rng(2024);
    const int samples = 100000;

    int cautious = 0, average = 0, aggressive = 0;
    for (int i = 0; i < samples; i++)
    {
        switch (IDM_Profiles::rollDriverType(rng))
        {
            case DriverType::Cautious:   cautious++;   break;
            case DriverType::Average:    average++;    break;
            case DriverType::Aggressive: aggressive++; break;
        }
    }

    // 1% tolerance: at 100k samples the sampling error is well under this, so
    // a failure means the weights moved, not that the dice were unlucky.
    CHECK_NEAR(static_cast<float>(cautious) / samples, 0.20f, 0.01f,
               "cautious drivers must be ~20% of the population");
    CHECK_NEAR(static_cast<float>(average) / samples, 0.60f, 0.01f,
               "average drivers must be ~60% of the population");
    CHECK_NEAR(static_cast<float>(aggressive) / samples, 0.20f, 0.01f,
               "aggressive drivers must be ~20% of the population");
}

SIM_TEST(Driver_Speed_Factor_Test,
         "A driver's desired speed is the road limit scaled by its personal speed factor")
{
    StraightRoadFixture fixture;
    std::mt19937 rng(7);

    const IDMParameters aggressive = IDM_Profiles::sampleProfile(DriverType::Aggressive, rng);
    const IDMParameters cautious = IDM_Profiles::sampleProfile(DriverType::Cautious, rng);

    VehicleState* fast = fixture.spawn(0.0f, 0.0f, 0, aggressive);
    VehicleState* slow = fixture.spawn(0.0f, 0.0f, 0, cautious);

    const float speedLimit = 25.0f;
    fast->setDesiredSpeed(speedLimit);
    slow->setDesiredSpeed(speedLimit);

    CHECK_NEAR(fast->getDesiredSpeed(), speedLimit * aggressive.speedFactor, 1e-3f,
               "the applied target must be limit * speedFactor");
    CHECK_CMP(fast->getDesiredSpeed(), >, speedLimit, "an aggressive driver must target over the limit");
    CHECK_CMP(slow->getDesiredSpeed(), <, speedLimit, "a cautious driver must target under the limit");
}

SIM_TEST(Vehicle_Launch_Boost_Test,
         "A car that has been stopped launches harder than plain IDM, and the boost fades with speed")
{
    StraightRoadFixture fixture;

    IDMParameters params = IDM_Profiles::getBasicDriverProfile();
    params.launchBoostFactor = 2.0f;
    VehicleState* ego = fixture.spawn(0.0f, 100.0f, 0, params);

    CHECK_NEAR(ego->getLaunchBoost(), 1.0f, 1e-4f,
               "a freshly spawned car has not stopped yet, so no boost is armed");

    // Sitting still for longer than the arming window (0.5 s) arms the boost.
    for (int i = 0; i < 20; i++) ego->updateWaitTime(0.1f);

    CHECK_NEAR(ego->getLaunchBoost(), params.launchBoostFactor, 1e-4f,
               "at a standstill the full launch multiplier must apply");
    CHECK_NEAR(fixture.idm(ego, nullptr), ego->getMaxAccel() * params.launchBoostFactor, 1e-3f,
               "the standing-start acceleration must be maxAccel scaled by the launch boost");

    // Past the fade-out speed (9 m/s) the driver is back on plain IDM.
    ego->accelerate(10.0f);
    CHECK_NEAR(ego->getLaunchBoost(), 1.0f, 1e-4f,
               "above the fade-out speed the launch boost must be gone");
}

SIM_TEST(Vehicle_Reaction_Delay_Test,
         "A stopped driver holds still for its reaction time before pulling away")
{
    StraightRoadFixture fixture;

    IDMParameters params = IDM_Profiles::getBasicDriverProfile();
    params.reactionTime = 1.0f;
    VehicleState* ego = fixture.spawn(0.0f, 100.0f, 0, params);

    // Arm the stopped state the same way the physics loop does.
    for (int i = 0; i < 20; i++) ego->updateWaitTime(0.1f);

    const float dt = 0.1f;
    const float requested = 2.0f;

    // Nine ticks of 0.1 s is still short of the 1.0 s reaction time.
    for (int i = 0; i < 9; i++)
    {
        CHECK_NEAR(ego->applyReactionDelay(requested, dt), 0.0f, 1e-6f,
                   "the driver must not move before its reaction time has elapsed");
    }

    CHECK_NEAR(ego->applyReactionDelay(requested, dt), requested, 1e-6f,
               "once the reaction time has elapsed the requested acceleration must pass through");

    // A driver who never stopped reacts through its headway instead, with no
    // launch delay at all.
    VehicleState* rolling = fixture.spawn(15.0f, 100.0f, 0, params);
    CHECK_NEAR(rolling->applyReactionDelay(requested, dt), requested, 1e-6f,
               "a moving driver must not be gated by the standing-start reaction delay");
}
