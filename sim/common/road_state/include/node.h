#ifndef NODE_H
#define NODE_H

#include <cstdint>
#include <vector>
#include "road.h"
#include <GeographicLib/LocalCartesian.hpp>

class Node
{
    public:
        // Getters
        inline double getLon() { return lon; }
        inline double getLat() { return lat; }
        inline int getId()    { return id;  }
        inline double getX()  { return x;   }
        inline double getY()  { return y;   }

        // Setters
        inline void setLon(double newLon) { lon = newLon; }
        inline void setLat(double newLat) { lat = newLat; }
        inline void setId(int newId)     {id = newId;}

        std::vector<Road> outgoingEdges;

        Node(std::uint64_t initId, double iLon, double iLat);
        Node();
        ~Node();
    private:
        void projectNodes();

        std::uint64_t id;

        double lon;
        double lat;

        double x, y, z;

        const double MAP_ORIGIN_LAT = 28.6254006;
        const double MAP_ORIGIN_LON = -81.3863168;
};

#endif