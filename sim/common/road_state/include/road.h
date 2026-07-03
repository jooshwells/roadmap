#ifndef ROAD_H
#define ROAD_H

#include <cstdint>
#include <vector>

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
        inline void setLanes(int l)          { lanes = l; }

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
};

#endif