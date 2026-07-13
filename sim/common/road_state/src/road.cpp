#include "road.h"
#include <cstdint>
#include <algorithm> // For std::max
#include <cmath>     // For std::pow

// Added edgeId (eId) to the initializer list
Road::Road(uint64_t eId, uint64_t origin, uint64_t dest, double dist, double sL, int l)
    : edgeId(eId), originId(origin), destId(dest), length(dist), speedLimit(sL), lanes(l) {}

Road::~Road() {}

void Road::setGeometry(std::vector<RoadGeomPoint> pts)
{
    // Drop consecutive duplicates so zero-length segments never produce a
    // degenerate (NaN) tangent, then accumulate arc length.
    geometry.clear();
    geometryLength = 0.0;
    for (const RoadGeomPoint& p : pts)
    {
        if (!geometry.empty())
        {
            const double dx = p.x - geometry.back().x;
            const double dy = p.y - geometry.back().y;
            const double d = std::sqrt(dx * dx + dy * dy);
            if (d < 1e-6) continue;
            geometryLength += d;
        }
        geometry.push_back({ p.x, p.y, geometryLength });
    }
    if (geometry.size() < 2)
    {
        geometry.clear();
        geometryLength = 0.0;
    }
}

bool Road::samplePointAt(double dist, double& outX, double& outY,
                         double& outTanX, double& outTanY) const
{
    if (geometry.size() < 2 || geometryLength <= 0.0 || length <= 0.0) return false;

    // Remap sim-length position onto the polyline's own arc length.
    double s = std::clamp(dist / length, 0.0, 1.0) * geometryLength;

    // Find the segment containing s (last segment for s == geometryLength).
    size_t i = 0;
    while (i + 2 < geometry.size() && geometry[i + 1].s <= s) i++;

    const RoadGeomPoint& a = geometry[i];
    const RoadGeomPoint& b = geometry[i + 1];
    const double segLen = b.s - a.s;
    const double t = (segLen > 0.0) ? (s - a.s) / segLen : 0.0;

    outX = a.x + (b.x - a.x) * t;
    outY = a.y + (b.y - a.y) * t;
    outTanX = (b.x - a.x) / segLen;
    outTanY = (b.y - a.y) / segLen;
    return true;
}

void Road::setLaneTurns(std::vector<uint8_t> turns, bool fromOsm)
{
    if (static_cast<int>(turns.size()) != lanes) return; // reject desynced maps
    laneTurns = std::move(turns);
    laneTurnsFromOsm = fromOsm;
}

bool Road::laneAllows(int lane, uint8_t movement) const
{
    if (laneTurns.empty()) return true; // no data: fail open
    if (lane < 0 || lane >= static_cast<int>(laneTurns.size())) return true;
    return (laneTurns[lane] & movement) != 0;
}

int Road::nearestLaneAllowing(int fromLane, uint8_t movement) const
{
    if (laneTurns.empty()) return -1;
    const int n = static_cast<int>(laneTurns.size());
    fromLane = std::max(0, std::min(fromLane, n - 1));
    if (laneTurns[fromLane] & movement) return fromLane;

    for (int d = 1; d < n; d++) {
        const int left = fromLane - d;
        if (left >= 0 && (laneTurns[left] & movement)) return left;
        const int right = fromLane + d;
        if (right < n && (laneTurns[right] & movement)) return right;
    }
    return -1;
}

double Road::getDynamicCost() const {
    // Capacity roughly equals physical space: length / 7 meters per car * lanes.
    // We enforce a minimum capacity of 1.0 to prevent division by zero on tiny edge segments.
    double capacity = std::max(1.0, (length / 7.0) * lanes);
    
    // Base cost is the time it takes to traverse at the speed limit
    double freeFlowTime = length / speedLimit;
    
    // Ratio of current cars to maximum capacity
    double ratio = currentVolume / capacity;
    
    // BPR Function: Cost = BaseTime * (1 + 0.15 * (Volume/Capacity)^4)
    return freeFlowTime * (1.0 + 0.15 * std::pow(ratio, 4.0));
}