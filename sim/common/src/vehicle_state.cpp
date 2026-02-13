#include "vehicle_state.h"
#include <iostream>

void VehicleState::accelerate(float amt) {
    m_speed += amt;
}

float VehicleState::getSpeed() const {
    return m_speed;
}

// Implement if memory needs to be freed up
VehicleState::~VehicleState() {}

VehicleState::VehicleState(float initialSpeed) : m_speed(initialSpeed) { std::cout << "State instantiated" << std::endl; }