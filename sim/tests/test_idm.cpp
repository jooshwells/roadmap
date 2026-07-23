// Unit tests for the Intelligent Driver Model in PhysicsProcessor::IDM.
//
// Every test drives the real function on a real (tiny) network rather than a
// re-implementation, so a change to the car-following math shows up here.

#include "sim_fixtures.h"
#include "test_harness.h"

#include <algorithm>
#include <limits>

// ---------------------------------------------------------------------------
// Free-road behaviour
// ---------------------------------------------------------------------------

SIM_TEST(IDM_Steady_State_Test,
         "A vehicle with no leader, already at its desired speed, holds zero acceleration")
{
    StraightRoadFixture fixture;
    const IDMParameters params = IDM_Profiles::getBasicDriverProfile();

    VehicleState* ego = fixture.spawn(params.desiredSpeed, 100.0f);

    // Guard the premise: the driver profile's speedFactor scales the target,
    // so the spawn speed is only "v0" when the two agree.
    CHECK_NEAR(ego->getDesiredSpeed(), ego->getSpeed(), 1e-4f,
               "test setup: the vehicle must start exactly at its desired speed");

    const float accel = fixture.idm(ego, nullptr);

    CHECK_FINITE(accel, "acceleration must be a real number");
    CHECK_NEAR(accel, 0.0f, 1e-3f,
               "an unobstructed vehicle cruising at v0 must neither accelerate nor brake");
}

SIM_TEST(IDM_Start_From_Rest_Test,
         "A stopped vehicle with no leader pulls away at exactly its maximum acceleration")
{
    StraightRoadFixture fixture;
    VehicleState* ego = fixture.spawn(0.0f, 100.0f);

    // At v = 0 the free-road term is a * (1 - 0^delta) = a, and there is no
    // interaction term without a leader.
    const float accel = fixture.idm(ego, nullptr);

    CHECK_NEAR(accel, ego->getMaxAccel(), 1e-4f,
               "from a standstill on an empty road the driver should use full acceleration");
}

SIM_TEST(IDM_Over_Speed_Test,
         "A vehicle above its desired speed with no leader decelerates back toward v0")
{
    StraightRoadFixture fixture;
    VehicleState* ego = fixture.spawn(0.0f, 100.0f);

    // 20% over the target: the free-road term is a * (1 - 1.2^delta).
    const float overSpeed = ego->getDesiredSpeed() * 1.2f;
    ego->accelerate(overSpeed);

    const float accel = fixture.idm(ego, nullptr);
    const float expected = ego->getMaxAccel() * (1.0f - std::pow(1.2f, ego->getAccelExp()));

    CHECK_CMP(accel, <, 0.0f, "a vehicle over its desired speed must decelerate");
    CHECK_NEAR(accel, expected, 1e-3f,
               "free-road deceleration must match a * (1 - (v/v0)^delta)");
}

SIM_TEST(IDM_Ideal_Speed_Test,
         "A vehicle alone on the road accelerates monotonically and asymptotes to v0")
{
    StraightRoadFixture fixture;
    VehicleState* ego = fixture.spawn(0.0f, 0.0f);

    const float v0 = ego->getDesiredSpeed();
    const float dt = 0.1f;
    const int steps = 3000; // 300 s of sim time

    float previousSpeed = ego->getSpeed();
    bool monotonic = true;
    bool overshot = false;

    for (int i = 0; i < steps; i++)
    {
        const float accel = fixture.idm(ego, nullptr);
        StraightRoadFixture::integrate(ego, accel, dt);

        if (ego->getSpeed() < previousSpeed - 1e-4f) monotonic = false;
        if (ego->getSpeed() > v0 + 1e-3f) overshot = true;
        previousSpeed = ego->getSpeed();
    }

    CHECK(monotonic, "speed must rise monotonically on a free road (no oscillation)");
    CHECK(!overshot, "a free-road vehicle must never exceed its desired speed");
    CHECK_NEAR(ego->getSpeed(), v0, v0 * 0.005f,
               "after 300 s alone on the road the speed must have converged to v0");
    CHECK_NEAR(fixture.idm(ego, nullptr), 0.0f, 1e-2f,
               "at the asymptote the requested acceleration must have decayed to zero");
}

// ---------------------------------------------------------------------------
// Interaction term / braking
// ---------------------------------------------------------------------------

SIM_TEST(IDM_Emrgncy_Brake_Test,
         "A gap far smaller than the safe headway s* produces maximum deceleration")
{
    StraightRoadFixture fixture;

    VehicleState* ego = fixture.spawn(25.0f, 100.0f);
    VehicleState* leader = fixture.spawn(0.0f, 105.0f); // stationary, 0.5 m of clear road

    const float gap = fixture.gap(ego, leader);
    const float safeHeadway = idmDesiredGap(ego, leader->getSpeed());

    // Guard the premise: this is only an emergency if s << s*.
    CHECK_CMP(gap, <, safeHeadway * 0.1f,
              "test setup: the gap must be far below the safe headway s*");

    const float accel = fixture.idm(ego, leader);

    CHECK_FINITE(accel, "emergency braking must not produce NaN or infinity");
    CHECK_NEAR(accel, MaxPhysicalDecel, 1e-3f,
               "closing on a stopped car with no room must saturate the braking clamp");
}

SIM_TEST(IDM_Decel_Clamp_Test,
         "IDM never returns a deceleration beyond the physical clamp, or a non-finite value")
{
    StraightRoadFixture fixture;

    // Adversarial states: overlapping bumpers, huge closing speeds, and a
    // leader behind the ego (which calculateTrueGap reports as a zero gap).
    const float egoSpeeds[]    = {0.0f, 5.0f, 30.0f, 80.0f};
    const float leaderOffsets[] = {-5.0f, 0.0f, 0.01f, 1.0f, 5.0f};

    for (float speed : egoSpeeds)
    {
        for (float offset : leaderOffsets)
        {
            VehicleState* ego = fixture.spawn(speed, 500.0f);
            VehicleState* leader = fixture.spawn(0.0f, 500.0f + offset);

            const float accel = fixture.idm(ego, leader);

            CHECK_FINITE(accel, "acceleration must stay finite for every gap/speed combination");
            CHECK_CMP(accel, >=, MaxPhysicalDecel - 1e-4f,
                      "deceleration must never exceed the -10 m/s^2 physical clamp");
            CHECK_CMP(accel, <=, ego->getMaxAccel() + 1e-4f,
                      "acceleration must never exceed the driver's maximum");
        }
    }
}

SIM_TEST(IDM_Gap_Monotonicity_Test,
         "With the leader's speed fixed, acceleration rises with the gap and tends to the free-road value")
{
    StraightRoadFixture fixture;
    VehicleState* ego = fixture.spawn(20.0f, 0.0f);

    float previousAccel = -std::numeric_limits<float>::max();
    bool monotonic = true;

    for (float gap = 2.0f; gap <= 300.0f; gap += 2.0f)
    {
        // Leader at the same speed, so only the gap varies between samples.
        VehicleState* leader = fixture.spawn(20.0f, ego->getPos() + gap + 4.5f);
        const float accel = fixture.idm(ego, leader);

        if (accel < previousAccel - 1e-5f) monotonic = false;
        previousAccel = accel;
    }

    const float freeRoadAccel = fixture.idm(ego, nullptr);

    CHECK(monotonic, "more room ahead must never make a driver brake harder");
    CHECK_CMP(previousAccel, <, freeRoadAccel,
              "any leader still restrains the driver slightly compared to an empty road");
    CHECK_NEAR(previousAccel, freeRoadAccel, 0.05f,
               "at 300 m the leader's influence must have nearly vanished");
}

SIM_TEST(IDM_Approach_Rate_Test,
         "At a fixed gap, a faster closing speed produces harder braking")
{
    StraightRoadFixture fixture;

    const float gap = 30.0f;
    const float leaderSpeeds[] = {25.0f, 20.0f, 15.0f, 10.0f, 5.0f, 0.0f};

    float previousAccel = std::numeric_limits<float>::max();
    bool monotonic = true;

    for (float leaderSpeed : leaderSpeeds)
    {
        VehicleState* ego = fixture.spawn(25.0f, 200.0f);
        VehicleState* leader = fixture.spawn(leaderSpeed, 200.0f + gap + 4.5f);

        const float accel = fixture.idm(ego, leader);
        if (accel > previousAccel + 1e-5f) monotonic = false;
        previousAccel = accel;
    }

    CHECK(monotonic, "closing on the leader faster must always mean more braking, never less");
    CHECK_CMP(previousAccel, <, 0.0f,
              "closing at 25 m/s on a stopped car 30 m ahead must be a braking request");
}

// ---------------------------------------------------------------------------
// Trajectories: what the model does over time
// ---------------------------------------------------------------------------

SIM_TEST(IDM_Smooth_Approach_Test,
         "A vehicle catching a slower constant-speed leader sheds speed smoothly, never touching the emergency clamp")
{
    StraightRoadFixture fixture;

    const float leaderSpeed = 15.0f;
    VehicleState* ego = fixture.spawn(30.0f, 0.0f);
    VehicleState* leader = fixture.spawn(leaderSpeed, 250.0f); // caught from well back

    const float dt = 0.05f;
    const int steps = 6000; // 300 s

    float hardestBraking = 0.0f;
    float slowestSpeed = ego->getSpeed();
    float minGapSeen = std::numeric_limits<float>::max();

    for (int i = 0; i < steps; i++)
    {
        const float accel = fixture.idm(ego, leader);
        hardestBraking = std::min(hardestBraking, accel);

        StraightRoadFixture::integrate(ego, accel, dt);
        StraightRoadFixture::integrate(leader, 0.0f, dt);

        slowestSpeed = std::min(slowestSpeed, ego->getSpeed());
        minGapSeen = std::min(minGapSeen, fixture.gap(ego, leader));
    }

    // The whole point of the interaction term is that a driver who sees the
    // slower car early brakes at comfortable levels. Anything approaching the
    // -10 m/s^2 clamp here would mean the model reacted far too late.
    CHECK_CMP(hardestBraking, >, MaxPhysicalDecel + 5.0f,
              "a routine approach must never come near the emergency braking clamp");
    CHECK_CMP(hardestBraking, >, -2.0f * ego->getSafeBrakePower(),
              "braking must stay within roughly the driver's comfortable deceleration b");

    // Smoothness: the follower must not overshoot into the leader's speed and
    // have to accelerate back out of a hole it dug itself.
    CHECK_CMP(slowestSpeed, >, leaderSpeed - 0.5f,
              "the follower must settle onto the leader's speed without undershooting it");

    CHECK_CMP(minGapSeen, >, 0.0f, "the approach must not end in a collision");
    CHECK_NEAR(ego->getSpeed(), leaderSpeed, 0.05f,
               "the follower must end up matching the leader's speed");
}

SIM_TEST(IDM_Equilibrium_Follow_Test,
         "A follower behind a constant-speed leader settles at the leader's speed and the analytic equilibrium gap")
{
    StraightRoadFixture fixture;

    const float cruiseSpeed = 20.0f;
    VehicleState* ego = fixture.spawn(cruiseSpeed, 0.0f);
    VehicleState* leader = fixture.spawn(cruiseSpeed, 100.0f);

    const float dt = 0.05f;
    const int steps = 8000; // 400 s

    for (int i = 0; i < steps; i++)
    {
        const float accel = fixture.idm(ego, leader);
        StraightRoadFixture::integrate(ego, accel, dt);
        StraightRoadFixture::integrate(leader, 0.0f, dt); // leader holds its cruise
    }

    const float expectedGap = idmEquilibriumGap(ego, cruiseSpeed);
    const float safeHeadway = idmDesiredGap(ego, cruiseSpeed); // s* = s0 + v*T at Δv = 0

    CHECK_NEAR(ego->getSpeed(), cruiseSpeed, 0.05f,
               "a settled follower must match its leader's speed");
    CHECK_NEAR(fixture.gap(ego, leader), expectedGap, 0.5f,
               "the settled gap must match the closed-form IDM equilibrium distance");
    CHECK_NEAR(fixture.idm(ego, leader), 0.0f, 0.02f,
               "at equilibrium the free-road and interaction terms must cancel");

    // The settled gap is NOT s* -- it is s* / sqrt(1 - (v/v0)^delta), which is
    // strictly larger below v0. A driver sitting at exactly s* would still be
    // fighting its own free-road term, so a suite that asserted "gap == s*"
    // would pass only on an IDM with the free-road term missing.
    CHECK_CMP(expectedGap, >, safeHeadway,
              "below v0 the equilibrium gap must exceed the safe headway s*");
    CHECK_CMP(fixture.gap(ego, leader), >, safeHeadway + 0.5f,
              "the settled follower must sit measurably further back than s*");
}

SIM_TEST(IDM_Stopped_Leader_Test,
         "A vehicle approaching a stopped car stops behind it at the minimum jam distance s0, without collision")
{
    StraightRoadFixture fixture;

    VehicleState* ego = fixture.spawn(25.0f, 0.0f);
    VehicleState* leader = fixture.spawn(0.0f, 500.0f); // stalled car, never moves

    const float dt = 0.05f;
    const int steps = 4000; // 200 s

    float minGapSeen = std::numeric_limits<float>::max();
    for (int i = 0; i < steps; i++)
    {
        const float accel = fixture.idm(ego, leader);
        StraightRoadFixture::integrate(ego, accel, dt);
        minGapSeen = std::min(minGapSeen, fixture.gap(ego, leader));
    }

    CHECK_CMP(minGapSeen, >, 0.0f, "the follower must never reach the stopped car's bumper");
    CHECK_NEAR(ego->getSpeed(), 0.0f, 0.05f, "the follower must come to a complete stop");
    CHECK_NEAR(fixture.gap(ego, leader), ego->getMinGap(), 0.2f,
               "a queued vehicle must rest exactly s0 behind the car in front");
}

SIM_TEST(IDM_Emergency_Stop_No_Collision_Test,
         "A follower at the equilibrium gap survives its leader braking hard to a standstill")
{
    StraightRoadFixture fixture;

    const float cruiseSpeed = 25.0f;
    VehicleState* ego = fixture.spawn(cruiseSpeed, 0.0f);

    VehicleState* leader = fixture.spawn(cruiseSpeed, 0.0f);
    leader->setPos(ego->getPos() + idmEquilibriumGap(ego, cruiseSpeed) + leader->getLength());

    const float dt = 0.05f;
    const int steps = 2400; // 120 s
    const float leaderBrake = -6.0f; // hard, but short of the -10 clamp

    float minGapSeen = std::numeric_limits<float>::max();
    float hardestBraking = 0.0f;

    for (int i = 0; i < steps; i++)
    {
        // The leader cruises for 10 s, then stands on the brakes.
        const float leaderAccel = (i * dt < 10.0f) ? 0.0f : leaderBrake;

        const float accel = fixture.idm(ego, leader);
        hardestBraking = std::min(hardestBraking, accel);

        StraightRoadFixture::integrate(ego, accel, dt);
        StraightRoadFixture::integrate(leader, leaderAccel, dt);

        minGapSeen = std::min(minGapSeen, fixture.gap(ego, leader));
    }

    CHECK_CMP(minGapSeen, >, 0.0f, "the follower must not run into a hard-braking leader");
    CHECK_CMP(hardestBraking, >=, MaxPhysicalDecel - 1e-4f,
              "braking must stay within the physical clamp even in an emergency");
    CHECK_NEAR(leader->getSpeed(), 0.0f, 1e-3f, "test setup: the leader must end up stopped");
    CHECK_NEAR(ego->getSpeed(), 0.0f, 0.05f, "the follower must end up stopped behind it");
    CHECK_NEAR(fixture.gap(ego, leader), ego->getMinGap(), 0.3f,
               "after the emergency the follower must settle at the minimum gap");
}

// ---------------------------------------------------------------------------
// Guard clauses
// ---------------------------------------------------------------------------

SIM_TEST(IDM_Dead_Leader_Ignored_Test,
         "A leader already marked for deletion is treated as an empty road, not as an obstacle")
{
    StraightRoadFixture fixture;

    VehicleState* ego = fixture.spawn(25.0f, 100.0f);
    VehicleState* leader = fixture.spawn(0.0f, 102.0f);

    const float blockedAccel = fixture.idm(ego, leader);
    leader->isMarkedForDeletion = true;
    const float clearedAccel = fixture.idm(ego, leader);

    CHECK_NEAR(blockedAccel, MaxPhysicalDecel, 1e-3f,
               "test setup: while alive the leader must force emergency braking");
    CHECK_NEAR(clearedAccel, fixture.idm(ego, nullptr), 1e-4f,
               "a despawned leader must not brake traffic behind it");
}

SIM_TEST(IDM_Invalid_Vehicle_Test,
         "IDM returns zero acceleration for a null, deleted, or unbound vehicle instead of crashing")
{
    StraightRoadFixture fixture;

    CHECK_NEAR(fixture.idm(nullptr, nullptr), 0.0f, 1e-6f,
               "a null vehicle must be handled, not dereferenced");

    VehicleState* deleted = fixture.spawn(20.0f, 100.0f);
    deleted->isMarkedForDeletion = true;
    CHECK_NEAR(fixture.idm(deleted, nullptr), 0.0f, 1e-6f,
               "a vehicle marked for deletion must request no acceleration");

    // A car whose road was deleted under it has no edge until it is despawned.
    VehicleState* unbound = fixture.spawn(20.0f, 100.0f);
    unbound->setCurrentEdge(nullptr);
    CHECK_NEAR(fixture.idm(unbound, nullptr), 0.0f, 1e-6f,
               "a vehicle with no current edge must request no acceleration");
}
