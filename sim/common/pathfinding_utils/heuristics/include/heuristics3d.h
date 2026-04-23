#ifndef HEURISTICS_H
#define HEURISTICS_H

#include <cmath>
#include <algorithm>
#include "node.h"

namespace Heuristics3D {

    // 1. Direct line-of-sight distance. (Allows smooth 3D diagonal movement).
    double Euclidean(const Node* a, const Node* b);

    // 2. Strict grid distance. (Only allows Up/Down/Left/Right/Forward/Backward).
    double Manhattan(const Node* a, const Node* b);

    // 3. Chebyshev distance. (Diagonal movement costs the same as straight movement).
    double Chebyshev(const Node* a, const Node* b);

    // Distance function that calculated flat distance, but penalizes large changes in z
    double PenalizedZ(const Node* a, const Node* b); 
}

#endif