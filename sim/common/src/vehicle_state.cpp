#include "vehicle_state.h"
#include <iostream>

void VehicleState::accelerate(float amount) {
    m_speed += amount;
}

void VehicleState::move(float distance) {
    m_pos += distance;
}

float VehicleState::getSpeed() const {
    return m_speed;
}

float VehicleState::getPos() const {
    return m_pos;
}

// Implement if memory needs to be freed up
VehicleState::~VehicleState() {}

VehicleState::VehicleState(float initialSpeed) : m_speed(initialSpeed) { std::cout << "State instantiated" << std::endl; }