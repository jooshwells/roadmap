#pragma once

// Shared intersection geometry used by the physics engine (stop lines), the
// road visualizer (Unreal cm), and the vehicle render-state builder (map
// meters). Pure C++ on purpose -- no Unreal dependencies -- and it lives in
// the sim so the stop line cars brake for is, by construction, the same arc
// position where the road visuals end and the stop sign / signal is planted.
//
// The model: an intersection is where 3+ distinct roadways meet. Each edge
// entering it should stop short of the node center by a "setback" radius large
// enough to clear the corridors of the crossing roads. Degree-1 (dead ends) and
// degree-2 nodes (geometry points in the middle of a continuous road) get a
// setback of 0 so those roads stay flush and uninterrupted.

#include "network.h"
#include "node.h"
#include "road.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_set>

namespace RoadIntersectionUtil
{
    enum class TurnDir { Left, Through, Right };

    // Classify the movement from a chord heading v1 into a chord heading v2.
    // Single source of truth for "what counts as a turn": the physics uses it
    // to pick target lanes and yield rules, the network uses it to infer
    // per-lane turn permissions, and the renderer uses it to choose which
    // lane a turning car sweeps into -- if these disagreed, a car could be
    // steered into a lane its rendered turn never reaches. Vectors are
    // normalized so edge length doesn't matter; anything within ~20 degrees
    // of straight ahead is a through movement. Node coords store y negated
    // from the geographic source (see NetworkBuilder), which flips the cross
    // product's handedness: positive cross = right turn here.
    inline TurnDir ClassifyTurn(double v1x, double v1y, double v2x, double v2y)
    {
        const double len1 = std::sqrt(v1x * v1x + v1y * v1y);
        const double len2 = std::sqrt(v2x * v2x + v2y * v2y);
        if (len1 < 1e-9 || len2 < 1e-9) return TurnDir::Through;

        const double sinTheta = (v1x * v2y - v1y * v2x) / (len1 * len2);
        const double cosTheta = (v1x * v2x + v1y * v2y) / (len1 * len2);

        constexpr double SinThroughLimit = 0.342; // sin(20 deg)
        if (cosTheta > 0.0 && std::abs(sinTheta) < SinThroughLimit) return TurnDir::Through;
        return sinTheta > 0.0 ? TurnDir::Right : TurnDir::Left;
    }

    // --- Relative through-continuation recovery ------------------------
    // The strict cone above calls anything past ~20 degrees a turn. But a
    // gently CURVED main road can deflect further than that at a junction --
    // its endpoint tangents are single polyline chords whose angles compound
    // across the node -- while still being the road's continuation, not a
    // turn. When the cone finds no through at an approach, its straightest
    // FORWARD exit is promoted to Through if it sits within this wider ceiling
    // AND clearly beats the next-straightest forward exit. So a curved arterial
    // keeps its through, while a symmetric Y-fork (two similar shallow angles)
    // still resolves to two turns. Shared by the lane-turn inference
    // (assignInferredTurnLanes) and the physics/route tangent classifier
    // (ClassifyTurnAtNodeTangent) so the lane a car is guided into and the
    // movement it is judged to make can never disagree. Tunable: raise the
    // ceiling to catch sharper curves, raise the margin to be stricter about
    // what counts as a clear continuation.
    constexpr double RelativeThroughCeilingRad     = 0.698; // 40 deg
    constexpr double RelativeThroughClearMarginRad = 0.262; // 15 deg

    // Signed deflection (radians) of the turn from heading v1 to heading v2:
    // magnitude is the turn angle, sign follows ClassifyTurn (positive =
    // right here). Returns 0 for a degenerate vector.
    inline double SignedDeflection(double v1x, double v1y, double v2x, double v2y)
    {
        const double len1 = std::sqrt(v1x * v1x + v1y * v1y);
        const double len2 = std::sqrt(v2x * v2x + v2y * v2y);
        if (len1 < 1e-9 || len2 < 1e-9) return 0.0;
        const double sinTheta = (v1x * v2y - v1y * v2x) / (len1 * len2);
        const double cosTheta = (v1x * v2x + v1y * v2y) / (len1 * len2);
        return std::atan2(sinTheta, cosTheta);
    }

    // Comfortable corner speed (m/s) for a turn of deflection |DeflRad| through
    // a junction whose pavement/stop line sits SetbackMeters from the node
    // center. A turning car sweeps an arc tangent to its approach and exit
    // legs; approximating that arc as a fillet whose tangent points sit one
    // setback back from the corner gives radius R = Setback / tan(theta/2). The
    // fastest a driver holding lateral accel ALat can take radius R is
    // sqrt(ALat * R). Movements inside the through cone (|defl| < ~20 deg,
    // matching ClassifyTurn's SinThroughLimit) get no reduction; otherwise the
    // result is clamped to [MinCorner, ExitLimit] so a hairpin never crawls and
    // a gentle bend never exceeds the road it enters. This replaces the old
    // flat left=0.70 / right=0.60 category multiplier so turn speed scales with
    // the ACTUAL geometry. Single source of truth shared by
    // applyJunctionTargetSpeed (the live desired speed) and
    // estimateCrossingSeconds (gap-acceptance timing) so the two never drift.
    //
    // Calibration note: the lateral-accel budget (latAccel, per driver) is set
    // well above a textbook comfort figure on purpose. A strictly realistic
    // ~3 m/s^2 makes a 90 deg turn through a 2-lane box (R ~ 8 m) work out to
    // ~4.9 m/s -- physically right, but roughly half the pace the old flat
    // multiplier gave, which read as a regression in how quickly traffic turns.
    // The tuned budget keeps the geometry GRADING (sharper turn -> slower) while
    // landing moderate turns near the old pacing and gentle ones above it.
    // Floor matches the old right-turn floor so no movement is slower than before.
    constexpr float DefaultMinCornerSpeed = 5.0f; // m/s, ~11 mph (old right-turn floor)

    inline float CornerSpeed(double DeflRad, float SetbackMeters, float ALat,
                             float ExitLimit, float MinCorner = DefaultMinCornerSpeed)
    {
        const double a = std::abs(DeflRad);
        constexpr double ThroughConeRad = 0.349; // ~20 deg, matches SinThroughLimit
        if (a < ThroughConeRad) return ExitLimit;
        // Degree-1/2 nodes report a setback of 0 (no junction box), which would
        // collapse the radius to nothing and floor a car mid-road at a sharp
        // polyline bend. Give those a small plausible fillet instead. Every real
        // intersection is already >= MedianGap + 1*LaneWidth = 4.5 m, so this
        // floor never changes junction behavior.
        const double S = std::max(SetbackMeters, 4.0f);
        // Cap the half-angle short of 90 deg so tan() stays finite for U-turns;
        // the clamp below floors whatever tiny radius a near-180 turn implies.
        const double half = std::min(a, 2.70) * 0.5; // cap ~155 deg
        const double denom = std::tan(half);
        const double R = (denom > 1e-4) ? std::max(0.5, S / denom) : 0.5;
        const float v = std::sqrt(std::max(0.0f, ALat) * static_cast<float>(R));
        // Never floor above the entered road's limit (low-speed streets).
        const float lo = std::min(MinCorner, ExitLimit);
        return std::clamp(v, lo, ExitLimit);
    }

    // True when a candidate exit (its |deflection| = CandidateAbsDefl) is the
    // through-continuation, given the smallest |deflection| among the OTHER
    // forward exits at the node (BestOtherForwardAbsDefl; pass +inf when the
    // candidate is the only forward exit). The candidate must sit within the
    // ceiling and beat the runner-up by the clear margin. A candidate already
    // inside the through cone never needs this; callers only ask when the cone
    // found no through.
    inline bool PromoteExitToThrough(double CandidateAbsDefl, double BestOtherForwardAbsDefl)
    {
        if (CandidateAbsDefl > RelativeThroughCeilingRad) return false;
        return (BestOtherForwardAbsDefl - CandidateAbsDefl) >= RelativeThroughClearMarginRad;
    }

    // Movement made at route node Curr, entering from Prev and leaving toward
    // Next. Convenience wrapper so route walkers don't hand-roll the chords.
    inline TurnDir ClassifyTurnAtNode(const Node& Prev, const Node& Curr, const Node& Next)
    {
        return ClassifyTurn(Curr.getX() - Prev.getX(), Curr.getY() - Prev.getY(),
                            Next.getX() - Curr.getX(), Next.getY() - Curr.getY());
    }

    // Rank of the departing lane among its approach's turn lanes for
    // Movement: the count of lanes permitting Movement strictly left of
    // FromLane (FromRight=false, for lefts) or strictly right of it
    // (FromRight=true, for rights). Rank 0 is the outermost turn lane. A
    // wrong-lane turner still gets a rank past the legal turn lanes, so it
    // lands beside them, not on top of them. 0 when the edge has no map.
    inline int TurnLaneRankBefore(const Road* Approach, int FromLane, uint8_t Movement, bool FromRight)
    {
        if (Approach == nullptr || !Approach->hasLaneTurnData()) return 0;
        const std::vector<uint8_t>& Masks = Approach->getLaneTurns();
        const int N = static_cast<int>(Masks.size());
        int Rank = 0;
        if (FromRight)
        {
            for (int i = N - 1; i > FromLane; --i)
                if (Masks[i] & Movement) ++Rank;
        }
        else
        {
            for (int i = 0; i < FromLane && i < N; ++i)
                if (Masks[i] & Movement) ++Rank;
        }
        return Rank;
    }

    // Lane a movement lands in on its destination edge. Turns land offset by
    // the departing lane's rank among the approach's turn lanes: with dual
    // left-turn lanes the leftmost feeds lane 0 and the second feeds lane 1
    // (mirrored for dual rights), so side-by-side turners sweep into
    // DIFFERENT lanes instead of colliding in the same one. Without a turn
    // map (Approach null or unmapped) rank is 0 -- rights enter the
    // rightmost lane, lefts lane 0, as before. Through keeps its lane
    // clamped to the new road's width. Single source of truth shared by the
    // physics edge transition, the spatial hash's route-following sensors,
    // and the renderer's junction blend -- if they disagreed, a car could
    // clear one lane with its sensor and then be seated in another on top
    // of a queue it never saw.
    inline int GetArrivalLane(TurnDir Turn, int FromLane, int DestLanes, const Road* Approach = nullptr)
    {
        if (DestLanes <= 0) return 0;
        if (Turn == TurnDir::Right)
        {
            const int Rank = TurnLaneRankBefore(Approach, FromLane, TurnLane::Right, true);
            return std::clamp(DestLanes - 1 - Rank, 0, DestLanes - 1);
        }
        if (Turn == TurnDir::Left)
        {
            const int Rank = TurnLaneRankBefore(Approach, FromLane, TurnLane::Left, false);
            return std::clamp(Rank, 0, DestLanes - 1);
        }
        return std::clamp(FromLane, 0, DestLanes - 1);
    }

    // Inverse of GetArrivalLane for the renderer's entry-side blend: the
    // lane on Approach a car now seated in ArrivalLane departed from. Scans
    // the approach's lanes for the first one the forward mapping sends to
    // ArrivalLane; falls back to the outermost turn lane when none does
    // (an arrival lane produced by a clamp).
    inline int GetDepartureLane(TurnDir Turn, int ArrivalLane, int DestLanes, const Road* Approach)
    {
        const int N = (Approach != nullptr) ? std::max(1, Approach->getLanes()) : 1;
        for (int FromLane = 0; FromLane < N; ++FromLane)
            if (GetArrivalLane(Turn, FromLane, DestLanes, Approach) == ArrivalLane) return FromLane;
        if (Turn == TurnDir::Right) return N - 1;
        if (Turn == TurnDir::Left)  return 0;
        return std::clamp(ArrivalLane, 0, N - 1);
    }

    // Keep in sync with the 350 cm lane width used by the road HISM scaling.
    constexpr float LaneWidthMeters = 3.5f;

    // Empty strip between opposing directions. Single source of truth for
    // MEDIAN_GAP_METERS in TrafficSimulation.cpp and MedianGapCm in the road
    // visualizer.
    constexpr float MedianGapMeters = 1.0f;

    inline bool IsIntersectionNode(const Node& N)
    {
        std::unordered_set<uint64_t> Neighbors;
        for (const Road& E : N.outgoingEdges) Neighbors.insert(E.getDest());
        for (uint64_t InId : N.incomingEdgeNodeIds) Neighbors.insert(InId);
        return Neighbors.size() >= 3;
    }

    // Lane count of the widest edge touching N, incoming or outgoing.
    inline int GetMaxLanesAtNode(Network* Net, const Node& N)
    {
        int MaxLanes = 1;
        for (const Road& E : N.outgoingEdges)
            MaxLanes = std::max(MaxLanes, E.getLanes());

        for (uint64_t PrevId : N.incomingEdgeNodeIds)
        {
            Node* Prev = Net->getNode(PrevId);
            if (!Prev) continue;
            for (const Road& E : Prev->outgoingEdges)
            {
                if (E.getDest() == N.getId())
                {
                    MaxLanes = std::max(MaxLanes, E.getLanes());
                    break;
                }
            }
        }
        return MaxLanes;
    }

    // Distance (meters) an edge should stop short of node N. A directed edge is
    // drawn offset to the right of its centerline, spanning from MedianGap to
    // MedianGap + lanes * LaneWidth; the widest such corridor at N is how far a
    // crossing road's pavement reaches from the node center.
    inline float GetNodeSetbackMeters(Network* Net, const Node& N, float MedianGap)
    {
        if (!IsIntersectionNode(N)) return 0.0f;
        return MedianGap + GetMaxLanesAtNode(Net, N) * LaneWidthMeters;
    }

    // Shrink a pair of setbacks proportionally so they never consume more than
    // the whole edge (short connector fully inside a junction).
    inline void ClampSetbacksToLength(float Length, float& InOutStart, float& InOutEnd)
    {
        const float Total = InOutStart + InOutEnd;
        if (Total > Length && Total > 0.0f)
        {
            const float Scale = std::max(0.0f, Length) / Total;
            InOutStart *= Scale;
            InOutEnd   *= Scale;
        }
    }

    // Arc position (meters from the edge origin) of the stop line where Edge
    // enters node N: the junction-box boundary where the pavement ends. Both
    // the physics stop line and the sign/signal fixture anchor here. Clamped
    // so a short edge mostly swallowed by the junction still keeps the line
    // somewhere sensible on its back half.
    inline float GetStopLineArcPos(Network* Net, const Road& Edge, const Node& N, float MedianGap)
    {
        const float Length = static_cast<float>(Edge.getLength());
        const float Setback = GetNodeSetbackMeters(Net, N, MedianGap);
        return std::clamp(Length - Setback, Length * 0.5f, Length);
    }

    // --- Lane-drop / lane-gain tapering --------------------------------
    // The road visualizer narrows an edge over the last TaperLenMeters to
    // meet a through-continuation with fewer lanes (and mirrors it at the
    // start for lane gains). The vehicle renderer must agree on where that
    // pavement edge is, or cars in the dropping lane ride off the road for
    // the whole taper zone -- so the neighbour pick and the zone math live
    // here, shared by both. TaperLenMeters must match the visualizer's
    // TaperLengthCm default (3000 cm).
    constexpr float TaperLenMeters = 30.0f;
    constexpr double TaperAlignmentDotDefault = 0.7;
    // A lane drop of more than one lane across a single junction is almost
    // always a junction topology (a fork, or a mainline meeting a separate
    // node) rather than a genuine taper -- collapsing e.g. 3 lanes straight to
    // 1 both looks wrong and forces cars to slide across multiple lanes at the
    // approach. Cap real tapers at a single dropped lane; larger jumps stay
    // abrupt and are handled as junctions.
    constexpr int TaperMaxLaneDeltaDefault = 1;
    // Master switch for pulling VEHICLES onto the narrowed pavement
    // (GetTaperedMaxLaneAt). Keep in sync with the road visualizer's
    // bTaperLaneDrops so the drawn pavement and the cars on it agree; flip to
    // false to leave cars in their physics lane regardless of the drawn taper.
    constexpr bool TaperVehiclesEnabled = true;

    // Unit 2D direction Edge leaves its origin with (AtEnd=false) or arrives
    // at its dest with (AtEnd=true): the curve tangent when the edge has
    // shape data, the node-to-node chord otherwise.
    inline bool GetEdgeEndDirection(Network* Net, const Road& Edge, bool AtEnd,
                                    double& OutX, double& OutY)
    {
        double px, py, tx, ty, pz;
        if (Edge.samplePointAt(AtEnd ? Edge.getLength() : 0.0, px, py, tx, ty, pz))
        {
            OutX = tx;
            OutY = ty;
            return true;
        }
        Node* A = Net->getNode(Edge.getOriginId());
        Node* B = Net->getNode(Edge.getDest());
        if (!A || !B) return false;
        const double dx = B->getX() - A->getX();
        const double dy = B->getY() - A->getY();
        const double len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-9) return false;
        OutX = dx / len;
        OutY = dy / len;
        return true;
    }

    // Movement made at Curr, arriving from Prev and leaving toward Next, using
    // the connecting edges' geometry TANGENTS instead of the node-to-node
    // chords ClassifyTurnAtNode uses. A curved through road leaves its origin
    // at an angle the straight chord never sees, so the chord classifier reads
    // it as a slight turn: the through movement is then dropped from the lane
    // map (a lane gets left+right and no through) and, on a route, a genuine
    // turn can read as "through" and be granted across opposing traffic with
    // no gap check. Same lesson as legBearingAt in network.cpp. Falls back per
    // edge to the chord when shape data is missing, and to the pure chord
    // classifier when either connecting edge can't be found.
    inline TurnDir ClassifyTurnAtNodeTangent(Network* Net, uint64_t PrevId,
                                             uint64_t CurrId, uint64_t NextId)
    {
        Node* Prev = Net->getNode(PrevId);
        Node* Curr = Net->getNode(CurrId);
        Node* Next = Net->getNode(NextId);
        if (Prev == nullptr || Curr == nullptr || Next == nullptr) return TurnDir::Through;

        const Road* In = nullptr;
        for (const Road& e : Prev->outgoingEdges)
            if (e.getDest() == CurrId) { In = &e; break; }
        const Road* Out = nullptr;
        for (const Road& e : Curr->outgoingEdges)
            if (e.getDest() == NextId) { Out = &e; break; }

        double ix, iy, ox, oy;
        if (In != nullptr && Out != nullptr &&
            GetEdgeEndDirection(Net, *In,  /*AtEnd=*/true,  ix, iy) &&
            GetEdgeEndDirection(Net, *Out, /*AtEnd=*/false, ox, oy))
        {
            const TurnDir Cone = ClassifyTurn(ix, iy, ox, oy);
            if (Cone == TurnDir::Through) return Cone;

            // Relative through recovery, mirroring assignInferredTurnLanes so
            // the movement a car is judged to make matches the lane map it was
            // routed into. Without it a curved continuation is marked through
            // in the lane map but reads as a turn here, and the wrong-lane gate
            // stalls a car going straight. Enumerate this node's other forward
            // exits; promote when the candidate is the clear straightest.
            const double CandDefl = std::abs(SignedDeflection(ix, iy, ox, oy));
            if (CandDefl <= RelativeThroughCeilingRad)
            {
                double bestOther = std::numeric_limits<double>::infinity();
                for (const Road& e : Curr->outgoingEdges)
                {
                    if (e.getDest() == NextId || e.getDest() == PrevId) continue; // candidate / U-turn twin
                    double ex, ey;
                    if (!GetEdgeEndDirection(Net, e, /*AtEnd=*/false, ex, ey)) continue;
                    if (ix * ex + iy * ey <= 0.0) continue; // only forward exits compete
                    bestOther = std::min(bestOther, std::abs(SignedDeflection(ix, iy, ex, ey)));
                }
                if (PromoteExitToThrough(CandDefl, bestOther)) return TurnDir::Through;
            }
            return Cone;
        }

        return ClassifyTurnAtNode(*Prev, *Curr, *Next);
    }

    // Lane count of the through-continuation Edge visually tapers to at the
    // given end -- Edge's own lane count when there is none, or when the
    // jump is junction-sized. Among roughly-aligned neighbours (dot above
    // AlignmentDot) the one with the CLOSEST lane count wins: that is the
    // mainline continuation, not a merging ramp. Jumps larger than
    // MaxLaneDelta are junctions, not lane drops, and stay abrupt.
    inline int GetTaperNeighborLanes(Network* Net, const Road& Edge, bool AtEnd,
                                     double AlignmentDot = TaperAlignmentDotDefault,
                                     int MaxLaneDelta = TaperMaxLaneDeltaDefault)
    {
        const int OwnLanes = std::max(1, Edge.getLanes());
        double ex, ey;
        if (!GetEdgeEndDirection(Net, Edge, AtEnd, ex, ey)) return OwnLanes;

        int BestDelta = MaxLaneDelta + 1; // must land within the cap to count
        double BestDot = -1.0;
        int Result = OwnLanes;

        auto Consider = [&](const Road& Neighbor, bool NeighborAtEnd)
        {
            // A *_link (ramp / turn slip) is never the mainline continuation --
            // it just happens to leave the node roughly aligned. Letting it win
            // the pick is what tapered full roads down to a single slip lane.
            if (Neighbor.isLink()) return;
            double nx, ny;
            if (!GetEdgeEndDirection(Net, Neighbor, NeighborAtEnd, nx, ny)) return;
            const double Dot = ex * nx + ey * ny;
            if (Dot < AlignmentDot) return;
            const int L = std::max(1, Neighbor.getLanes());
            const int Delta = std::abs(L - OwnLanes);
            if (Delta < BestDelta || (Delta == BestDelta && Dot > BestDot))
            {
                BestDelta = Delta;
                BestDot = Dot;
                Result = L;
            }
        };

        if (AtEnd)
        {
            // Through-continuation out of the dest node.
            Node* DestNode = Net->getNode(Edge.getDest());
            if (!DestNode) return OwnLanes;
            for (const Road& NextEdge : DestNode->outgoingEdges)
            {
                if (NextEdge.getDest() == Edge.getOriginId()) continue; // U-turn twin
                Consider(NextEdge, false);
            }
        }
        else
        {
            // Through-predecessor into the origin node.
            Node* Origin = Net->getNode(Edge.getOriginId());
            if (!Origin) return OwnLanes;
            for (uint64_t PrevId : Origin->incomingEdgeNodeIds)
            {
                if (PrevId == Edge.getDest()) continue; // U-turn pair
                Node* Prev = Net->getNode(PrevId);
                if (!Prev) continue;
                for (const Road& Cand : Prev->outgoingEdges)
                {
                    if (Cand.getDest() == Origin->getId())
                    {
                        Consider(Cand, true);
                        break;
                    }
                }
            }
        }
        return (BestDelta > MaxLaneDelta) ? OwnLanes : Result;
    }

    // Highest drivable lane index (continuous) at arc position Dist on Edge,
    // following the same taper the road visuals draw: full width along the
    // body, sliding down to the neighbour's width across a taper zone. A car
    // whose lane offset is clamped by this rides the narrowing pavement in
    // instead of hanging off it.
    inline float GetTaperedMaxLaneAt(Network* Net, const Road& Edge, float Dist)
    {
        const int OwnLanes = std::max(1, Edge.getLanes());
        const float FullMaxLane = static_cast<float>(OwnLanes - 1);
        if (OwnLanes <= 1) return 0.0f;
        if (!TaperVehiclesEnabled) return FullMaxLane;

        const int DownLanes = GetTaperNeighborLanes(Net, Edge, true);
        const int UpLanes = GetTaperNeighborLanes(Net, Edge, false);
        const bool bTaperEnd = DownLanes < OwnLanes;
        const bool bTaperStart = UpLanes < OwnLanes;
        if (!bTaperEnd && !bTaperStart) return FullMaxLane;

        const float Length = static_cast<float>(Edge.getLength());
        float SbStart = 0.0f;
        float SbEnd = 0.0f;
        Node* A = Net->getNode(Edge.getOriginId());
        Node* B = Net->getNode(Edge.getDest());
        if (A) SbStart = GetNodeSetbackMeters(Net, *A, MedianGapMeters);
        if (B) SbEnd = GetNodeSetbackMeters(Net, *B, MedianGapMeters);
        ClampSetbacksToLength(Length, SbStart, SbEnd);
        const float DrawLen = Length - SbStart - SbEnd;
        if (DrawLen <= 0.001f) return FullMaxLane;

        // Zones shrink proportionally when both cannot fit, like the visuals.
        float StartTaper = bTaperStart ? TaperLenMeters : 0.0f;
        float EndTaper = bTaperEnd ? TaperLenMeters : 0.0f;
        const float Total = StartTaper + EndTaper;
        if (Total > DrawLen && Total > 0.0f)
        {
            const float Scale = DrawLen / Total;
            StartTaper *= Scale;
            EndTaper *= Scale;
        }

        const float T = std::clamp(Dist - SbStart, 0.0f, DrawLen);
        float WidthLanes = static_cast<float>(OwnLanes);
        if (bTaperStart && StartTaper > 0.001f && T < StartTaper)
        {
            WidthLanes = UpLanes + (OwnLanes - UpLanes) * (T / StartTaper);
        }
        else if (bTaperEnd && EndTaper > 0.001f && T > DrawLen - EndTaper)
        {
            WidthLanes = OwnLanes + (DownLanes - OwnLanes) * ((T - (DrawLen - EndTaper)) / EndTaper);
        }
        return std::max(0.0f, WidthLanes - 1.0f);
    }

    // OSM maps one physical junction between divided roads as 2-4 controlled
    // nodes a few meters apart; the short edges between them are the interior
    // of the junction, not real approaches (wide-arterial median crossings
    // run 16-25 m). Cars on such a leg already stopped -- or were released --
    // at the junction perimeter, so an internal leg gets no control gate in
    // the physics, no stop line, and no sign/signal fixture. Single source of
    // truth shared by the physics control gate, the fixture renderer, and the
    // intersection inspector; if these disagreed, cars would stop where
    // nothing is drawn or roll through a rendered stop line.
    constexpr float InternalJunctionLegMaxMeters = 30.0f;

    inline bool IsInternalJunctionLeg(Network* Net, const Road& Edge)
    {
        if (Edge.getLength() >= InternalJunctionLegMaxMeters) return false;
        Node* From = Net->getNode(Edge.getOriginId());
        Node* To = Net->getNode(Edge.getDest());
        return From != nullptr && To != nullptr
            && From->type != Node::PASS_THROUGH
            && To->type != Node::PASS_THROUGH;
    }
}
