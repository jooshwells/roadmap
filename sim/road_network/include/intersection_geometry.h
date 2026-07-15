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
}
