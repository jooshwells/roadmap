// Unit tests for intersection control: traffic-light phases, the yellow
// dilemma zone, yield gap acceptance, and all-way stop right-of-way.
//
// These drive the real PhysicsProcessor::update() loop over a real junction.
// The control logic itself (canVehicleEnter / controlGrantsEntry) is private,
// but its effect is public and is exactly what a driver experiences: a car
// denied entry is handed a zero-length "ghost" leader parked at the stop
// line, which its IDM brakes for like any other obstacle. IntersectionFixture
// ::heldByControl reads that.

#include "intersection_fixtures.h"
#include "sim_fixtures.h" // MaxPhysicalDecel
#include "test_harness.h"

#include <algorithm>
#include <limits>
#include <vector>

namespace
{
    // Routes straight across the junction, arm to opposite arm.
    const std::vector<uint64_t> EastToWest = {IntersectionFixture::East,
                                              IntersectionFixture::Centre,
                                              IntersectionFixture::West};
    const std::vector<uint64_t> WestToEast = {IntersectionFixture::West,
                                              IntersectionFixture::Centre,
                                              IntersectionFixture::East};
    const std::vector<uint64_t> NorthToSouth = {IntersectionFixture::North,
                                                IntersectionFixture::Centre,
                                                IntersectionFixture::South};
    const std::vector<uint64_t> SouthToNorth = {IntersectionFixture::South,
                                                IntersectionFixture::Centre,
                                                IntersectionFixture::North};
}

SIM_TEST(Int_Red_Light_Approach_Test,
         "A vehicle approaching a red light brakes smoothly and stops one minimum gap short of the stop line")
{
    IntersectionFixture fixture("signal");

    // The east approach is axis 1, and the phase ring starts on an axis-0
    // green, so this car meets a red. A parked car on the north approach is
    // required to keep it that way: the signal is actuated, so an approach
    // demanding a green with no competing traffic is simply served, and a
    // lone car would sail through without ever seeing a red.
    VehicleState* demand = fixture.spawn(NorthToSouth, 0.0f, 0.0f, 1);
    VehicleState* ego = fixture.spawn(EastToWest, 15.0f, 250.0f, 1);
    CHECK(ego != nullptr && demand != nullptr,
          "test setup: both vehicles must bind to their approaches");
    if (ego == nullptr || demand == nullptr) return;

    const float dt = 0.1f;
    fixture.step(dt);
    const float northLine = fixture.stopLineFor(demand);

    float hardestBraking = 0.0f;
    float restDistance = 0.0f;
    bool everHeld = false;
    bool rested = false;
    bool crossedLine = false;

    for (int i = 0; i < 400; i++) // up to 40 s
    {
        IntersectionFixture::freeze(demand, northLine - 20.0f, 0.0f, 1);
        fixture.step(dt);
        if (!fixture.isAlive(ego)) break;

        hardestBraking = std::min(hardestBraking, ego->getAcceleration());
        const bool held = fixture.heldByControl(ego);
        if (held) everHeld = true;

        const float distance = fixture.distanceToStopLine(ego);
        if (distance < 0.0f) crossedLine = true;

        // Sample the instant it settles, rather than at a fixed step: the
        // light legitimately turns green later, and the car legitimately
        // drives off when it does.
        if (held && ego->getSpeed() < 0.05f) { rested = true; restDistance = distance; break; }
    }

    CHECK(everHeld, "test setup: the light must actually have denied this approach");
    CHECK(rested, "the vehicle must come to a complete stop at the red");
    CHECK(!crossedLine, "a vehicle must never roll past the stop line on a red");
    CHECK_NEAR(restDistance, ego->getMinGap(), 0.35f,
               "a stopped vehicle must rest exactly one minimum gap s0 short of the stop line");
    CHECK_CMP(hardestBraking, >, MaxPhysicalDecel + 5.0f,
              "stopping for a red seen from 130 m out must not need emergency braking");
}

SIM_TEST(Int_Green_Light_Resume_Test,
         "A vehicle queued at a red holds position until the light releases it, then accelerates away toward the limit")
{
    IntersectionFixture fixture("signal");

    // As in the red-light test, the parked north car is what makes the east
    // approach face a red at all (the signal is actuated). It stays frozen
    // for the whole test so the ring keeps cycling normally.
    VehicleState* demand = fixture.spawn(NorthToSouth, 0.0f, 0.0f, 1);
    VehicleState* ego = fixture.spawn(EastToWest, 15.0f, 250.0f, 1);
    CHECK(ego != nullptr && demand != nullptr,
          "test setup: both vehicles must bind to their approaches");
    if (ego == nullptr || demand == nullptr) return;

    const float dt = 0.1f;
    fixture.step(dt);
    const float northLine = fixture.stopLineFor(demand);
    auto holdDemand = [&]() { IntersectionFixture::freeze(demand, northLine - 20.0f, 0.0f, 1); };

    // Phase 1: let it settle at the stop line.
    bool settled = false;
    for (int i = 0; i < 400; i++)
    {
        holdDemand();
        fixture.step(dt);
        if (!fixture.isAlive(ego)) break;
        if (fixture.heldByControl(ego) && ego->getSpeed() < 0.05f) { settled = true; break; }
    }
    CHECK(settled, "test setup: the vehicle must come to rest at the red");
    if (!settled || !fixture.isAlive(ego)) return;

    const float restPos = ego->getPos();

    // Phase 2: wait for the release, watching for any creep past the line.
    int releaseStep = -1;
    float creep = 0.0f;
    for (int i = 0; i < 1500; i++) // up to 150 s
    {
        holdDemand();
        fixture.step(dt);
        if (!fixture.isAlive(ego)) break;

        if (!fixture.heldByControl(ego)) { releaseStep = i; break; }
        creep = std::max(creep, ego->getPos() - restPos);
    }

    CHECK_CMP(releaseStep, >=, 0, "the light must eventually release the waiting vehicle");
    CHECK_CMP(creep, <, 0.5f, "a held vehicle must not creep forward while the light is red");
    if (releaseStep < 0) return;

    // Phase 3: the virtual obstacle is gone, so the driver should launch.
    float maxSpeed = 0.0f;
    bool clearedJunction = false;
    bool completedTrip = false;
    for (int i = 0; i < 600; i++) // 60 s
    {
        holdDemand();
        fixture.step(dt);
        if (!fixture.isAlive(ego)) { completedTrip = true; break; }

        maxSpeed = std::max(maxSpeed, ego->getSpeed());
        if (ego->currentRouteIndex >= 1) clearedJunction = true;
    }

    CHECK(clearedJunction || completedTrip,
          "the released vehicle must cross the junction onto the far side");
    CHECK_CMP(maxSpeed, >, 12.0f,
              "after the green the driver must accelerate back toward the 15 m/s limit");
}

SIM_TEST(Int_Yellow_Light_Dilemma_Test,
         "On yellow, a vehicle too close to stop comfortably is let through while one that can stop is held")
{
    IntersectionFixture fixture("signal");

    // Two cars on the same approach at the same speed, in separate lanes so
    // they never interact: one deep in the dilemma zone, one far enough back
    // to stop comfortably. Both are pinned in place, so the only thing that
    // changes between frames is the light.
    VehicleState* nearCar = fixture.spawn(EastToWest, 20.0f, 0.0f, 0);
    VehicleState* farCar = fixture.spawn(EastToWest, 20.0f, 0.0f, 2);

    // A parked car on the north approach keeps demand on the other axis, so
    // the actuated ring keeps cycling instead of resting on green for the
    // only approach that has traffic.
    VehicleState* demand = fixture.spawn(NorthToSouth, 0.0f, 0.0f, 1);

    CHECK(nearCar != nullptr && farCar != nullptr && demand != nullptr,
          "test setup: all three vehicles must bind to their approaches");
    if (nearCar == nullptr || farCar == nullptr || demand == nullptr) return;

    const float dt = 0.1f;
    const float speed = 20.0f;

    fixture.step(dt); // one frame so the edges and stop lines resolve
    const float stopLine = fixture.stopLineFor(nearCar);
    const float northLine = fixture.stopLineFor(demand);

    // Comfortable braking is 2b. At 20 m/s that is a stopping distance of
    // 20^2 / (2 * 2 * 1.5) = 66.7 m, so 20 m in is squarely inside the
    // dilemma zone and 200 m out is squarely outside it.
    const float nearPos = stopLine - 20.0f;
    const float farPos = stopLine - 200.0f;

    bool sawGreenBoth = false;   // both let through
    bool sawDilemma = false;     // the yellow window: only the near car goes
    bool sawRedBoth = false;     // both held
    bool sawInversion = false;   // the far car let through while the near one is held

    for (int i = 0; i < 1800; i++) // 180 s, several full cycles
    {
        IntersectionFixture::freeze(nearCar, nearPos, speed, 0);
        IntersectionFixture::freeze(farCar, farPos, speed, 2);
        IntersectionFixture::freeze(demand, northLine - 20.0f, 0.0f, 1);

        fixture.step(dt);
        if (!fixture.isAlive(nearCar) || !fixture.isAlive(farCar)) break;

        IntersectionFixture::freeze(nearCar, nearPos, speed, 0);
        IntersectionFixture::freeze(farCar, farPos, speed, 2);

        const bool nearHeld = fixture.heldByControl(nearCar);
        const bool farHeld = fixture.heldByControl(farCar);

        if (!nearHeld && !farHeld) sawGreenBoth = true;
        if (!nearHeld && farHeld) sawDilemma = true;
        if (nearHeld && farHeld) sawRedBoth = true;
        if (nearHeld && !farHeld) sawInversion = true;
    }

    CHECK(sawGreenBoth, "test setup: the light must serve this approach at some point");
    CHECK(sawRedBoth, "test setup: the light must also hold this approach at some point");
    CHECK(sawDilemma,
          "there must be a yellow window where the car that cannot stop is let through and the one that can is held");
    CHECK(!sawInversion,
          "a car deep in the dilemma zone must never be held while a car further back is released");
}

SIM_TEST(Int_Yield_Gap_Acceptance_Test,
         "A vehicle waiting on the minor road enters only when the major road is clear enough to cross")
{
    // A T junction: the north/south street is a fast three-lane major road,
    // the east stem is a slow single-lane minor road. Three legs is what
    // makes Network downgrade the stop to a yield and split major from minor.
    IntersectionFixture fixture("stop", IntersectionFixture::Arms::Tee, 3, 20.0, 400.0, 1, 8.0);

    Node* centre = fixture.centreNode();
    CHECK(centre != nullptr, "test setup: the centre node must exist");
    if (centre == nullptr) return;

    CHECK(centre->type == Node::YIELD_STOP,
          "test setup: a three-leg stop must be assigned as a yield");
    const bool eastIsMinor = std::find(centre->minorRoadOriginIds.begin(),
                                       centre->minorRoadOriginIds.end(),
                                       IntersectionFixture::East) != centre->minorRoadOriginIds.end();
    CHECK(eastIsMinor, "test setup: the slow single-lane stem must be the minor road");

    const std::vector<uint64_t> minorRoute = {IntersectionFixture::East,
                                              IntersectionFixture::Centre,
                                              IntersectionFixture::South};
    const std::vector<uint64_t> majorRoute = {IntersectionFixture::North,
                                              IntersectionFixture::Centre,
                                              IntersectionFixture::South};
    const float dt = 0.1f;

    // --- Case 1: cross traffic bearing down on the junction ---------------
    {
        VehicleState* yielding = fixture.spawn(minorRoute, 0.0f, 0.0f, 0);
        VehicleState* crossing = fixture.spawn(majorRoute, 20.0f, 0.0f, 1);
        CHECK(yielding != nullptr && crossing != nullptr,
              "test setup: both vehicles must bind to their approaches");
        if (yielding == nullptr || crossing == nullptr) return;

        fixture.step(dt);
        const float yieldLine = fixture.stopLineFor(yielding);

        bool everGranted = false;
        for (int i = 0; i < 100; i++) // 10 s of sustained cross traffic
        {
            // The minor car waits at the line; the cross car is held ~1.5 s
            // of travel from the junction, far too little to cross in front
            // of.
            IntersectionFixture::freeze(yielding, yieldLine - 0.5f, 0.0f, 0);
            IntersectionFixture::freeze(crossing, 370.0f, 20.0f, 1);
            fixture.step(dt);
            if (!fixture.isAlive(yielding) || !fixture.isAlive(crossing)) break;
            IntersectionFixture::freeze(yielding, yieldLine - 0.5f, 0.0f, 0);

            if (!fixture.heldByControl(yielding)) everGranted = true;
        }

        CHECK(!everGranted,
              "a yielding vehicle must not pull out in front of close, fast cross traffic");
    }

    // --- Case 2: the same junction with the major road empty --------------
    {
        IntersectionFixture clear("stop", IntersectionFixture::Arms::Tee, 3, 20.0, 400.0, 1, 8.0);
        VehicleState* yielding = clear.spawn(minorRoute, 0.0f, 0.0f, 0);
        CHECK(yielding != nullptr, "test setup: the yielding vehicle must bind");
        if (yielding == nullptr) return;

        clear.step(dt);
        const float yieldLine = clear.stopLineFor(yielding);

        bool granted = false;
        for (int i = 0; i < 100; i++)
        {
            IntersectionFixture::freeze(yielding, yieldLine - 0.5f, 0.0f, 0);
            clear.step(dt);
            if (!clear.isAlive(yielding)) { granted = true; break; }
            IntersectionFixture::freeze(yielding, yieldLine - 0.5f, 0.0f, 0);

            if (!clear.heldByControl(yielding)) { granted = true; break; }
        }

        CHECK(granted, "with the major road empty the yielding vehicle must be allowed to go");
    }
}

SIM_TEST(Int_Right_Of_Way_Deadlock_Test,
         "Four vehicles arriving together at an all-way stop are each granted right-of-way in turn, with no deadlock")
{
    IntersectionFixture fixture("stop");

    Node* centre = fixture.centreNode();
    CHECK(centre != nullptr && centre->type == Node::FOUR_WAY_STOP,
          "test setup: a four-leg stop must stay an all-way stop");
    if (centre == nullptr || centre->type != Node::FOUR_WAY_STOP) return;

    // Identical distance, identical speed, one per approach: they reach their
    // stop lines on the same frame, which is the case that can deadlock.
    std::vector<VehicleState*> cars;
    cars.push_back(fixture.spawn(NorthToSouth, 12.0f, 250.0f, 1));
    cars.push_back(fixture.spawn(SouthToNorth, 12.0f, 250.0f, 1));
    cars.push_back(fixture.spawn(EastToWest, 12.0f, 250.0f, 1));
    cars.push_back(fixture.spawn(WestToEast, 12.0f, 250.0f, 1));

    for (VehicleState* car : cars)
    {
        CHECK(car != nullptr, "test setup: every vehicle must bind to its approach");
        if (car == nullptr) return;
    }

    const float dt = 0.1f;
    float longestWait = 0.0f;
    int clearedStep = -1;

    for (int i = 0; i < 2000; i++) // up to 200 s
    {
        fixture.step(dt);

        for (VehicleState* car : cars)
        {
            if (fixture.isAlive(car)) longestWait = std::max(longestWait, car->getWaitTime());
        }

        if (fixture.physics->getActiveVehicles().empty()) { clearedStep = i; break; }
    }

    CHECK_CMP(clearedStep, >=, 0,
              "all four vehicles must clear the intersection -- none may sit deadlocked");
    // Measured: ~11.9 s for the last of the four to be served. A jammed
    // intersection is not marginally worse, it is unbounded (the same test
    // against a stop that never grants occupancy sat at 183 s), so 30 s
    // separates the two cleanly without being sensitive to timing drift.
    CHECK_CMP(longestWait, <, 30.0f,
              "no vehicle may wait 30 s at a four-way stop shared with only three others");
}
