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

// getters
float VehicleState::getSpeed() const 
{
    return m_speed;
}

float VehicleState::getPos() const 
{
    return m_pos;
}

int VehicleState::getCount() const 
{
    return count;
}

VehicleState* VehicleState::getLeader() const
{
    return leader;
}


// Implement if memory needs to be freed up
VehicleState::~VehicleState() {}

VehicleState::VehicleState(float initialSpeed, float initialPosition) : m_speed(initialSpeed), id(count), m_pos(initialPosition)
{ 
    std::cout << "State instantiated" << std::endl; 
    count++; 
}


// Moved to physics
// void VehicleState::update(float dt) 
// {
//     // check braking logic
//     if(leader!=nullptr) {
//         float leaderPos=leader->getPos();
//         float myPos=m_pos;
//         float gap =leaderPos-myPos;

//         // just tesing with gap of 20 for now
//         if(gap<20.0f) {
//             float brakePower = -4.0f * dt;
//             accelerate(brakePower);
//         }
//     }

//     move(m_speed *dt);
//     // no negative speed 
//     if (m_speed < 0.0f) {
//         m_speed = 0.0f;
//     }
// }