#ifndef NODE_H
#define NODE_H

#include <cstdint>
#include <vector>
#include <limits>
#include <string> 
#include "road.h"

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
        // Elevation in meters (0 = ground), assigned by Network::applyVerticality.
        inline void setZ(double newZ) { z = newZ; }

        std::vector<Road> outgoingEdges;
        std::vector<uint64_t> incomingEdgeNodeIds; 
        std::vector<uint64_t> minorRoadOriginIds; // classify roads as minor for right of way hierarchy
        
        bool operator==(const Node& other) const 
        {
            return id == other.id;
        }

        // pathfinding stuff 
        double g = std::numeric_limits<double>::infinity();
        double rhs = std::numeric_limits<double>::infinity();

        // intersection stuff
        enum IntersectionType { PASS_THROUGH, FOUR_WAY_STOP, TRAFFIC_LIGHT, YIELD_STOP };
        IntersectionType type = PASS_THROUGH;

        // True when 'type' came from the dataset (signal/stop/yield in the
        // node JSONL) rather than from applyDefaultTrafficControls. Explicit
        // controls survive Network::refreshTrafficControlAt after runtime
        // road edits; defaulted ones are recomputed from the new topology.
        bool controlFromData = false;

        // True when 'type' was raised to TRAFFIC_LIGHT by
        // Network::harmonizeClusteredControls -- this node's own warrant did
        // not signalize it, but it shares one physical junction (a cluster of
        // nodes joined by short internal legs) with a node that IS a signal,
        // so it joins the coordinated plan instead of yielding into a road the
        // light already meters. Only ever set on defaulted nodes, and kept
        // distinct from a genuine signal so refreshTrafficControlAt can revert
        // it to its base control and re-derive the promotion from scratch --
        // one promotion never seeds another.
        bool promotedToSignal = false;

        // updated constructor
        Node(std::uint64_t initId, double iLon, double iLat, double x, double y, const std::string& typeStr);
        
        Node();
        ~Node();

    private:
        std::uint64_t id;

        double lon;
        double lat;

        double x, y, z;

        const double MAP_ORIGIN_LAT = 28.6254006;
        const double MAP_ORIGIN_LON = -81.3863168;
};

#endif