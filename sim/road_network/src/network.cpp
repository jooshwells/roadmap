#include "network.h"
#include <cstdint>
#include <iostream>
#include <fstream>

void Network::addNode(std::uint64_t id, double lat, double lon, double x, double y)
{
    nodes.try_emplace(id, id, lat, lon, x, y);
    numNodes++;
}

Node* Network::getNode(uint64_t id)
{
    auto it = nodes.find(id);

    if (it != nodes.end())
    {
        return &(it->second);
    }

    return nullptr;
}

void Network::addDirectedEdge(uint64_t fromId, uint64_t toId, double dist, double speedLimit)
{
    if (nodes.find(fromId) != nodes.end() && nodes.find(toId) != nodes.end())
    {
        nodes[fromId].outgoingEdges.emplace_back(toId, dist, speedLimit);
        nodes[toId].incomingEdgeNodeIds.push_back(fromId);
    }
    else
    {
        throw std::invalid_argument("Cannot create edge: Node ID does not exist.");
    }
}

void Network::visualizeNetwork()
{
    for (uint64_t i = 1; i < numNodes; i++)
    {
        std::cout << "Node " << i << " connects to: ";
        uint64_t n = getNode(i)->outgoingEdges.size();
        uint64_t j = 0;
        for (Road e : getNode(i)->outgoingEdges)
        {
            std::cout << e.getDest();
            j++;
            if (j < n) std::cout << " and ";
        }
        std::cout << "\n";
    }
}

void Network::visualizeNetworkForPython() {
    std::string filename = "network_graph.csv";
    std::ofstream outFile(filename);
    
    if (!outFile.is_open()) {
        std::cerr << "Error: Could not open " << filename << " for writing.\n";
        return;
    }

    // UPDATE: Added source_x and source_y to the header
    outFile << "source,target,length,source_x,source_y\n";

    for (const auto& pair : nodes) {
        std::uint64_t sourceId = pair.first;
        const Node& node = pair.second;
        
        if (node.outgoingEdges.empty()) {
            // Write the dead-end, skipping target and length, but providing X and Y
            outFile << sourceId << ",,," << node.getX() << "," << node.getY() << "\n"; 
        } else {
            for (const Road& road : node.outgoingEdges) {
                // Write the edge details, plus the X and Y of the source node
                outFile << sourceId << "," << road.getDest() << "," << road.getLength() 
                        << "," << node.getX() << "," << node.getY() << "\n";
            }
        }
    }

    outFile.close();
    std::cout << "Network successfully exported to: " << filename << "\n";
    std::cout << "You can now run 'python visualize_network.py' to see the graph.\n";
}

Network::Network(){ numNodes = 0; }

Network::~Network() {}