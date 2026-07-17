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
                              std::vector<uint8_t> laneTurns, bool isLink)
{
    if (nodes.find(fromId) != nodes.end() && nodes.find(toId) != nodes.end())
    {
        Road& edge = nodes[fromId].outgoingEdges.emplace_back(nextEdgeId++, fromId, toId, dist, speedLimit, lanes);
        edge.setLayer(layer);
        edge.setIsLink(isLink);

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
    const bool link = edge->isLink();

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
    addDirectedEdge(newNodeId, toId, lenB, speed, lanes, std::move(ptsB), layer, {}, link);
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

namespace
{
    // Angular separation of two leg bearings folded to [0, 90] degrees
    // (radians): the opposing legs of one road (~180 apart) fold to ~0, a
    // true crossing folds to near 90. Legs more than 45 apart count as
    // crossing; anything shallower is the same roadway or a merge.
    constexpr double NETWORK_PI = 3.14159265358979323846;

    double crossingSeparation(double bearingA, double bearingB)
    {
        double diff = std::fabs(bearingA - bearingB);
        diff = std::fmod(diff, NETWORK_PI);                          // fold to [0, 180)
        if (diff > NETWORK_PI / 2.0) diff = NETWORK_PI - diff;       // fold to [0, 90]
        return diff;
    }

    bool legsCross(double bearingA, double bearingB)
    {
        return crossingSeparation(bearingA, bearingB) > NETWORK_PI / 4.0;
    }

    // Direction a leg leaves 'node' toward 'neighbor', taken from the edge
    // geometry's local tangent at the node. The node-to-node straight line
    // is only a fallback: a curved side street that departs due east and
    // then bends north reads as a near-collinear fork by endpoint bearing
    // but is a genuine crossing at the junction mouth. Callers compare
    // bearings folded to [0, 90], so pointing toward vs away from the node
    // doesn't matter.
    bool legBearingAt(const Node& node, const Node& neighbor, double& outBearing)
    {
        double px, py, tx, ty, pz;
        for (const Road& e : node.outgoingEdges)
        {
            if (e.getDest() != neighbor.getId()) continue;
            if (e.samplePointAt(0.0, px, py, tx, ty, pz) &&
                std::isfinite(tx) && std::isfinite(ty) && (tx != 0.0 || ty != 0.0))
            {
                outBearing = std::atan2(ty, tx);
                return true;
            }
        }
        for (const Road& e : neighbor.outgoingEdges)
        {
            if (e.getDest() != node.getId()) continue;
            if (e.samplePointAt(e.getLength(), px, py, tx, ty, pz) &&
                std::isfinite(tx) && std::isfinite(ty) && (tx != 0.0 || ty != 0.0))
            {
                outBearing = std::atan2(-ty, -tx);
                return true;
            }
        }

        double dx = neighbor.getX() - node.getX();
        double dy = neighbor.getY() - node.getY();
        if (dx == 0.0 && dy == 0.0) return false;
        outBearing = std::atan2(dy, dx);
        return true;
    }
}

// OSM rarely tags stop signs (and tags that exist usually sit on approach
// nodes that simplification drops), so most real intersections arrive as
// PASS_THROUGH and cross traffic would enter them unchecked. Default those to
// stops; calculateIntersectionPriorities then downgrades the <=3-incoming ones
// to yield-style two-way stops on the minor road. Nodes connected to fewer
// than 3 distinct neighbors are dead ends or mid-road continuation nodes, not
// intersections, and must stay PASS_THROUGH or the yield downgrade would brake
// traffic mid-road. A degree-3+ node whose legs are all near-collinear is a
// fork or merge of one roadway (carriageway split around a median, turn-pocket
// divergence), not an intersection, and must also stay PASS_THROUGH. Explicit
// values from the data (signal/stop/yield) win.
//
// 4-way crossings default to TRAFFIC_LIGHT instead of a stop when either
//  - two *crossing* approaches are both high-speed (all-way stops are a
//    low-speed device), or
//  - an approach is wide enough (>= SIGNAL_LANE_THRESHOLD lanes) that cross
//    traffic can't realistically clear it from a stop, however slow and small
//    the crossing road is.
// Crossing-ness is judged by approach bearing, since the two opposing legs of
// one arterial are distinct neighbor nodes and would otherwise count as two
// fast "roads" and signalize every arterial's side-street crossings.
// Link edges (OSM *_link turn slips and ramps) count toward being an
// intersection but never toward the 4-way tally or the signal warrant: a
// median crossover's slip must yield to the carriageway it lands on, not
// signalize it.
void Network::applyDefaultTrafficControls()
{
    for (auto& pair : nodes)
    {
        applyDefaultControlAt(pair.second);
    }
}

void Network::applyDefaultControlAt(Node& node)
{
    // Approach speed (m/s, ~45 mph) above which an all-way stop is
    // unrealistic and a crossing of two such roads gets a signal.
    constexpr double SIGNAL_SPEED_THRESHOLD_MPS = 20.1;
    // Approach lane count at which crossing traffic can no longer clear the
    // road from a stop (3 lanes per direction = 6 lanes to cross), so the
    // crossing gets a signal regardless of the crossing road's speed.
    constexpr int SIGNAL_LANE_THRESHOLD = 3;

    if (node.type != Node::PASS_THROUGH) return;

    std::unordered_set<uint64_t> neighbors(node.incomingEdgeNodeIds.begin(),
                                           node.incomingEdgeNodeIds.end());
    for (const Road& edge : node.outgoingEdges)
    {
        neighbors.insert(edge.getDest());
    }

    if (neighbors.size() < 3) return;

    // Only a node where two legs actually cross is an intersection. Bearings
    // toward every distinct neighbor (incoming or outgoing) count, so a
    // one-way side street of either direction still registers.
    std::vector<double> legBearings;
    legBearings.reserve(neighbors.size());
    for (uint64_t neighborId : neighbors)
    {
        Node* neighborNode = getNode(neighborId);
        if (!neighborNode) continue;

        double bearing;
        if (legBearingAt(node, *neighborNode, bearing))
        {
            legBearings.push_back(bearing);
        }
    }

    bool hasCrossing = false;
    for (size_t i = 0; i < legBearings.size() && !hasCrossing; ++i)
    {
        for (size_t j = i + 1; j < legBearings.size(); ++j)
        {
            if (legsCross(legBearings[i], legBearings[j]))
            {
                hasCrossing = true;
                break;
            }
        }
    }
    if (!hasCrossing) return;

    node.type = Node::FOUR_WAY_STOP;

    // Signal upgrade only applies to true 4-way crossings. Link edges (OSM
    // *_link turn slips / ramps) don't count toward the leg tally: a median
    // crossover where a left-turn slip lands on the opposite carriageway has
    // 4 neighbors but is really a 3-leg junction, and counting the slip
    // would signalize the arterial's through movement against its own turn
    // traffic. The slip still made the node an intersection above, so the
    // yield pass will put the give-way on it instead.
    std::size_t nonLinkNeighbors = 0;
    for (uint64_t neighborId : neighbors)
    {
        bool hasNonLinkEdge = false;
        for (const Road& edge : node.outgoingEdges)
        {
            if (edge.getDest() == neighborId && !edge.isLink()) { hasNonLinkEdge = true; break; }
        }
        if (!hasNonLinkEdge)
        {
            if (Node* neighborNode = getNode(neighborId))
            {
                for (const Road& edge : neighborNode->outgoingEdges)
                {
                    if (edge.getDest() == node.getId() && !edge.isLink()) { hasNonLinkEdge = true; break; }
                }
            }
        }
        if (hasNonLinkEdge) ++nonLinkNeighbors;
    }
    if (nonLinkNeighbors < 4) return;

    // Bearing (radians), speed limit, and lane count of each incoming
    // approach. Link approaches never warrant a signal, so they are skipped
    // here too.
    struct Approach { double bearing; double speed; int lanes; };
    std::vector<Approach> approaches;
    for (uint64_t incomingId : node.incomingEdgeNodeIds)
    {
        Node* predNode = getNode(incomingId);
        if (!predNode) continue;

        double approachSpeed = 0.0;
        int approachLanes = 0;
        for (const Road& edge : predNode->outgoingEdges)
        {
            if (edge.getDest() != node.getId() || edge.isLink()) continue;
            if (edge.getSpeedLimit() > approachSpeed) approachSpeed = edge.getSpeedLimit();
            if (edge.getLanes() > approachLanes) approachLanes = edge.getLanes();
        }

        double bearing;
        if (approachSpeed <= 0.0 || !legBearingAt(node, *predNode, bearing)) continue;

        approaches.push_back({bearing, approachSpeed, approachLanes});
    }

    // Any crossing pair of approaches that is fast-over-fast or involves a
    // wide road warrants a signal.
    for (size_t i = 0; i < approaches.size(); ++i)
    {
        for (size_t j = i + 1; j < approaches.size(); ++j)
        {
            const Approach& a = approaches[i];
            const Approach& b = approaches[j];
            if (!legsCross(a.bearing, b.bearing)) continue;

            const bool bothFast = a.speed >= SIGNAL_SPEED_THRESHOLD_MPS &&
                                  b.speed >= SIGNAL_SPEED_THRESHOLD_MPS;
            const bool eitherWide = a.lanes >= SIGNAL_LANE_THRESHOLD ||
                                    b.lanes >= SIGNAL_LANE_THRESHOLD;
            if (bothFast || eitherWide)
            {
                node.type = Node::TRAFFIC_LIGHT;
                return;
            }
        }
    }
}

// look at speed limits and lane counts, helps establish minor/major roads for right of way handling
void Network::calculateIntersectionPriorities()
{
    for (auto& pair : nodes)
    {
        assignYieldPriorityAt(pair.second);
    }
}

void Network::assignYieldPriorityAt(Node& node)
{
    // Recomputed from scratch so runtime refreshes never leave approaches
    // from a pre-edit topology in the list.
    node.minorRoadOriginIds.clear();

    if (node.type != Node::YIELD_STOP && node.type != Node::FOUR_WAY_STOP) return;

    // 2 way or T type stop, skip if 4 way
    if (node.incomingEdgeNodeIds.size() > 3) return;

    // change to yield stop
    node.type = Node::YIELD_STOP;

    struct Approach { uint64_t originId; double bearing; double speed; int lanes; };
    std::vector<Approach> approaches;
    std::unordered_set<uint64_t> seenOrigins;
    for (uint64_t incomingId : node.incomingEdgeNodeIds)
    {
        if (!seenOrigins.insert(incomingId).second) continue;

        Node* predNode = getNode(incomingId);
        if (!predNode) continue;

        double approachSpeed = 0.0;
        int approachLanes = 0;
        for (const Road& edge : predNode->outgoingEdges)
        {
            if (edge.getDest() != node.getId()) continue;
            if (edge.getSpeedLimit() > approachSpeed) approachSpeed = edge.getSpeedLimit();
            if (edge.getLanes() > approachLanes) approachLanes = edge.getLanes();
        }

        double bearing;
        if (!legBearingAt(node, *predNode, bearing)) continue;

        approaches.push_back({incomingId, bearing, approachSpeed, approachLanes});
    }

    // The major road is the fastest (then widest) approach. Only approaches
    // that actually CROSS it yield; a near-collinear approach is the same
    // roadway, so a lane-count change across the node (a turn pocket
    // starting, a lane drop) never puts a stop on the through road.
    const Approach* major = nullptr;
    for (const Approach& a : approaches)
    {
        if (!major || a.speed > major->speed ||
            (a.speed == major->speed && a.lanes > major->lanes))
        {
            major = &a;
        }
    }
    if (!major) return;

    for (const Approach& a : approaches)
    {
        if (&a == major) continue;
        if (legsCross(a.bearing, major->bearing))
        {
            node.minorRoadOriginIds.push_back(a.originId);
        }
    }

    // A control the dataset placed explicitly must still stop someone even
    // when no incoming approach crosses the major road (e.g. a tagged stop
    // on a continuation node): every approach yields. Defaulted nodes with
    // no crossing approach have no conflicting inbound traffic, and an empty
    // list correctly lets everything roll through.
    if (node.minorRoadOriginIds.empty() && node.controlFromData)
    {
        for (const Approach& a : approaches)
        {
            node.minorRoadOriginIds.push_back(a.originId);
        }
    }
}

void Network::refreshTrafficControlAt(uint64_t nodeId)
{
    Node* node = getNode(nodeId);
    if (!node) return;

    if (!node->controlFromData)
    {
        // Defaulted (or so-far-uncontrolled) node: rebuild the control from
        // the current topology, exactly as the load-time pipeline would.
        node->type = Node::PASS_THROUGH;
        applyDefaultControlAt(*node);
    }
    else if (node->type == Node::YIELD_STOP && node->incomingEdgeNodeIds.size() > 3)
    {
        // A dataset stop that the <=3-approach rule downgraded to a yield has
        // gained enough approaches to be a full all-way stop again.
        node->type = Node::FOUR_WAY_STOP;
    }

    assignYieldPriorityAt(*node);
}

namespace
{
    // Fill every unmarked lane (mask 0) with the movements that exist past
    // the destination node, leaving OSM-marked lanes untouched. Filled lanes
    // carry TurnLane::Inferred so a later pass can redo them when the
    // intersection changes. Conventions: every unmarked lane goes through,
    // the leftmost/rightmost unmarked lane picks up a left/right turn no
    // marked lane already covers, and an approach with no through movement
    // (T-stem) splits its unmarked lanes between the two turns. Finally,
    // never feed the through road more lanes than it has: while the through
    // count exceeds 'throughCapacity', filled lanes that also carry a turn
    // drop their through bit, outermost first, so "left||" into a 1-lane
    // road becomes "left|through|right" instead of "left|through|through;right".
    void fillUnmarkedLanes(std::vector<uint8_t>& masks,
                           bool hasLeft, bool hasThrough, bool hasRight,
                           int throughCapacity)
    {
        std::vector<int> unmarked;
        for (int i = 0; i < static_cast<int>(masks.size()); i++)
            if (masks[i] == 0) unmarked.push_back(i);
        if (unmarked.empty()) return;

        if (!hasLeft && !hasThrough && !hasRight)
        {
            // Dead end: nothing to restrict.
            for (int i : unmarked) masks[i] = TurnLane::Through | TurnLane::Inferred;
            return;
        }

        bool coveredLeft = false, coveredRight = false;
        for (uint8_t m : masks)
        {
            coveredLeft  |= (m & TurnLane::Left) != 0;
            coveredRight |= (m & TurnLane::Right) != 0;
        }
        const bool needLeft  = hasLeft && !coveredLeft;
        const bool needRight = hasRight && !coveredRight;

        if (!hasThrough && needLeft && needRight)
        {
            // T-junction stem: left half of the unmarked lanes turns left,
            // right half right, the middle of an odd count may do either.
            const int n = static_cast<int>(unmarked.size());
            for (int k = 0; k < n; k++)
            {
                uint8_t m = TurnLane::Inferred;
                if (k < (n + 1) / 2) m |= TurnLane::Left;
                if (k >= n / 2)      m |= TurnLane::Right;
                masks[unmarked[k]] = m;
            }
            return;
        }

        for (int i : unmarked)
            masks[i] = TurnLane::Inferred | (hasThrough ? TurnLane::Through : 0);
        if (needLeft)  masks[unmarked.front()] |= TurnLane::Left;
        if (needRight) masks[unmarked.back()]  |= TurnLane::Right;

        // A lane can still end up with no movement when everything available
        // is already covered by marked lanes (e.g. "left||right" at a
        // T-stem): fail open with whatever the intersection offers.
        for (int i : unmarked)
        {
            if (masks[i] != TurnLane::Inferred) continue;
            if (hasLeft)  masks[i] |= TurnLane::Left;
            if (hasRight) masks[i] |= TurnLane::Right;
        }

        if (!hasThrough || throughCapacity <= 0) return;
        int throughCount = 0;
        for (uint8_t m : masks) throughCount += (m & TurnLane::Through) ? 1 : 0;

        // Trim overflow through lanes, right-turn lanes from the right first,
        // then left-turn lanes from the left. Only filled lanes give up their
        // through bit, and only when a turn keeps the lane usable.
        for (auto it = unmarked.rbegin(); it != unmarked.rend() && throughCount > throughCapacity; ++it)
        {
            uint8_t& m = masks[*it];
            if ((m & TurnLane::Right) && (m & TurnLane::Through))
            {
                m &= static_cast<uint8_t>(~TurnLane::Through);
                throughCount--;
            }
        }
        for (auto it = unmarked.begin(); it != unmarked.end() && throughCount > throughCapacity; ++it)
        {
            uint8_t& m = masks[*it];
            if ((m & TurnLane::Left) && (m & TurnLane::Through))
            {
                m &= static_cast<uint8_t>(~TurnLane::Through);
                throughCount--;
            }
        }
    }
}

// Most OSM edges arrive with a null turn:lanes tag -- or a partial one like
// "left||" where only the turn pocket is painted -- and with no turn map a
// car will happily hook a right from the leftmost lane. Fill the gaps with
// standard road-marking conventions: look at which movements geometrically
// exist at the edge's destination (left / through / right, U-turns excluded)
// and hand the lane list to fillUnmarkedLanes above. Lanes explicitly marked
// in real OSM data are never touched; inferred lanes are recomputed from
// scratch each call so runtime road edits stay consistent.
void Network::assignInferredTurnLanes()
{
    for (auto& pair : nodes)
    {
        Node& from = pair.second;
        for (Road& edge : from.outgoingEdges)
        {
            const int lanes = edge.getLanes();
            if (lanes <= 0) continue;

            // OSM-marked lanes are authoritative; only unmarked tokens
            // (mask 0) and lanes a previous pass filled are ours to set.
            std::vector<uint8_t> masks;
            if (edge.isLaneTurnsFromOsm())
            {
                masks = edge.getLaneTurns();
                bool anyUnmarked = false;
                for (uint8_t& m : masks)
                {
                    if (m & TurnLane::Inferred) m = 0; // recompute below
                    anyUnmarked |= (m == 0);
                }
                if (!anyUnmarked) continue; // fully tagged
            }
            else
            {
                masks.assign(lanes, 0);
            }

            Node* dest = getNode(edge.getDest());
            if (!dest) continue;

            const double inX = dest->getX() - from.getX();
            const double inY = dest->getY() - from.getY();

            bool hasLeft = false, hasThrough = false, hasRight = false;
            int throughCapacity = 0; // lane count of the road(s) straight ahead
            for (const Road& out : dest->outgoingEdges)
            {
                if (out.getDest() == edge.getOriginId()) continue; // U-turn
                Node* outDest = getNode(out.getDest());
                if (!outDest) continue;

                switch (RoadIntersectionUtil::ClassifyTurn(
                    inX, inY, outDest->getX() - dest->getX(), outDest->getY() - dest->getY()))
                {
                    case RoadIntersectionUtil::TurnDir::Left:    hasLeft = true;    break;
                    case RoadIntersectionUtil::TurnDir::Through: hasThrough = true;
                                                                 throughCapacity += out.getLanes(); break;
                    case RoadIntersectionUtil::TurnDir::Right:   hasRight = true;   break;
                }
            }

            fillUnmarkedLanes(masks, hasLeft, hasThrough, hasRight, throughCapacity);

            edge.setLaneTurns(std::move(masks), edge.isLaneTurnsFromOsm());
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
