#include "vehicle_state.h"
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

void VehicleState::setDesiredSpeed(float new_des_speed)
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
void VehicleState::updateWaitTime(float dt, float speedThreshold) 
{
    if (m_speed < speedThreshold) 
    {
        m_waitTime += dt;
    }
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
    desiredSpeed(params.desiredSpeed),
    minGap(params.minGap),
    safeBrakePower(params.safeBrakePower),
    safeTimeHeadway(params.safeTimeHeadway),
    id(count),
    m_length(params.length)
{ 
    count++; 
}

void VehicleState::setLane(int newLane) 
{
    m_lane = newLane;
}

Road* VehicleState::getCurrentEdge() const 
{
    return currentEdge;
}

void VehicleState::setCurrentEdge(Road* edge) 
{
    currentEdge = edge;
}