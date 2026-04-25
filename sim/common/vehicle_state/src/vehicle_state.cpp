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

// Implement if memory needs to be freed up
VehicleState::~VehicleState() {}


VehicleState::VehicleState(float iS, float iP, const IDMParameters& params) :
    m_speed(iS),
    m_pos(iP),
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