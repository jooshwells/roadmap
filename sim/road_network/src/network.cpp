#include "network.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <fstream>
#include <limits>

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
                              std::vector<RoadGeomPoint> geometry)
{
    if (nodes.find(fromId) != nodes.end() && nodes.find(toId) != nodes.end())
    {
        Road& edge = nodes[fromId].outgoingEdges.emplace_back(nextEdgeId++, toId, dist, speedLimit, lanes);

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

bool Network::splitDirectedEdge(uint64_t fromId, uint64_t toId, uint64_t newNodeId, double x, double y)
{
    if (newNodeId == fromId || newNodeId == toId) return false;

    Node* from = getNode(fromId);
    Node* to = getNode(toId);
    if (!from || !to) return false;

    Road* edge = nullptr;
    for (Road& e : from->outgoingEdges)
    {
        if (e.getDest() == toId) { edge = &e; break; }
    }
    if (!edge) return false;

    // Full centerline in map coordinates: the stored shape polyline, or the
    // straight node-to-node chord for shapeless (e.g. runtime-drawn) edges.
    std::vector<RoadGeomPoint> pts;
    if (edge->hasCurveGeometry())
    {
        pts = edge->getGeometry();
    }
    else
    {
        const double dx = to->getX() - from->getX();
        const double dy = to->getY() - from->getY();
        pts.push_back({ from->getX(), from->getY(), 0.0 });
        pts.push_back({ to->getX(), to->getY(), std::sqrt(dx * dx + dy * dy) });
    }
    const double total = pts.back().s;
    if (total <= 0.0) return false;

    // Project (x, y) onto the polyline to find the split arc length.
    double bestS = 0.0;
    double bestD2 = std::numeric_limits<double>::max();
    for (size_t i = 0; i + 1 < pts.size(); i++)
    {
        const double ax = pts[i].x, ay = pts[i].y;
        const double bx = pts[i + 1].x, by = pts[i + 1].y;
        const double vx = bx - ax, vy = by - ay;
        const double segLen2 = vx * vx + vy * vy;
        double t = (segLen2 > 0.0) ? ((x - ax) * vx + (y - ay) * vy) / segLen2 : 0.0;
        t = std::clamp(t, 0.0, 1.0);
        const double px = ax + vx * t, py = ay + vy * t;
        const double d2 = (x - px) * (x - px) + (y - py) * (y - py);
        if (d2 < bestD2)
        {
            bestD2 = d2;
            bestS = pts[i].s + (pts[i + 1].s - pts[i].s) * t;
        }
    }

    // Keep the split strictly inside the edge so neither half degenerates.
    const double endMargin = std::min(1.0, total * 0.05);
    bestS = std::clamp(bestS, endMargin, total - endMargin);

    // Split the centerline at bestS, using the new node position (x, y) as the
    // shared vertex so both halves end exactly on the node.
    std::vector<RoadGeomPoint> ptsA, ptsB;
    for (const RoadGeomPoint& p : pts)
    {
        if (p.s < bestS) ptsA.push_back(p);
        else if (p.s > bestS) ptsB.push_back(p);
    }
    ptsA.push_back({ x, y, 0.0 });
    ptsB.insert(ptsB.begin(), { x, y, 0.0 });

    // Divide the sim length proportionally: OSM length_m can differ slightly
    // from polyline arc length, and the halves must sum to the original.
    const double lenA = edge->getLength() * (bestS / total);
    const double lenB = edge->getLength() - lenA;
    const double speed = edge->getSpeedLimit();
    const int lanes = edge->getLanes();

    // Create the split node. Re-fetch everything afterwards per the header's
    // pointer-stability warning.
    addNode(newNodeId, 0.0, 0.0, x, y);
    from = getNode(fromId);
    to = getNode(toId);
    Node* mid = getNode(newNodeId);
    if (!from || !to || !mid) return false;
    edge = nullptr;
    for (Road& e : from->outgoingEdges)
    {
        if (e.getDest() == toId) { edge = &e; break; }
    }
    if (!edge) return false;

    // Retarget the first half in place.
    edge->setDest(newNodeId);
    edge->setLength(lenA);
    edge->setGeometry(std::move(ptsA));
    mid->incomingEdgeNodeIds.push_back(fromId);

    // 'to' no longer receives an edge directly from 'from'.
    auto it = std::find(to->incomingEdgeNodeIds.begin(), to->incomingEdgeNodeIds.end(), fromId);
    if (it != to->incomingEdgeNodeIds.end()) to->incomingEdgeNodeIds.erase(it);

    // Second half gets a fresh edge id and the remaining centerline.
    addDirectedEdge(newNodeId, toId, lenB, speed, lanes, std::move(ptsB));
    return true;
}

bool Network::removeDirectedEdge(uint64_t fromId, uint64_t toId)
{
    Node* from = getNode(fromId);
    Node* to = getNode(toId);
    if (!from || !to) return false;

    auto edgeIt = std::find_if(from->outgoingEdges.begin(), from->outgoingEdges.end(),
        [toId](const Road& e) { return e.getDest() == toId; });
    if (edgeIt == from->outgoingEdges.end()) return false;

    from->outgoingEdges.erase(edgeIt);

    auto inIt = std::find(to->incomingEdgeNodeIds.begin(), to->incomingEdgeNodeIds.end(), fromId);
    if (inIt != to->incomingEdgeNodeIds.end()) to->incomingEdgeNodeIds.erase(inIt);

    return true;
}

bool Network::removeNodeIfIsolated(uint64_t id)
{
    Node* node = getNode(id);
    if (!node) return false;
    if (!node->outgoingEdges.empty() || !node->incomingEdgeNodeIds.empty()) return false;

    nodes.erase(id);
    nodeIds.erase(std::remove(nodeIds.begin(), nodeIds.end(), id), nodeIds.end());
    if (numNodes > 0) numNodes--;
    return true;
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