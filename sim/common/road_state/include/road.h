#ifndef ROAD_H
#define ROAD_H

#include <cstdint>
#include <string>
#include <vector>

// Per-lane permitted movements through the intersection at the end of an
// edge. Lane 0 is the leftmost lane (closest to the median), matching both
// the render layout and the left-to-right order of OSM turn:lanes strings.
namespace TurnLane {
    constexpr uint8_t Left    = 1 << 0;
    constexpr uint8_t Through = 1 << 1;
    constexpr uint8_t Right   = 1 << 2;
    // Set on lanes whose movement bits were filled by
    // Network::assignInferredTurnLanes rather than parsed from the tag, so
    // a later pass may recompute them when the intersection changes. Lanes
    // that came from the tag itself never carry it.
    constexpr uint8_t Inferred = 1 << 3;

    // Parse an OSM turn:lanes value ("left|through|through;right") into
    // per-lane masks, tokens left to right. Empty and "none" tokens are
    // unmarked lanes and parse to 0 -- no movement data, which
    // Network::assignInferredTurnLanes fills from the downstream
    // intersection. Returns empty when the tag does not describe exactly
    // 'lanes' lanes -- a desynced map is worse than none, since the
    // inference pass covers the gap.
    std::vector<uint8_t> fromOsmString(const std::string& spec, int lanes);

    // Format per-lane masks back into that syntax ("left;through|right").
    // Lanes with an empty mask become empty tokens (OSM "no restriction").
    std::string toOsmString(const std::vector<uint8_t>& masks);
}

// One vertex of an edge's real-world centerline polyline (OSM geometry_xy),
// in the same projected map coordinates as Node x/y (y already sign-flipped).
// 's' is the cumulative arc length in meters from the first point.
// 'z' is the elevation in meters (0 = ground) assigned by
// Network::applyVerticality from the edge's OSM layer; it is deliberately the
// LAST member so existing {x, y, s} aggregate initializers stay valid.
struct RoadGeomPoint {
    double x;
    double y;
    double s;
    double z = 0.0;
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
        inline int getLayer() const               { return layer; }
        inline bool isLink() const                { return linkRoad; }

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
        inline void setLayer(int l)          { layer = l; }
        inline void setIsLink(bool v)        { linkRoad = v; }
        // Used by Network::splitDirectedEdge to shorten an edge in place so
        // existing Road* pointers (e.g. VehicleState::currentEdge) stay valid.
        inline void setDest(uint64_t d)      { destId = d; }
        inline void setLength(double le)     { length = le; }

        // Per-lane turn permissions (TurnLane flags, index 0 = leftmost lane).
        // Empty = no data: every movement is allowed from every lane. Lanes
        // parsed from OSM turn:lanes are authoritative; lanes carrying
        // TurnLane::Inferred (and whole maps with fromOsm=false) may be
        // recomputed whenever the network changes.
        void setLaneTurns(std::vector<uint8_t> turns, bool fromOsm);
        // Drops the turn map entirely (e.g. the user cleared an explicit
        // value) so the next assignInferredTurnLanes pass owns this edge.
        inline void clearLaneTurns() { laneTurns.clear(); laneTurnsFromOsm = false; }
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

        // Writes the vertical profile onto the stored centerline: z holds zMid
        // (the edge's own layer elevation) over the span and ramps to the
        // endpoint node elevations zStart/zEnd within rampLen meters of each
        // end (smoothstep). flatStart/flatEnd hold the endpoint elevation for
        // that many meters BEFORE the ramp begins -- the intersection setback
        // radius -- so the road face the visualizer trims at the junction box
        // sits exactly at the junction pavement's height instead of partway up
        // the ramp. Extra vertices are inserted inside the ramp zones so the
        // profile survives on long straight segments that only have two
        // polyline points. Elevations are meters; arc lengths stay 2D.
        // Safe to re-run after layer/topology edits: z is recomputed from
        // scratch and cut vertices landing on existing ones are skipped.
        void applyVerticalProfile(double zStart, double zMid, double zEnd, double rampLen,
                                  double flatStart = 0.0, double flatEnd = 0.0);

        // Position + unit tangent on the centerline at 'dist' meters along the
        // edge. 'dist' is measured against the simulator's edge length and is
        // remapped proportionally onto the polyline's own arc length, so it
        // stays consistent with vehicle positions even if the two differ
        // slightly. Returns false when no usable geometry is stored.
        // 'outZ' receives the interpolated elevation (meters, 0 = ground).
        bool samplePointAt(double dist, double& outX, double& outY,
                           double& outTanX, double& outTanY, double& outZ) const;

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
        int layer = 0; // OSM vertical layer: 0 ground, +1 overpass, -1 underpass
        // OSM *_link (ramp / turn slip), not a full roadway: its presence at a
        // node must not make the junction look big enough to signalize.
        bool linkRoad = false;
        int currentVolume = 0;
        std::vector<RoadGeomPoint> geometry; // empty = straight line
        double geometryLength = 0.0;         // total polyline arc length (m)
        std::vector<uint8_t> laneTurns;      // empty = unrestricted
        bool laneTurnsFromOsm = false;
};

#endif
