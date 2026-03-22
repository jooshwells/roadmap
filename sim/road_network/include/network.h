#ifndef NETWORK_INTIALIZER_H
#define NETWORK_INTIALIZER_H

#include "node.h"
#include <unordered_map>
#include <cstdint>


class Network {

    public:
        
        void addNode(std::uint64_t id, double lat, double lon);
        Node* getNode(int id);
        void addDirectedEdge(int fromId, int toId, double dist, double speedLimit);
        void visualizeNetwork();

        Network();
        ~Network();

    private:
        std::unordered_map<int, Node> nodes;
        int numNodes;
        

};

#endif