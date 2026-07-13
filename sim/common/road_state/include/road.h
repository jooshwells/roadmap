#ifndef ROAD_H
#define ROAD_H

#include <cstdint>
#include <vector>

// Per-lane permitted movements through the intersection at the end of an
// edge. Lane 0 is the leftmost lane (closest to the median), matching both
// the render layout and the left-to-right order of OSM turn:lanes strings.
namespace TurnLane {
    constexpr uint8_t Left    = 1 << 0;
    constexpr uint8_t Through = 1 << 1;
    constexpr uint8_t Right   = 1 << 2;
}

// One vertex of an edge's real-world centerline polyline (OSM geometry_xy),
// in the same projected map coordinates as Node x/y (y already sign-flipped).
// 's' is the cumulative arc length in meters from the first point.
struct RoadGeomPoint {
    double x;
    double y;
    double s;
};

class Road {

    public:
        // Getters
        inline std::uint64_t getEdgeId() const    { return edgeId; } 
        inline std::uint64_t getDest() const      { return destId; }
        inline double getSpeedLimit() const       { return speedLimit; }
        inline int getLanes() const               { return lanes; }
        inline double getLength() const           { return length; }
        inline uint64_t getOriginId() const { return originId; }

        // Setters
        inline void setSpeedLimit(double sL) { speedLimit = sL; }
        // Runtime lane edits invalidate any per-lane turn map; clearing it
        // falls back to "all movements allowed" until inference re-runs.
        inline void setLanes(int l)
        {
            lanes = l;
            if (static_cast<int>(laneTurns.size()) != l)
            {
                laneTurns.clear();
                laneTurnsFromOsm = false;
            }
        }

        // Per-lane turn permissions (TurnLane flags, index 0 = leftmost lane).
        // Empty = no data: every movement is allowed from every lane. Masks
        // parsed from OSM turn:lanes are authoritative; inferred ones may be
        // recomputed whenever the network changes.
        void setLaneTurns(std::vector<uint8_t> turns, bool fromOsm);
        inline const std::vector<uint8_t>& getLaneTurns() const { return laneTurns; }
        inline bool hasLaneTurnData() const { return !laneTurns.empty(); }
        inline bool isLaneTurnsFromOsm() const { return laneTurnsFromOsm; }
        bool laneAllows(int lane, uint8_t movement) const;
        // Closest lane to fromLane permitting 'movement' (fromLane itself if
        // it qualifies); -1 when there is no turn map or no such lane.
        int nearestLaneAllowing(int fromLane, uint8_t movement) const;

        // Curved centerline (empty for edges without OSM shape data, e.g.
        // runtime-created roads -- consumers fall back to a straight line).
        // Points are oriented origin -> dest and cumulative arc lengths are
        // filled in by setGeometry.
        void setGeometry(std::vector<RoadGeomPoint> pts);
        inline const std::vector<RoadGeomPoint>& getGeometry() const { return geometry; }
        inline bool hasCurveGeometry() const { return geometry.size() >= 2; }

        // Position + unit tangent on the centerline at 'dist' meters along the
        // edge. 'dist' is measured against the simulator's edge length and is
        // remapped proportionally onto the polyline's own arc length, so it
        // stays consistent with vehicle positions even if the two differ
        // slightly. Returns false when no usable geometry is stored.
        bool samplePointAt(double dist, double& outX, double& outY,
                           double& outTanX, double& outTanY) const;

        // volume tracking and dynamic cost for pathfinding
        void addVehicle() { currentVolume++; }
        void removeVehicle() { if (currentVolume > 0) currentVolume--; }
        int getCurrentVolume() const { return currentVolume; }
        double getDynamicCost() const;

        // Updated constructor signature
        Road(uint64_t eId, uint64_t origin, uint64_t dest, double le, double sl, int l);
        ~Road();

    private:
        uint64_t edgeId; // NEW: The unique identifier for this road segment
        uint64_t destId;
        uint64_t originId;
        double length;
        double speedLimit;
        int lanes;
        int currentVolume = 0;
        std::vector<RoadGeomPoint> geometry; // empty = straight line
        double geometryLength = 0.0;         // total polyline arc length (m)
        std::vector<uint8_t> laneTurns;      // empty = unrestricted
        bool laneTurnsFromOsm = false;
};

#endif