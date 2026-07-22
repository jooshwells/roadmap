# Simulation Unit Tests

Unit tests for the driver-behavior side of the simulation: the IDM
car-following model, the MOBIL lane-change model, and the driver personality
layer. No external test framework is used — `test_harness.h` is ~150 lines of
self-registering test plumbing, so the sim keeps its current dependency list.

Results follow the project convention: **1 = Pass, 0 = Fail**.

## Building and running

The tests build with everything else:

```sh
cd sim
./build.sh                 # or: cmake --build build --config Release
./build/tests/Release/sim_tests.exe
```

To build only the tests, or to skip them entirely:

```sh
cmake --build build --config Release --target sim_tests
cmake -S . -B build -DROADMAP_BUILD_TESTS=OFF     # sim-only build
```

Command line:

| Invocation | Effect |
| --- | --- |
| `sim_tests` | Run every test |
| `sim_tests IDM_` | Run only tests whose name contains `IDM_` |
| `sim_tests --csv results.csv` | Also write a `test_name,result` CSV |

The exit code is the number of failed tests, so a CI step can gate on it.

## What is covered

### IDM (`test_idm.cpp`)

| Test | What it verifies |
| --- | --- |
| `IDM_Steady_State_Test` | A vehicle with no leader, at its desired speed, has zero acceleration |
| `IDM_Start_From_Rest_Test` | A stopped vehicle on an empty road pulls away at exactly `maxAccel` |
| `IDM_Over_Speed_Test` | Above `v0`, free-road deceleration matches `a(1 - (v/v0)^delta)` |
| `IDM_Ideal_Speed_Test` | Alone on the road, speed rises monotonically and asymptotes to `v0` |
| `IDM_Emrgncy_Brake_Test` | A gap far below the safe headway `s*` saturates the braking clamp |
| `IDM_Decel_Clamp_Test` | Across adversarial gaps/speeds, output stays finite and within `[-10, maxAccel]` |
| `IDM_Gap_Monotonicity_Test` | More room ahead never means harder braking; a far leader tends to free-road accel |
| `IDM_Approach_Rate_Test` | At a fixed gap, faster closing speed means harder braking |
| `IDM_Smooth_Approach_Test` | Catching a slower leader sheds speed comfortably, with no undershoot and no emergency braking |
| `IDM_Equilibrium_Follow_Test` | A settled follower matches the leader's speed at the closed-form equilibrium gap |
| `IDM_Stopped_Leader_Test` | A car approaching a stalled car stops `s0` behind it, never touching it |
| `IDM_Emergency_Stop_No_Collision_Test` | A follower at the equilibrium gap survives a `-6 m/s^2` leader stop |
| `IDM_Dead_Leader_Ignored_Test` | A leader marked for deletion stops braking the traffic behind it |
| `IDM_Invalid_Vehicle_Test` | Null / deleted / edge-less vehicles return `0` instead of crashing |

### MOBIL (`test_mobil.cpp`)

| Test | What it verifies |
| --- | --- |
| `MOBIL_Blocked_Lane_Incentive_Test` | An empty lane beside a slow leader is an attractive change |
| `MOBIL_No_Incentive_When_Clear_Test` | Two clear lanes tie; moving behind a slow car is discouraged |
| `MOBIL_Unsafe_Cut_In_Vetoed_Test` | Cutting in front of a fast car returns the `-999` unsafe sentinel |
| `MOBIL_Leader_Crash_Vetoed_Test` | Merging into a sub-`0.5 * s0` space returns `-999` |
| `MOBIL_Politeness_Test` | A polite driver discounts a change that hurts the new follower |

### Intersections (`test_intersections.cpp`)

| Test | What it verifies |
| --- | --- |
| `Int_Red_Light_Approach_Test` | A car braking for a red stops `s0` short of the stop line, without emergency braking |
| `Int_Green_Light_Resume_Test` | A queued car holds position (no creep), then launches and clears the junction on release |
| `Int_Yellow_Light_Dilemma_Test` | On yellow, a car that cannot stop is let through while one that can is held |
| `Int_Yield_Gap_Acceptance_Test` | A minor-road car waits for close cross traffic and goes when the major road is clear |
| `Int_Right_Of_Way_Deadlock_Test` | Four cars arriving together at an all-way stop all clear, none deadlocks |

Two things about these are worth knowing before you extend them.

**The signal is actuated.** An approach that demands a green with no competing
traffic is simply served, so a lone car never sees a red at all. The red-light
and green-resume tests park a car on the cross street specifically to create
the red they are testing — without it they pass vacuously by watching a car
sail through a green.

**Right-of-way at an all-way stop is FIFO, not "yield to the vehicle on the
right".** `controlGrantsEntry` admits cars to `waitQueue` in the order they
come to a full stop at the line, and serves one occupant at a time. The test
therefore asserts the property that matters — every car is eventually served,
and none waits unboundedly — rather than a rule the simulation does not
implement.

### Driver personalities (`test_driver_profiles.cpp`)

| Test | What it verifies |
| --- | --- |
| `Driver_Profile_Range_Test` | Every sampled parameter stays inside its documented archetype range |
| `Driver_Profile_Ordering_Test` | Aggressive > Average > Cautious on accel, headway, speed factor, reaction |
| `Driver_Population_Mix_Test` | The spawn mix is ~20% cautious / 60% average / 20% aggressive |
| `Driver_Speed_Factor_Test` | The applied target is `speed limit * speedFactor` |
| `Vehicle_Launch_Boost_Test` | The standing-start boost arms after a stop and fades out by 9 m/s |
| `Vehicle_Reaction_Delay_Test` | A stopped driver holds for `reactionTime`; a rolling driver does not |

## Adding a test

Drop a `SIM_TEST` block into any file in the `sim_tests` source list — the
registration is automatic:

```cpp
SIM_TEST(My_New_Test, "One line describing what must be true")
{
    StraightRoadFixture fixture;
    VehicleState* ego = fixture.spawn(/*speed*/ 20.0f, /*pos*/ 100.0f);

    CHECK_NEAR(fixture.idm(ego, nullptr), 0.42f, 1e-3f, "why this must hold");
}
```

Available checks: `CHECK`, `CHECK_NEAR`, `CHECK_CMP`, `CHECK_FINITE`. A failed
check records its reason and lets the test continue, so one run reports every
broken expectation rather than only the first.

`sim_fixtures.h` provides `StraightRoadFixture` (a two-node uncontrolled road
with a live `PhysicsProcessor`, vehicle spawning, spatial-hash refresh, and the
same integration step the physics loop uses) plus the analytic helpers
`idmEquilibriumGap` and `idmDesiredGap`.

`intersection_fixtures.h` provides `IntersectionFixture` — a signalized,
all-way-stop, or yield junction with four (or three) approach arms, driven
through the real `update()` loop. Note two differences from
`StraightRoadFixture`:

- **It does not own its vehicles.** `PhysicsProcessor::addVehicle` takes
  ownership and `update()` deletes cars that reach their destination, so check
  `isAlive(v)` before dereferencing anything you held across a `step()`.
- **Control decisions are read through `heldByControl(v)`.** The gating logic
  is private, but a denied car is handed a zero-length ghost leader parked at
  the stop line, and that is both observable and exactly what the driver feels.

`freeze(v, pos, speed, lane)` pins a car in place across frames, which is how
the dilemma-zone and gap-acceptance tests interrogate the control logic at a
known distance and speed instead of chasing a moving target.

New source files need to be added to `tests/CMakeLists.txt`.

## A note on the equilibrium gap

A settled follower does **not** sit at the safe headway `s*`. Setting the IDM
acceleration to zero gives

```
0 = 1 - (v/v0)^delta - (s*/s)^2   =>   s = s* / sqrt(1 - (v/v0)^delta)
```

so below `v0` the equilibrium gap is strictly *larger* than `s*` (at 20 m/s
with the basic profile: 34.8 m against an `s*` of 32.0 m). A driver holding
exactly `s*` would still be fighting its own free-road term. This matters for
test design: an equilibrium test that asserted `gap == s*` would pass only on
an IDM with the free-road term deleted — which is exactly what the mutation
run below produced. `IDM_Equilibrium_Follow_Test` asserts the full expression
and that the settled gap exceeds `s*`.

## A note on the tests themselves

These assertions were verified by mutation testing — deliberately breaking the
IDM and confirming the right tests flipped to `0` before restoring the code:

| Mutation | Tests that caught it |
| --- | --- |
| Deceleration clamp widened from `-10` to `-50` | `IDM_Emrgncy_Brake_Test`, `IDM_Decel_Clamp_Test`, `IDM_Dead_Leader_Ignored_Test` |
| Free-road term `(v/v0)^delta` dropped from the acceleration equation | `IDM_Steady_State_Test`, `IDM_Over_Speed_Test`, `IDM_Ideal_Speed_Test`, `IDM_Equilibrium_Follow_Test` |
| Anticipation term `v*dv / (2*sqrt(a*b))` dropped from the desired gap | `IDM_Smooth_Approach_Test`, `IDM_Stopped_Leader_Test`, `IDM_Emergency_Stop_No_Collision_Test` |
| Yellow-phase `cannotStop` dilemma-zone override forced to `false` | `Int_Yellow_Light_Dilemma_Test` |
| `hasSafeGap` forced to `true` | `Int_Yield_Gap_Acceptance_Test` |
| All-way stop never granting occupancy | `Int_Right_Of_Way_Deadlock_Test` |

The third mutation is the interesting one: without the closing-speed term the
follower brakes at `-6.2 m/s^2`, undershoots to 11.2 m/s, and then actually
rear-ends both the stalled car and the hard-braking leader — the collision
assertions are load-bearing, not decoration.
