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

VehicleState::VehicleState(float iS, float iP, float aExp, float mA, float dS, float mG, float sB, float sTH) :
 m_speed(iS),
 m_pos(iP),
 accelExp(aExp),
 maxAccel(mA),
 desiredSpeed(dS),
 minGap(mG),
 safeBrakePower(sB),
 safeTimeHeadway(sTH),
 id(count)
{ 
    // std::cout << "State instantiated" << std::endl; 
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