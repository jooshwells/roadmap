// Unit tests for the MOBIL lane-change model in PhysicsProcessor::MOBIL.
//
// MOBIL returns an incentive: positive means the change is worth making, and
// the sentinel -999 means it is unsafe at any incentive. These tests set up
// two-lane geometries with a known right answer and check the sign.

#include "sim_fixtures.h"
#include "test_harness.h"

namespace
{
    // The unsafe sentinel returned by MOBIL's safety criterion.
    constexpr float UnsafeChange = -999.0f;

    IDMParameters profileWithPoliteness(float politeness)
    {
        IDMParameters params = IDM_Profiles::getBasicDriverProfile();
        params.politeness = politeness;
        return params;
    }
}

SIM_TEST(MOBIL_Blocked_Lane_Incentive_Test,
         "A driver stuck behind a slow car wants to move into an empty adjacent lane")
{
    StraightRoadFixture fixture(2000.0, 2, 30.0);

    VehicleState* ego = fixture.spawn(25.0f, 500.0f, 0);
    fixture.spawn(5.0f, 530.0f, 0); // slow leader in the same lane
    fixture.refreshLeaders();

    const float incentive = fixture.physics->MOBIL(ego, 1);

    CHECK_FINITE(incentive, "the incentive must be a real number");
    CHECK_CMP(incentive, >, 0.0f,
              "an empty lane beside a slow leader must be an attractive lane change");
}

SIM_TEST(MOBIL_No_Incentive_When_Clear_Test,
         "A driver on an open road gains nothing by changing lanes, and loses by moving behind a slow car")
{
    StraightRoadFixture fixture(2000.0, 2, 30.0);

    VehicleState* ego = fixture.spawn(25.0f, 500.0f, 0);
    fixture.refreshLeaders();

    // Both lanes clear: the model must see the two lanes as equivalent.
    CHECK_NEAR(fixture.physics->MOBIL(ego, 1), 0.0f, 1e-4f,
               "with both lanes empty there is nothing to gain from a lane change");

    // Same geometry, but now the target lane has a slow car ahead.
    StraightRoadFixture blocked(2000.0, 2, 30.0);
    VehicleState* ego2 = blocked.spawn(25.0f, 500.0f, 0);
    blocked.spawn(5.0f, 530.0f, 1);
    blocked.refreshLeaders();

    CHECK_CMP(blocked.physics->MOBIL(ego2, 1), <, 0.0f,
              "moving in behind a slow car when the current lane is clear must be discouraged");
}

SIM_TEST(MOBIL_Unsafe_Cut_In_Vetoed_Test,
         "A lane change that would force the new follower into emergency braking is refused outright")
{
    StraightRoadFixture fixture(2000.0, 2, 30.0);

    VehicleState* ego = fixture.spawn(25.0f, 500.0f, 0);
    fixture.spawn(5.0f, 530.0f, 0);          // slow leader: strong reason to change
    fixture.spawn(30.0f, 496.0f, 1);         // fast car right alongside in the target lane
    fixture.refreshLeaders();

    const float incentive = fixture.physics->MOBIL(ego, 1);

    CHECK_NEAR(incentive, UnsafeChange, 1e-3f,
               "cutting in front of a fast car with no gap must be vetoed as unsafe");
}

SIM_TEST(MOBIL_Leader_Crash_Vetoed_Test,
         "Sliding in directly behind a car in the target lane is refused as a collision")
{
    StraightRoadFixture fixture(2000.0, 2, 30.0);

    VehicleState* ego = fixture.spawn(25.0f, 500.0f, 0);
    fixture.spawn(5.0f, 530.0f, 0);          // slow leader: strong reason to change
    fixture.spawn(25.0f, 505.0f, 1);         // target-lane car half a car length ahead
    fixture.refreshLeaders();

    const float incentive = fixture.physics->MOBIL(ego, 1);

    CHECK_NEAR(incentive, UnsafeChange, 1e-3f,
               "merging into a space smaller than half the minimum gap must be vetoed");
}

SIM_TEST(MOBIL_Politeness_Test,
         "A polite driver values a lane change less than a selfish one when it inconveniences the new follower")
{
    // Identical geometry twice, differing only in the ego's politeness factor.
    // The new follower is far enough back that the change is legal (it brakes
    // less than b_safe), but it is still made worse off -- exactly the case
    // MOBIL's politeness term weighs.
    auto incentiveFor = [](float politeness) {
        StraightRoadFixture fixture(2000.0, 2, 30.0);
        VehicleState* ego = fixture.spawn(25.0f, 500.0f, 0, profileWithPoliteness(politeness));
        fixture.spawn(5.0f, 530.0f, 0);   // slow leader in the current lane
        fixture.spawn(25.0f, 460.0f, 1);  // new follower, 35.5 m back
        fixture.refreshLeaders();
        return fixture.physics->MOBIL(ego, 1);
    };

    const float selfish = incentiveFor(0.0f);
    const float polite = incentiveFor(0.8f);

    CHECK_CMP(selfish, >, UnsafeChange + 1.0f,
              "test setup: the change must be safe, so politeness is what decides it");
    CHECK_CMP(polite, <, selfish,
              "a polite driver must discount a lane change that hurts the new follower");
}
