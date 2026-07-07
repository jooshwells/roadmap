#pragma once

// Shared intersection geometry used by both the road visualizer (Unreal cm) and
// the vehicle render-state builder (map meters). Pure C++ on purpose -- no
// Unreal dependencies -- so TrafficSimulation stays engine-agnostic.
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
#include <cstdint>
#include <unordered_set>

namespace RoadIntersectionUtil
{
    // Keep in sync with the 350 cm lane width used by the road HISM scaling.
    constexpr float LaneWidthMeters = 3.5f;

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
    inline float GetNodeSetbackMeters(Network* Net, const Node& N, float MedianGapMeters)
    {
        if (!IsIntersectionNode(N)) return 0.0f;
        return MedianGapMeters + GetMaxLanesAtNode(Net, N) * LaneWidthMeters;
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
}
