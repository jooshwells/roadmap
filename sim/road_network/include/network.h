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
        
        void addNode(std::uint64_t id, double lat, double lon, double x, double y);
        
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
        void addDirectedEdge(uint64_t fromId, uint64_t toId, double dist, double speedLimit, int lanes,
                             std::vector<RoadGeomPoint> geometry = {}, int layer = 0);

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

        // Assigns vertical elevations from the OSM layer data, run once after
        // all edges are loaded. Each non-ground edge's centerline holds
        // layer * layerHeightM over its span and ramps (smoothstep) to its
        // endpoint node elevations within rampLengthM of each end. A node's
        // elevation is the incident-edge layer closest to ground, so a bridge
        // deck ramps up inside its own span while ground-level roads through
        // the same endpoint node stay flat; where two elevated edges meet the
        // node sits at deck height and the spans stay flush.
        void applyVerticality(double layerHeightM = 7.0, double rampLengthM = 50.0);
        void visualizeNetwork();

        const std::unordered_map<uint64_t, Node>& getNodes() const { return nodes; }

        Network();
        ~Network();

    private:
        std::unordered_map<uint64_t, Node> nodes;
        std::vector<uint64_t> nodeIds;
        uint64_t numNodes;
        uint64_t nextEdgeId = 1;
        

};

#endif