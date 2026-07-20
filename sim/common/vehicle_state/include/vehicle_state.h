#ifndef VEHICLE_STATE_H
#define VEHICLE_STATE_H
#include "road.h"

#include <vector>
#include <cstdint>

struct IDMParameters {
    float accelExp;
    float maxAccel;
    float desiredSpeed;
    float minGap;
    float safeBrakePower;
    float safeTimeHeadway;
    float length;
    float politeness;      // MOBIL p: 0 = selfish, 1 = selfless

    // Driver personality. Defaulted so existing positional brace-inits of the
    // eight original fields keep compiling (ghost cars, tests).
    const char* profileName = "Average"; // static string; shown by the stats panel
    float speedFactor = 1.0f;       // desired speed as a multiple of the road limit
    float reactionTime = 0.0f;      // s of lag before pulling away from a stop
    float launchBoostFactor = 2.0f; // standing-start accel multiplier (see getLaunchBoost)
};

class VehicleState {

    public:
        /* Write Functions */
        void accelerate(float amount); 
        void move(float distance);     
        void setPos(float new_pos);
        void setLeader(VehicleState* newLeader);
        void setLane(int newLane);
        void setDesiredSpeed(float new_des_speed);

        // Progressive lane changing: setLane() snaps instantly (used for lane
        // clamping at edge transitions); startLaneChange() begins a timed
        // transition whose duration scales with the driver's politeness.
        void startLaneChange(int targetLane);
        void updateLaneChange(float dt);
        
        // NEW: Allow physics engine to update these values
        void setAcceleration(float accel);
        void updateWaitTime(float dt, float speedThreshold = 0.5f);

        // Per-driver reaction lag: while the car sits at a full stop and the
        // physics first asks it to go (light turned green, the queue ahead
        // moved), the requested acceleration is held at zero until the go
        // condition has persisted for this driver's reactionTime. Called once
        // per physics tick with the frame's intended acceleration; returns
        // the (possibly suppressed) acceleration to apply.
        float applyReactionDelay(float accel, float dt);

        /* Read Functions */
        inline int   getId() const                { return id; }
        inline float getSpeed() const             { return m_speed; }
        inline float getPos() const               { return m_pos; }
        inline int   getLane() const              { return m_lane; }
        
        // NEW: Telemetry getters
        inline float getAcceleration() const      { return m_acceleration; }
        inline float getWaitTime() const          { return m_waitTime; }
        inline uint64_t getOrigin() const         { return m_origin; }
        inline uint64_t getDestination() const    { return m_destination; }
        uint64_t getEdgeId() const; 

        inline int   getCount() const             { return count; }
        inline float getDesiredSpeed() const      { return desiredSpeed; }
        inline float getAccelExp() const          { return accelExp; }
        inline float getMinGap() const            { return minGap; }
        inline float getSafeBrakePower() const    { return safeBrakePower; }
        inline float getSafeTimeHeadway() const   { return safeTimeHeadway; }
        inline VehicleState* getLeader() const    { return leader; }
        inline float getMaxAccel() const          { return maxAccel; }
        // Standing-start multiplier for drive acceleration (never braking):
        // drivers push notably harder pulling away from a full stop -- red
        // light, stop sign -- than plain IDM's gentle free-road ramp. Arms
        // after a genuine stop, pays out LaunchBoostFactor at standstill, and
        // fades to 1x by LaunchBoostEndSpeed.
        float getLaunchBoost() const;
        // Continuous time (s) spent below LaunchArmSpeed; resets the moment
        // the car moves again. Used by the wrong-lane gate's hesitation
        // window before it reroutes around a turn its lane doesn't allow.
        inline float getStopDuration() const      { return m_stopDuration; }
        // One-shot release of the wrong-lane hold on the current edge: once
        // the hold has been served (or given up on), the gate must stay open
        // until the next edge -- getStopDuration() resets on any creep, so
        // re-arming from it traps cars in a stop/creep/stop loop forever.
        inline bool hasServedWrongLaneHold() const { return m_wrongLaneHoldServed; }
        inline void markWrongLaneHoldServed()      { m_wrongLaneHoldServed = true; }
        // Lifetime count of wrong-lane reroutes. The gate stops offering
        // reroutes past a small cap so a driver who keeps landing in wrong
        // lanes eventually just takes the wrong-lane turn instead of
        // orbiting the same blocks forever.
        inline int  getRerouteCount() const        { return m_rerouteCount; }
        inline void incrementRerouteCount()        { ++m_rerouteCount; }
        inline float getLength() const            { return m_length;}
        inline float getPoliteness() const        { return politeness; }
        inline const char* getProfileName() const { return m_profileName; }
        inline float getSpeedFactor() const       { return m_speedFactor; }
        inline float getReactionTime() const      { return m_reactionTime; }

        // Lane transition state (m_lane is always the committed target lane)
        inline bool  isChangingLanes() const      { return m_laneChangeElapsed < m_laneChangeDuration; }
        inline int   getPreviousLane() const      { return m_laneFrom; }
        inline bool  canStartLaneChange() const   { return !isChangingLanes() && m_laneChangeCooldown <= 0.0f; }
        // Continuous lane position for rendering: eases from the old lane to
        // the new one (smoothstep) over the transition interval.
        float getRenderLane() const;
        // d(renderLane)/dt in lanes per second; lets the renderer angle the
        // car toward the target lane proportionally to how fast it is merging.
        float getLaneChangeLateralRate() const;

        Road* getCurrentEdge() const;
        void setCurrentEdge(Road* edge);

        // NEW: Added origin and destination IDs to constructor
        VehicleState(uint64_t originNode, uint64_t destNode, float iS, float iP, int startingLane, const IDMParameters& params);
        ~VehicleState();

        std::vector<uint64_t> currentRoute;
        uint64_t currentRouteIndex;
        bool isMarkedForDeletion = false;
        bool isAlive() const { return !isMarkedForDeletion; }
        static bool isSafe(VehicleState* v) {
            return (v != nullptr && !v->isMarkedForDeletion);
        }

    private:
        float m_speed;
        float m_pos;
        int m_lane;

        // Lane transition state: while elapsed < duration the car is sliding
        // from m_laneFrom toward m_lane. Duration of 0 means "not changing".
        int   m_laneFrom = 0;
        float m_laneChangeElapsed = 0.0f;
        float m_laneChangeDuration = 0.0f;
        float m_laneChangeCooldown = 0.0f;

        // Launch boost state (see getLaunchBoost). Arming uses a wider speed
        // band than the wait-time threshold because a car held at a stop
        // line creeps against its ghost leader (oscillating ~0-0.8 m/s)
        // rather than resting at exactly zero -- that creep is still a stop.
        static constexpr float LaunchBoostEndSpeed = 9.0f;  // m/s, ~20 mph
        static constexpr float LaunchArmSpeed      = 1.0f;  // m/s, counts as stopped
        static constexpr float LaunchArmStopTime   = 0.5f;  // s below that to arm
        float m_stopDuration = 0.0f;
        bool  m_launchBoostArmed = false;

        // Driver personality (fixed at spawn from IDMParameters).
        const char* m_profileName = "Average";
        float m_speedFactor = 1.0f;       // scales every desired-speed target
        float m_reactionTime = 0.0f;      // s (see applyReactionDelay)
        float m_launchBoostFactor = 2.0f; // per-driver standing-start kick
        float m_reactionElapsed = 0.0f;   // s the current go condition has persisted

        // Wrong-lane hold state (see hasServedWrongLaneHold); cleared by
        // setCurrentEdge at every edge transition.
        bool m_wrongLaneHoldServed = false;
        int  m_rerouteCount = 0;

        // NEW: Telemetry state variables
        float m_acceleration = 0.0f;
        float m_waitTime = 0.0f;
        uint64_t m_origin;
        uint64_t m_destination;

        float accelExp; 
        float maxAccel;
        float desiredSpeed; 
        float minGap; 
        float safeBrakePower;
        float safeTimeHeadway;
        float m_length;
        float politeness;

        Road* currentEdge = nullptr; 
    
        int id;
        inline static int count = 0;

        VehicleState* leader = nullptr;
};

#endif