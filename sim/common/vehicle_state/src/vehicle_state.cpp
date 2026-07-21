#include "vehicle_state.h"
#include <algorithm>
#include <iostream>

void VehicleState::accelerate(float amount) 
{
    m_speed += amount;
}

void VehicleState::move(float distance) 
{
    m_pos += distance;
}

void VehicleState::setPos(float new_pos)
{
    m_pos = new_pos;
}

// Every speed target (road limit, junction turn pacing) is filtered through
// the driver's personality: aggressive drivers cruise over the limit and
// sweep turns harder, cautious ones sit under it.
void VehicleState::setDesiredSpeed(float new_des_speed)
{
    desiredSpeed = new_des_speed * m_speedFactor;
}

// Geometry-derived corner speeds are a physical lateral-accel cap (personality
// already baked in via latAccel); take them verbatim so speedFactor doesn't
// scale them a second time.
void VehicleState::setDesiredSpeedRaw(float new_des_speed)
{
    desiredSpeed = new_des_speed;
}

void VehicleState::setLeader(VehicleState* newLeader) 
{
    leader = newLeader;
}

// NEW: Store the current acceleration calculated by the physics step
void VehicleState::setAcceleration(float accel) 
{
    m_acceleration = accel;
}

// NEW: Accumulate wait time if moving below the threshold (e.g., 0.5 m/s)
// Also drives the launch-boost state machine: a sustained full stop arms the
// boost, and it stays live until the car has accelerated past the boost's
// fade-out speed. Slowing down again without fully stopping does not re-arm.
void VehicleState::updateWaitTime(float dt, float speedThreshold)
{
    if (m_speed < speedThreshold)
    {
        m_waitTime += dt;
    }

    if (m_speed < LaunchArmSpeed)
    {
        m_stopDuration += dt;
        if (m_stopDuration >= LaunchArmStopTime) m_launchBoostArmed = true;
    }
    else
    {
        m_stopDuration = 0.0f;
        if (m_speed >= LaunchBoostEndSpeed) m_launchBoostArmed = false;
    }
}

float VehicleState::getLaunchBoost() const
{
    if (!m_launchBoostArmed || m_speed >= LaunchBoostEndSpeed) return 1.0f;
    const float t = std::max(0.0f, m_speed) / LaunchBoostEndSpeed;
    return m_launchBoostFactor - (m_launchBoostFactor - 1.0f) * t;
}

float VehicleState::applyReactionDelay(float accel, float dt)
{
    // Only launches from a genuine stop are gated (same arming as the launch
    // boost); rolling drivers respond through their time headway instead.
    if (m_launchBoostArmed && m_speed < LaunchArmSpeed)
    {
        if (accel > 0.05f)
        {
            m_reactionElapsed += dt;
            if (m_reactionElapsed < m_reactionTime) return 0.0f;
        }
        else
        {
            // Go condition vanished (light back to red, queue re-compressed):
            // the driver will need a fresh reaction next time.
            m_reactionElapsed = 0.0f;
        }
    }
    else
    {
        m_reactionElapsed = 0.0f;
    }
    return accel;
}

// NEW: Safely get the current road's ID
uint64_t VehicleState::getEdgeId() const 
{
    return (currentEdge != nullptr) ? currentEdge->getEdgeId() : 0;
}

VehicleState::~VehicleState() {}

// NEW: Constructor now accepts and initializes origin and destination
VehicleState::VehicleState(uint64_t originNode, uint64_t destNode, float iS, float iP, int startingLane, const IDMParameters& params) :
    m_speed(iS),
    m_pos(iP),
    m_lane(startingLane),
    m_origin(originNode),       // INITIALIZE
    m_destination(destNode),    // INITIALIZE
    accelExp(params.accelExp),
    maxAccel(params.maxAccel),
    desiredSpeed(params.desiredSpeed * params.speedFactor),
    minGap(params.minGap),
    safeBrakePower(params.safeBrakePower),
    safeTimeHeadway(params.safeTimeHeadway),
    id(count),
    m_length(params.length),
    politeness(params.politeness),
    m_laneFrom(startingLane),
    m_profileName(params.profileName),
    m_speedFactor(params.speedFactor),
    m_reactionTime(params.reactionTime),
    m_launchBoostFactor(params.launchBoostFactor),
    m_latAccel(params.latAccel)
{
    count++;
}

void VehicleState::setLane(int newLane)
{
    // Instant snap (spawn / lane clamping at edge transitions): abandon any
    // transition in progress so the car doesn't render offset from a lane
    // that no longer exists on the new edge.
    m_lane = newLane;
    m_laneFrom = newLane;
    m_laneChangeElapsed = 0.0f;
    m_laneChangeDuration = 0.0f;
}

void VehicleState::startLaneChange(int targetLane)
{
    if (targetLane == m_lane) return;

    m_laneFrom = m_lane;
    m_lane = targetLane; // commit immediately; physics treats us as in the target lane

    // Polite drivers ease over gently, impolite ones dart across.
    // politeness 0 -> ~1.5s, politeness 1 -> ~5s.
    m_laneChangeDuration = 1.5f + politeness * 3.5f;
    m_laneChangeElapsed = 0.0f;
}

void VehicleState::updateLaneChange(float dt)
{
    if (m_laneChangeCooldown > 0.0f) {
        m_laneChangeCooldown -= dt;
    }

    if (!isChangingLanes()) return;

    m_laneChangeElapsed += dt;
    if (m_laneChangeElapsed >= m_laneChangeDuration) {
        m_laneFrom = m_lane;
        m_laneChangeElapsed = 0.0f;
        m_laneChangeDuration = 0.0f;
        // Brief settling period before the next MOBIL decision, so cars
        // don't immediately weave back.
        m_laneChangeCooldown = 1.0f + politeness * 2.0f;
    }
}

float VehicleState::getRenderLane() const
{
    if (!isChangingLanes()) return static_cast<float>(m_lane);

    float t = m_laneChangeElapsed / m_laneChangeDuration;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    float eased = t * t * (3.0f - 2.0f * t); // smoothstep
    return m_laneFrom + eased * (m_lane - m_laneFrom);
}

float VehicleState::getLaneChangeLateralRate() const
{
    if (!isChangingLanes()) return 0.0f;

    float t = m_laneChangeElapsed / m_laneChangeDuration;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    float easedRate = 6.0f * t * (1.0f - t) / m_laneChangeDuration; // d(smoothstep)/dt
    return easedRate * (m_lane - m_laneFrom);
}

Road* VehicleState::getCurrentEdge() const 
{
    return currentEdge;
}

void VehicleState::setCurrentEdge(Road* edge)
{
    // A new edge means a new stop line, so the wrong-lane hold re-arms.
    if (edge != currentEdge) m_wrongLaneHoldServed = false;
    currentEdge = edge;
}