#ifndef STATE_3D_H
#define STATE_3D_H

#include <vector>
#include <limits>

struct State3D;

// Represents a directed edge to a neighboring state
struct Edge3D {
    State3D* target;
    double cost;

    Edge3D(State3D* t, double c);
};

// The core State component
struct State3D {
    int x, y, z;

    // D* vars
    double g;
    double rhs;

    // 3. Adjacency List (Topology)
    std::vector<Edge3D> neighbors;

    // Constructors
    State3D(int x, int y, int z);

    bool operator==(const State3D& other) const;
    bool operator!=(const State3D& other) const;

    // Graph Building Helpers
    void AddNeighbor(State3D* target, double cost);

    // Returns true if the edge was found and updated
    bool UpdateEdgeCost(State3D* target, double new_cost);
    
    void ResetPathfindingData();
};

#endif