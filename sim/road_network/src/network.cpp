#include "network.h"
#include <cstdint>
#include <iostream>
#include <fstream>

// Combined signature to take offline x/y AND the intersection type string
void Network::addNode(std::uint64_t id, double lat, double lon, double x, double y, const std::string& typeStr)
{
    // Pass ALL variables down into the Node constructor
    auto [iterator, inserted] = nodes.try_emplace(id, id, lat, lon, x, y, typeStr);
    
    if (inserted)
    {
        nodeIds.push_back(id);
        numNodes++;
    }
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

Node* Network::getRandomNode(std::mt19937& rng)
{
    // Safety check in case the map is empty
    if (nodeIds.empty()) 
    {
        return nullptr; 
    }

    // Define a distribution range from 0 to the last index
    std::uniform_int_distribution<std::size_t> dist(0, nodeIds.size() - 1);
    
    // Pick a random index
    std::size_t randomIndex = dist(rng);
    
    // Get the ID and return the corresponding Node pointer
    uint64_t randomId = nodeIds[randomIndex];
    return getNode(randomId);
}

void Network::addDirectedEdge(uint64_t fromId, uint64_t toId, double dist, double speedLimit, int lanes)
{
    if (nodes.find(fromId) != nodes.end() && nodes.find(toId) != nodes.end())
    {
        nodes[fromId].outgoingEdges.emplace_back(nextEdgeId++, fromId, toId, dist, speedLimit, lanes);
        
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

    outFile << "source,target,length,source_x,source_y,edge_id\n";

    for (const auto& pair : nodes) {
        std::uint64_t sourceId = pair.first;
        const Node& node = pair.second;
        
        if (node.outgoingEdges.empty()) {
            // Write the dead-end, leaving target, length, and edge_id blank
            outFile << sourceId << ",,," << node.getX() << "," << node.getY() << ",\n"; 
        } else {
            for (const Road& road : node.outgoingEdges) {
                // UPDATE: Output the unique edge ID at the end of the line
                outFile << sourceId << "," 
                        << road.getDest() << "," 
                        << road.getLength() << "," 
                        << node.getX() << "," 
                        << node.getY() << "," 
                        << road.getEdgeId() << "\n";
            }
        }
    }

    outFile.close();
    std::cout << "Network successfully exported to: " << filename << "\n";
}

// look at speed limits and lane counts, helps establish minor/major roads for right of way handling
void Network::calculateIntersectionPriorities()
{
    for (auto& pair : nodes) 
    {
        Node& node = pair.second;

        if (node.type == Node::YIELD_STOP || node.type == Node::FOUR_WAY_STOP) 
        {
            // 2 way or T type stop, skip if 4 way
            if (node.incomingEdgeNodeIds.size() <= 3) 
            {
                // change to yield stop 
                node.type = Node::YIELD_STOP;

                double maxSpeed = 0.0;
                int maxLanes = 0;

                // get speed and lane counts
                for (uint64_t incomingId : node.incomingEdgeNodeIds) 
                {
                    Node* predNode = getNode(incomingId);
                    if (!predNode) continue;

                    for (Road& edge : predNode->outgoingEdges) {
                        if (edge.getDest() == node.getId()) {
                            if (edge.getSpeedLimit() > maxSpeed) maxSpeed = edge.getSpeedLimit();
                            if (edge.getLanes() > maxLanes) maxLanes = edge.getLanes();
                        }
                    }
                }

                // make slower/smaller road MINOR
                for (uint64_t incomingId : node.incomingEdgeNodeIds) 
                {
                    Node* predNode = getNode(incomingId);
                    if (!predNode) continue;

                    for (Road& edge : predNode->outgoingEdges) {
                        if (edge.getDest() == node.getId()) {
                            // make minor road yield
                            if (edge.getSpeedLimit() < maxSpeed || edge.getLanes() < maxLanes) {
                                node.minorRoadOriginIds.push_back(edge.getOriginId());
                            }
                        }
                    }
                }

                // if identitical pick random
                if (node.minorRoadOriginIds.empty() && !node.incomingEdgeNodeIds.empty()) {
                    Node* predNode = getNode(node.incomingEdgeNodeIds[0]);
                    for (Road& edge : predNode->outgoingEdges) {
                        if (edge.getDest() == node.getId()) {
                            node.minorRoadOriginIds.push_back(edge.getOriginId());
                            break;
                        }
                    }
                }
            }
        }
    }
}

Network::Network(){ numNodes = 0; }

Network::~Network() {}