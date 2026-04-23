#ifndef NETWORK_INTIALIZER_H
#define NETWORK_INTIALIZER_H

#include "node.h"
#include <unordered_map>
#include <cstdint>


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
        
        void visualizeNetworkForPython();
        void addDirectedEdge(uint64_t fromId, uint64_t toId, double dist, double speedLimit);
        void visualizeNetwork();

        Network();
        ~Network();

    private:
        std::unordered_map<uint64_t, Node> nodes;
        uint64_t numNodes;
        

};

#endif