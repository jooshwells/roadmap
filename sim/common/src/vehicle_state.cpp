#include "vehicle_state.h"
#include <iostream>

void VehicleState::accelerate(float amount) {
    m_speed += amount;
}

void VehicleState::move(float distance) {
    m_pos += distance;
}

void VehicleState::setLeader(VehicleState* newLeader) {
    leader = newLeader;
}

float VehicleState::getSpeed() const {
    return m_speed;
}

float VehicleState::getPos() const {
    return m_pos;
}

int VehicleState::getCount() const {
    return count;
}

// Implement if memory needs to be freed up
VehicleState::~VehicleState() {}

VehicleState::VehicleState(float initialSpeed, float initialPosition) : m_speed(initialSpeed), id(count), m_pos(initialPosition)
{ 
    std::cout << "State instantiated" << std::endl; 
    count++; 
}