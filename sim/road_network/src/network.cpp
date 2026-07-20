#include "network.h"
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <fstream>

void Network::addNode(std::uint64_t id, double lat, double lon, double x, double y)
{
    auto [iterator, inserted] = nodes.try_emplace(id, id, lat, lon, x, y);
    
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

void Network::addDirectedEdge(uint64_t fromId, uint64_t toId, double dist, double speedLimit, int lanes,
                              std::vector<RoadGeomPoint> geometry, const std::string& name)
{
    if (nodes.find(fromId) != nodes.end() && nodes.find(toId) != nodes.end())
    {
        Road& edge = nodes[fromId].outgoingEdges.emplace_back(nextEdgeId++, toId, dist, speedLimit, lanes, name);

        nodes[toId].incomingEdgeNodeIds.push_back(fromId);

        if (geometry.size() >= 2)
        {
            // OSM geometry is stored per way, so a reversed directed edge can
            // carry a to->from point order. Orient by whichever end sits
            // closest to each node, then snap the endpoints exactly onto the
            // node coordinates so road visuals stay flush at junctions.
            const Node& a = nodes[fromId];
            const Node& b = nodes[toId];
            auto dist2 = [](const RoadGeomPoint& p, const Node& n) {
                const double dx = p.x - n.getX();
                const double dy = p.y - n.getY();
                return dx * dx + dy * dy;
            };
            const double fwd = dist2(geometry.front(), a) + dist2(geometry.back(), b);
            const double rev = dist2(geometry.front(), b) + dist2(geometry.back(), a);
            if (rev < fwd) std::reverse(geometry.begin(), geometry.end());

            geometry.front() = { a.getX(), a.getY(), 0.0 };
            geometry.back()  = { b.getX(), b.getY(), 0.0 };
            edge.setGeometry(std::move(geometry));
        }
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

void Network::visualizeNetworkForPython(const std::string& outputPath) {
    std::string filename = outputPath;
    std::ofstream outFile(filename);
    
    if (!outFile.is_open()) {
        std::cerr << "Error: Could not open " << filename << " for writing.\n";
        return;
    }

    outFile << "source,target,length,source_x,source_y,edge_id\n";

    for (const auto& pair : nodes) {
        std::uint64_t sourceId = pair.first;
        const Node& node = pair.second;

        // NetworkBuilder stores y negated (-j["y"]) for the sim's coordinate
        // convention. Undo that here so the exported CSV matches the source map
        // orientation the Python heatmaps expect (north up).
        const double sourceY = -node.getY();

        if (node.outgoingEdges.empty()) {
            // Write the dead-end, leaving target, length, and edge_id blank
            outFile << sourceId << ",,," << node.getX() << "," << sourceY << ",\n";
        } else {
            for (const Road& road : node.outgoingEdges) {
                // UPDATE: Output the unique edge ID at the end of the line
                outFile << sourceId << ","
                        << road.getDest() << ","
                        << road.getLength() << ","
                        << node.getX() << ","
                        << sourceY << ","
                        << road.getEdgeId() << "\n";
            }
        }
    }

    outFile.close();
    std::cout << "Network successfully exported to: " << filename << "\n";
}

Network::Network(){ numNodes = 0; }

Network::~Network() {}