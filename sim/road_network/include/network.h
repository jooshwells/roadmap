#ifndef NETWORK_INTIALIZER_H
#define NETWORK_INTIALIZER_H

#include "node.h"
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <random>


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
        
        void visualizeNetworkForPython();
        // 'geometry' is the optional OSM centerline polyline (map meters, y
        // sign-flipped to match Node coords). It may arrive in either point
        // order; it is oriented from->to and endpoint-snapped before storage.
        void addDirectedEdge(uint64_t fromId, uint64_t toId, double dist, double speedLimit, int lanes,
                             std::vector<RoadGeomPoint> geometry = {});
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