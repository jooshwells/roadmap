#ifndef HEURISTICS_H
#define HEURISTICS_H

#include <limits>
#include <vector>
#include "road.h"

const double INF = std::numeric_limits<double>::infinity();

struct State {
    double x, y, z;
    double g = INF;
    double rhs = INF;

    std::vector<Road> neighbors;

    bool operator==(const State& other) const {
        return x == other.x && y == other.y && z == other.z;
    }
};

typedef std::pair<double, double> Key;

struct QueueElement {
    Key key;
    State* state;

    bool operator<(const QueueElement& other) const {
        if (key.first != other.key.first) return key.first < other.key.first;
        if (key.second != other.key.second) return key.second < other.key.second;

        if (state->x != other.state->x) return state->x < other.state->x;
        if (state->y != other.state->y) return state->y < other.state->y;
        return state->z < other.state->z;
    }
};

class Heuristics
{
    public:
        double Euclidean3D()

};

#endif