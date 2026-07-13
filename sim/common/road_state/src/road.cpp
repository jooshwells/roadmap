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
    // degenerate (NaN) tangent, then accumulate arc length (2D -- slopes do
    // not stretch the arc measure the setbacks and vehicles use).
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
        geometry.push_back({ p.x, p.y, geometryLength, p.z });
    }
    if (geometry.size() < 2)
    {
        geometry.clear();
        geometryLength = 0.0;
    }
}

void Road::applyVerticalProfile(double zStart, double zMid, double zEnd, double rampLen,
                                double flatStart, double flatEnd)
{
    if (geometry.size() < 2 || geometryLength <= 0.0) return;

    if (zStart == zMid && zMid == zEnd)
    {
        for (RoadGeomPoint& p : geometry) p.z = zMid;
        return;
    }

    const double L = geometryLength;

    // Each end zone = flat run at the node's elevation (the junction setback)
    // followed by the smoothstep ramp. Zones are capped at half the edge; on
    // short edges the flat run yields to the ramp so the profile never jumps.
    double fA = 0.0, rA = 0.0, fB = 0.0, rB = 0.0;
    if (zStart != zMid)
    {
        const double zone = std::min(flatStart + rampLen, L * 0.5);
        fA = std::min(std::max(flatStart, 0.0), zone * 0.5);
        rA = zone - fA;
    }
    if (zEnd != zMid)
    {
        const double zone = std::min(flatEnd + rampLen, L * 0.5);
        fB = std::min(std::max(flatEnd, 0.0), zone * 0.5);
        rB = zone - fB;
    }

    auto smooth = [](double t) {
        t = std::clamp(t, 0.0, 1.0);
        return t * t * (3.0 - 2.0 * t);
    };
    auto zAt = [&](double s) -> double {
        if (rA > 0.0)
        {
            if (s <= fA)      return zStart;
            if (s < fA + rA)  return zStart + (zMid - zStart) * smooth((s - fA) / rA);
        }
        if (rB > 0.0)
        {
            if (s >= L - fB)     return zEnd;
            if (s > L - fB - rB) return zEnd + (zMid - zEnd) * smooth((L - fB - s) / rB);
        }
        return zMid;
    };

    // Arc positions to add as vertices so the smoothstep is actually sampled
    // inside the ramps -- a straight bridge often has only its two endpoints.
    std::vector<double> cuts;
    const int STEPS = 6;
    auto addRampCuts = [&](double s0, double s1) {
        for (int k = 0; k <= STEPS; k++)
            cuts.push_back(s0 + (s1 - s0) * k / STEPS);
    };
    if (rA > 0.0) addRampCuts(fA, fA + rA);
    if (rB > 0.0) addRampCuts(L - fB - rB, L - fB);
    std::sort(cuts.begin(), cuts.end());
    cuts.erase(std::unique(cuts.begin(), cuts.end(),
        [](double a, double b) { return std::abs(a - b) < 1e-6; }), cuts.end());

    std::vector<RoadGeomPoint> out;
    out.reserve(geometry.size() + cuts.size());
    size_t ci = 0;
    for (size_t i = 0; i + 1 < geometry.size(); i++)
    {
        const RoadGeomPoint a = geometry[i];
        const RoadGeomPoint b = geometry[i + 1];
        out.push_back(a);
        const double segLen = b.s - a.s;
        while (ci < cuts.size() && cuts[ci] <= a.s + 1e-6) ci++;
        for (; ci < cuts.size() && cuts[ci] < b.s - 1e-6; ci++)
        {
            const double t = (cuts[ci] - a.s) / segLen;
            out.push_back({ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, cuts[ci], 0.0 });
        }
    }
    out.push_back(geometry.back());

    for (RoadGeomPoint& p : out) p.z = zAt(p.s);
    geometry = std::move(out);
}

bool Road::samplePointAt(double dist, double& outX, double& outY,
                         double& outTanX, double& outTanY, double& outZ) const
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
    outZ = a.z + (b.z - a.z) * t;
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