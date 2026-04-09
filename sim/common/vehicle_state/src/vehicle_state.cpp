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

void VehicleState::setLeader(VehicleState* newLeader) 
{
    leader = newLeader;
}

// Implement if memory needs to be freed up
VehicleState::~VehicleState() {}


VehicleState::VehicleState(float iS, float iP, int startingLane, const IDMParameters& params) :
    m_speed(iS),
    m_pos(iP),
    m_lane(startingLane),
    accelExp(params.accelExp),
    maxAccel(params.maxAccel),
    desiredSpeed(params.desiredSpeed),
    minGap(params.minGap),
    safeBrakePower(params.safeBrakePower),
    safeTimeHeadway(params.safeTimeHeadway),
    id(count),
    m_length(params.length)
{ 
    // std::cout << "State instantiated" << std::endl; 
    count++; 
}

void VehicleState::setLane(int newLane) 
{
    m_lane = newLane;
}