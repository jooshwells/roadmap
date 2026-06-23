#ifndef NODE_H
#define NODE_H

#include <cstdint>
#include <vector>
#include <limits>
#include "road.h"
// #include <GeographicLib/LocalCartesian.hpp>

class Node
{
    public:
        // Getters
        inline double getLon() const { return lon; }
        inline double getLat() const { return lat; }
        inline uint64_t    getId()  const { return id;  }
        inline double getX()   const { return x;   }
        inline double getY()   const { return y;   }
        inline double getZ()   const { return z;   }

        // Setters
        inline void setLon(double newLon) { lon = newLon; }
        inline void setLat(double newLat) { lat = newLat; }
        inline void setId(uint64_t newId)      {id = newId;}

        std::vector<Road> outgoingEdges;
        std::vector<uint64_t> incomingEdgeNodeIds;
        
        bool operator==(const Node& other) const 
        {
            return id == other.id;
        }

        // double g = std::numeric_limits<double>::infinity();
        // double rhs = std::numeric_limits<double>::infinity();

        Node(std::uint64_t initId, double iLon, double iLat, double x, double y);
        Node();
        ~Node();
    private:
        // void projectNodes();

        std::uint64_t id;

        double lon;
        double lat;

        double x, y, z;

        const double MAP_ORIGIN_LAT = 28.6254006;
        const double MAP_ORIGIN_LON = -81.3863168;
};

#endif