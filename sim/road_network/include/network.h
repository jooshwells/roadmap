#ifndef NETWORK_INTIALIZER_H
#define NETWORK_INTIALIZER_H

#include "node.h"
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <random>
#include <string>


class Network {

    public:
        
        // Combined signature: takes offline X/Y AND the intersection type string
        // typeStr defaults to "none" (uncontrolled / PASS_THROUGH) so
        // runtime-created nodes need no traffic-control decision.
        void addNode(std::uint64_t id, double lat, double lon, double x, double y, const std::string& typeStr = "none");
        
        /**
         * WARNING!!!!
         * DO NOT STORE THESE POINTERS IN VARIABLES!!
         * Since these pointers are pointing to nodes within an unordered map,
         * if the map grows in size, the actual pointers to the objects may be
         * shuffled around. 
         */
        Node* getNode(uint64_t id);
        Node* getRandomNode(std::mt19937& rng);
        
        // Exports the graph geometry as the CSV the Python heatmap pipeline
        // reads (columns: source,target,length,source_x,source_y,edge_id).
        // Pass an absolute path so the file lands where the pipeline expects
        // it regardless of the process working directory.
        void visualizeNetworkForPython(const std::string& outputPath = "wf_network_graph.csv");
        // 'geometry' is the optional OSM centerline polyline (map meters, y
        // sign-flipped to match Node coords). It may arrive in either point
        // order; it is oriented from->to and endpoint-snapped before storage.
        // 'layer' is the OSM vertical layer (0 ground, +1 overpass, -1
        // underpass) used by applyVerticality to elevate the edge.
        // 'laneTurns' is the optional per-lane turn map parsed from OSM
        // turn:lanes (TurnLane flags, one entry per lane, left to right);
        // pass empty when the tag is null and assignInferredTurnLanes will
        // fill the gap. Individual 0 entries are unmarked lanes ("left||"),
        // which the same pass completes from the downstream intersection.
        // 'isLink' marks OSM *_link edges (ramps / turn slips); see
        // Road::isLink for how traffic-control defaulting treats them.
        void addDirectedEdge(uint64_t fromId, uint64_t toId, double dist, double speedLimit, int lanes,
                             std::vector<RoadGeomPoint> geometry = {}, int layer = 0,
                             std::vector<uint8_t> laneTurns = {}, bool isLink = false);

        // Splits the directed edge from->to at map point (x, y): the existing
        // Road is shortened IN PLACE to end at newNodeId (so Road* pointers
        // held by vehicles stay valid) and a new edge newNodeId->to inherits
        // its speed limit, lane count, layer, and the rest of the centerline.
        // Creates newNodeId at (x, y) if it does not exist yet. The split
        // point is projected onto the edge's centerline and the sim length is
        // divided proportionally. Returns false if either node or the edge is
        // missing.
        bool splitDirectedEdge(uint64_t fromId, uint64_t toId, uint64_t newNodeId, double x, double y);

        // Removes the directed edge from->to. Erasing from outgoingEdges
        // shifts the remaining Roads, so any cached Road* into that node must
        // be re-resolved afterwards (see TrafficSimulation::
        // RefreshVehicleEdgePointers). Returns false if the edge is missing.
        bool removeDirectedEdge(uint64_t fromId, uint64_t toId);

        // Removes a node only when nothing connects to it anymore (no
        // outgoing edges and no incoming ones). Returns false when the node
        // is missing or still connected, so callers can try unconditionally
        // after deleting edges.
        bool removeNodeIfIsolated(uint64_t id);

        // Canonical elevation constants, shared by the sim build and the
        // frontend so both sides always agree on deck heights and ghost
        // previews. medianGapM must match the visualizer's MedianGapCm.
        static constexpr double DefaultLayerHeightM = 7.0;
        static constexpr double DefaultRampLengthM = 50.0;
        static constexpr double DefaultMedianGapM = 1.0;

        // Assigns vertical elevations from the OSM layer data, run after all
        // edges are loaded (safe to re-run after runtime road edits). Each
        // non-ground edge's centerline holds layer * layerHeightM over its
        // span and ramps (smoothstep) to its endpoint node elevations within
        // rampLengthM of each end, after holding the node's elevation flat
        // through the junction setback radius (derived from medianGapM and
        // the lane counts at the node) so road faces meet the junction
        // pavement flush. A node's elevation is the layer whose road
        // continues THROUGH it (edges toward 2+ distinct neighbours; most
        // neighbours wins, ties go nearest the ground), so a viaduct stays at
        // deck height where a ramp joins it and the ramp climbs to meet it;
        // nodes without a through road use the incident layer closest to the
        // ground, so a bridge that simply ends ramps down inside its own span.
        void applyVerticality(double layerHeightM = DefaultLayerHeightM,
                              double rampLengthM = DefaultRampLengthM,
                              double medianGapM = DefaultMedianGapM);
        void visualizeNetwork();
        void applyDefaultTrafficControls();
        void calculateIntersectionPriorities();

        // Re-derives one node's traffic control after a runtime topology
        // change (road drawn, edge split/deleted, lanes/speed edited), giving
        // it exactly what the load-time defaulting pipeline would: defaulted
        // nodes are reclassified from scratch (a drawn crossing becomes a
        // stop/signal immediately instead of after the next reload), explicit
        // dataset controls are kept, and the yield minor-road list is
        // recomputed either way. Missing ids are ignored.
        void refreshTrafficControlAt(uint64_t nodeId);
        // Fill in per-lane turn permissions for every edge that has no OSM
        // turn:lanes data, from the movements geometrically available at the
        // edge's destination node. Idempotent; re-run after runtime road
        // edits so inferred maps track the current network shape.
        void assignInferredTurnLanes();
        void resetPathfindingState();

        const std::unordered_map<uint64_t, Node>& getNodes() const { return nodes; }

        Network();
        ~Network();

    private:
        std::unordered_map<uint64_t, Node> nodes;
        std::vector<uint64_t> nodeIds;
        uint64_t numNodes;
        uint64_t nextEdgeId = 1;

        // Per-node bodies of applyDefaultTrafficControls /
        // calculateIntersectionPriorities, shared with
        // refreshTrafficControlAt so runtime edits and load-time
        // classification can never disagree.
        void applyDefaultControlAt(Node& node);
        void assignYieldPriorityAt(Node& node);
        
};

#endif