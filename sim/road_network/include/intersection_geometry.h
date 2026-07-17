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

    // Movement made at route node Curr, entering from Prev and leaving toward
    // Next. Convenience wrapper so route walkers don't hand-roll the chords.
    inline TurnDir ClassifyTurnAtNode(const Node& Prev, const Node& Curr, const Node& Next)
    {
        return ClassifyTurn(Curr.getX() - Prev.getX(), Curr.getY() - Prev.getY(),
                            Next.getX() - Curr.getX(), Next.getY() - Curr.getY());
    }

    // Lane a movement lands in on its destination edge: right turns enter the
    // rightmost lane, left turns the leftmost, through keeps its lane clamped
    // to the new road's width. Single source of truth shared by the physics
    // edge transition and the spatial hash's route-following sensors -- if
    // they disagreed, a car could clear one lane with its sensor and then be
    // seated in another on top of a queue it never saw.
    inline int GetArrivalLane(TurnDir Turn, int FromLane, int DestLanes)
    {
        if (DestLanes <= 0) return 0;
        if (Turn == TurnDir::Right) return DestLanes - 1;
        if (Turn == TurnDir::Left)  return 0;
        return std::clamp(FromLane, 0, DestLanes - 1);
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
    constexpr int TaperMaxLaneDeltaDefault = 2;

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
