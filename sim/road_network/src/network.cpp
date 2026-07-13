#include "network.h"
#include "intersection_geometry.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <limits>
#include <unordered_map>
#include <unordered_set>

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

void Network::addDirectedEdge(uint64_t fromId, uint64_t toId, double dist, double speedLimit, int lanes,
                              std::vector<RoadGeomPoint> geometry, int layer,
                              std::vector<uint8_t> laneTurns)
{
    if (nodes.find(fromId) != nodes.end() && nodes.find(toId) != nodes.end())
    {
        Road& edge = nodes[fromId].outgoingEdges.emplace_back(nextEdgeId++, fromId, toId, dist, speedLimit, lanes);
        edge.setLayer(layer);

        nodes[toId].incomingEdgeNodeIds.push_back(fromId);

        if (static_cast<int>(laneTurns.size()) == lanes)
        {
            edge.setLaneTurns(std::move(laneTurns), true);
        }

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
    const int layer = edge->getLayer();

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
    addDirectedEdge(newNodeId, toId, lenB, speed, lanes, std::move(ptsB), layer);
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

void Network::applyVerticality(double layerHeightM, double rampLengthM, double medianGapM)
{
    // Node elevation = layer of the road that continues THROUGH the node,
    // i.e. the layer with edges toward the most distinct neighbours (2+).
    // That keeps a viaduct at deck height where an on/off-ramp joins it (the
    // ramp climbs inside its own span) instead of the deck dipping to ground
    // at every junction. Ties go to the layer nearest the ground. Nodes with
    // no through layer -- a bridge that simply ends, or a lone dead end --
    // fall back to the incident layer closest to ground, so ground roads
    // passing a bridge endpoint stay flat and the deck ramps down in-span.
    for (auto& [id, node] : nodes)
    {
        // Distinct neighbours per layer, over both edge directions.
        std::unordered_map<int, std::unordered_set<uint64_t>> perLayer;
        for (const Road& e : node.outgoingEdges)
        {
            perLayer[e.getLayer()].insert(e.getDest());
        }
        for (uint64_t inId : node.incomingEdgeNodeIds)
        {
            auto it = nodes.find(inId);
            if (it == nodes.end()) continue;
            for (const Road& e : it->second.outgoingEdges)
            {
                if (e.getDest() == id)
                {
                    perLayer[e.getLayer()].insert(inId);
                    break;
                }
            }
        }

        bool haveAny = false;
        int closest = 0;
        bool haveThrough = false;
        int throughLayer = 0;
        size_t throughCount = 0;
        for (const auto& [layer, nbrs] : perLayer)
        {
            if (!haveAny || std::abs(layer) < std::abs(closest))
            {
                closest = layer;
                haveAny = true;
            }
            if (nbrs.size() >= 2 &&
                (!haveThrough || nbrs.size() > throughCount ||
                 (nbrs.size() == throughCount && std::abs(layer) < std::abs(throughLayer))))
            {
                throughLayer = layer;
                throughCount = nbrs.size();
                haveThrough = true;
            }
        }

        node.setZ((haveThrough ? throughLayer : closest) * layerHeightM);
    }

    // Junction setback radius per node (the same trim the visualizer uses),
    // computed once and reused as the flat zone of every incident edge.
    std::unordered_map<uint64_t, double> setbacks;
    setbacks.reserve(nodes.size());
    for (auto& [id, node] : nodes)
    {
        setbacks[id] = RoadIntersectionUtil::GetNodeSetbackMeters(
            this, node, static_cast<float>(medianGapM));
    }

    // Write each edge's vertical profile onto its centerline. Applied even
    // when start/mid/end are all at ground level: a re-run after a layer edit
    // must scrub the stale profile off a road that was just grounded.
    for (auto& [id, node] : nodes)
    {
        for (Road& e : node.outgoingEdges)
        {
            auto it = nodes.find(e.getDest());
            if (it == nodes.end()) continue;

            const double zStart = node.getZ();
            const double zEnd   = it->second.getZ();
            const double zMid   = e.getLayer() * layerHeightM;

            e.applyVerticalProfile(zStart, zMid, zEnd, rampLengthM,
                                   setbacks[id], setbacks[e.getDest()]);
        }
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

// OSM rarely tags stop signs (and tags that exist usually sit on approach
// nodes that simplification drops), so most real intersections arrive as
// PASS_THROUGH and cross traffic would enter them unchecked. Default those to
// stops; calculateIntersectionPriorities then downgrades the <=3-incoming ones
// to yield-style two-way stops on the minor road. Nodes connected to fewer
// than 3 distinct neighbors are dead ends or mid-road continuation nodes, not
// intersections, and must stay PASS_THROUGH or the yield downgrade would brake
// traffic mid-road. Explicit values from the data (signal/stop/yield) win.
void Network::applyDefaultTrafficControls()
{
    for (auto& pair : nodes)
    {
        Node& node = pair.second;
        if (node.type != Node::PASS_THROUGH) continue;

        std::unordered_set<uint64_t> neighbors(node.incomingEdgeNodeIds.begin(),
                                               node.incomingEdgeNodeIds.end());
        for (const Road& edge : node.outgoingEdges)
        {
            neighbors.insert(edge.getDest());
        }

        if (neighbors.size() >= 3)
        {
            node.type = Node::FOUR_WAY_STOP;
        }
    }
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

// Most OSM edges arrive with a null turn:lanes tag, and with no turn map a
// car will happily hook a right from the leftmost lane. Fill the gap with
// standard road-marking conventions: look at which movements geometrically
// exist at the edge's destination (left / through / right, U-turns excluded),
// then give every lane through, the leftmost lane the left turn, and the
// rightmost lane the right turn. Approaches with no through movement (T-stem)
// split their lanes between the two turns instead. Edges whose map came from
// real OSM data are left untouched; inferred maps are recomputed from scratch
// each call so runtime road edits stay consistent.
void Network::assignInferredTurnLanes()
{
    for (auto& pair : nodes)
    {
        Node& from = pair.second;
        for (Road& edge : from.outgoingEdges)
        {
            if (edge.isLaneTurnsFromOsm()) continue;

            Node* dest = getNode(edge.getDest());
            if (!dest) continue;

            const int lanes = edge.getLanes();
            if (lanes <= 0) continue;

            const double inX = dest->getX() - from.getX();
            const double inY = dest->getY() - from.getY();

            bool hasLeft = false, hasThrough = false, hasRight = false;
            for (const Road& out : dest->outgoingEdges)
            {
                if (out.getDest() == edge.getOriginId()) continue; // U-turn
                Node* outDest = getNode(out.getDest());
                if (!outDest) continue;

                switch (RoadIntersectionUtil::ClassifyTurn(
                    inX, inY, outDest->getX() - dest->getX(), outDest->getY() - dest->getY()))
                {
                    case RoadIntersectionUtil::TurnDir::Left:    hasLeft = true;    break;
                    case RoadIntersectionUtil::TurnDir::Through: hasThrough = true; break;
                    case RoadIntersectionUtil::TurnDir::Right:   hasRight = true;   break;
                }
            }

            std::vector<uint8_t> masks(lanes, 0);
            if (!hasLeft && !hasThrough && !hasRight)
            {
                // Dead end: nothing to restrict.
                for (uint8_t& m : masks) m |= TurnLane::Through;
            }
            else if (!hasThrough && hasLeft && hasRight)
            {
                // T-junction stem: left half turns left, right half right,
                // middle lane of an odd count may do either.
                for (int i = 0; i < (lanes + 1) / 2; i++) masks[i] |= TurnLane::Left;
                for (int i = lanes / 2; i < lanes; i++)   masks[i] |= TurnLane::Right;
            }
            else
            {
                if (hasThrough)
                    for (uint8_t& m : masks) m |= TurnLane::Through;

                if (hasLeft)
                {
                    if (hasThrough) masks[0] |= TurnLane::Left;
                    else            for (uint8_t& m : masks) m |= TurnLane::Left;
                }
                if (hasRight)
                {
                    if (hasThrough) masks[lanes - 1] |= TurnLane::Right;
                    else            for (uint8_t& m : masks) m |= TurnLane::Right;
                }
            }

            edge.setLaneTurns(std::move(masks), false);
        }
    }
}

void Network::resetPathfindingState() {
    for (auto& pair : nodes) {
        pair.second.g = std::numeric_limits<double>::infinity();
        pair.second.rhs = std::numeric_limits<double>::infinity();
    }
}

Network::Network(){ numNodes = 0; }

Network::~Network() {}
