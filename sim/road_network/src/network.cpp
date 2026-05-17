#include "network.h"
#include <cstdint>
#include <iostream>

void Network::addNode(std::uint64_t id, double lat, double lon)
{
    nodes.try_emplace(id, id, lat, lon);
    numNodes++;
}

Node* Network::getNode(int id)
{
    auto it = nodes.find(id);

    if (it != nodes.end())
    {
        return &(it->second);
    }

    return nullptr;
}

void Network::addDirectedEdge(int fromId, int toId, double dist, double speedLimit, int lanes)
{
    if (nodes.find(fromId) != nodes.end() && nodes.find(toId) != nodes.end())
    {
        nodes[fromId].outgoingEdges.emplace_back(toId, dist, speedLimit, lanes);
    }
    else
    {
        throw std::invalid_argument("Cannot create edge: Node ID does not exist.");
    }
}

void Network::visualizeNetwork()
{
    for (int i = 1; i < numNodes; i++)
    {
        std::cout << "Node " << i << " connects to: ";
        int n = getNode(i)->outgoingEdges.size();
        int j = 0;
        for (Road e : getNode(i)->outgoingEdges)
        {
            std::cout << e.getDest();
            j++;
            if (j < n) std::cout << " and ";
        }
        std::cout << "\n";
    }
}

Network::Network(){ numNodes = 0; }

Network::~Network() {}