#include "state3d.h"

const double INF = std::numeric_limits<double>::infinity(); // infinity

Edge3D::Edge3D(State3D* t, double c) : target(t), cost(c) {}

State3D::State3D(int x_val, int y_val, int z_val) 
    : x(x_val), y(y_val), z(z_val), g(INF), rhs(INF) { // not pre-allocated vector here, but can for memory optimization if needed
}

bool State3D::operator==(const State3D& other) const {
    return x == other.x && y == other.y && z == other.z;
}

bool State3D::operator!=(const State3D& other) const {
    return !(*this == other); // Reuse the equality operator
}

void State3D::AddNeighbor(State3D* target, double cost) {
    // emplace_back constructs the Edge3D directly in place, saving a copy
    neighbors.emplace_back(target, cost);
}

bool State3D::UpdateEdgeCost(State3D* target, double new_cost) {
    for (auto& edge : neighbors) {
        if (edge.target == target) {
            edge.cost = new_cost;
            return true; 
        }
    }
    return false; // Edge does not exist in this state's adjacency list
}

void State3D::ResetPathfindingData() {
    g = INF;
    rhs = INF;
}