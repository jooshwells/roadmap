#include "heuristics3d.h"
#include <cmath>
#include <algorithm>

namespace Heuristics3D {

    double Euclidean(const Node* a, const Node* b) {
        double dx = a->getX() - b->getX();
        double dy = a->getY() - b->getY();
        double dz = a->getZ() - b->getZ();
        
        return std::sqrt((dx * dx) + (dy * dy) + (dz * dz));
    }

    double Manhattan(const Node* a, const Node* b) {
        double dx = std::abs(a->getX() - b->getX());
        double dy = std::abs(a->getY() - b->getY());
        double dz = std::abs(a->getZ() - b->getZ());
        
        return dx + dy + dz;
    }

    double Chebyshev(const Node* a, const Node* b) {
        double dx = std::abs(a->getX() - b->getX());
        double dy = std::abs(a->getY() - b->getY());
        double dz = std::abs(a->getZ() - b->getZ());
        
        // Returns the longest distance along any single axis
        return std::max({dx, dy, dz});
    }

    double PenalizedZ(const Node* a, const Node* b) {
        // Flat 2D Euclidean distance on the X/Y plane
        double dx = a->getX() - b->getX();
        double dy = a->getY() - b->getY();
        double flat_distance = std::sqrt((dx * dx) + (dy * dy));
        
        // Calculate the Z difference
        double dz = std::abs(a->getZ() - b->getZ());
        
        // Apply a weight to the Z-axis. 
        // Example: Changing elevation by 1 unit is treated as 3 units of flat driving.
        const double Z_MULTIPLIER = 3.0; 
        
        return flat_distance + (dz * Z_MULTIPLIER);
    }

}