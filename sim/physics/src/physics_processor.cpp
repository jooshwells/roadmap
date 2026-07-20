#define _USE_MATH_DEFINES
#include "physics_processor.h"
#include "spatial_hash.h"
#include "vehicle_state.h"
#include "node.h"
#include "road.h"
#include "intersection_geometry.h"
#include "dstarlite.h"
#include "heuristics3d.h"
#include <vector>
#include <math.h>
#include <cmath>
#include <limits>
#include <iostream>
#include <algorithm>

// Stop line for an edge entering a controlled node: the junction-box boundary
// where the pavement ends and the sign/signal is planted, not the node center
// at getLength(). Cars braking for getLength() halt in the middle of the
// rendered intersection.
static float stopLineArcPos(Network* network, const Road* road, const Node* destNode)
{
    return RoadIntersectionUtil::GetStopLineArcPos(
        network, *road, *destNode, RoadIntersectionUtil::MedianGapMeters);
}

// Traffic-light actuation tuning (see updateIntersections).
static constexpr float MIN_GREEN_SECONDS      = 5.0f;  // shortest green before a gap-out may end it
static constexpr float DEMAND_POLL_SECONDS    = 0.5f;  // demand scan rate during greens
static constexpr float DEMAND_DETECTOR_METERS = 60.0f; // approach distance that registers as demand
static constexpr float STRAIGHT_GREEN_BUDGET  = 30.0f; // straight green total, split across the axes
static constexpr float LEFT_DEMAND_WAIT_SECONDS = 2.0f; // stopped-time before a left turner counts

PhysicsProcessor::PhysicsProcessor(Network* mapNetwork, VehicleSpatialHash* spatialObj) : network(mapNetwork), spatialHash(spatialObj), vehicleList(), vehicleUpdates()
{
    // Create every traffic light's state up front so axis groupings are valid
    // the first time a car queries the light (no one-frame "red fallback"),
    // and so the frontend can render every fixture before traffic reaches it.
    if (network != nullptr) {
        // Clusters (and their shared axis baselines) must exist before any
        // axis grouping happens.
        rebuildSignalClusters();

        for (const auto& pair : network->getNodes()) {
            if (pair.second.type != Node::TRAFFIC_LIGHT) continue;
            Node* node = network->getNode(pair.first);
            if (node) {
                IntersectionState& state = intersections[pair.first];
                initializeLightAxes(node, state);

                // Stagger the starting timers so the whole map's lights don't
                // run in lockstep and release traffic in synchronized pulses.
                // A node-id hash spreads each light across its initial green.
                uint32_t h = static_cast<uint32_t>(pair.first) * 2654435761u;
                state.lightTimer = static_cast<float>(h % 1024u) / 1024.0f * state.phaseDurations[2];
            }
        }

        // Per-node split decisions need pooling once every member's axes
        // exist.
        syncClusterSplitPhasing();
    }
}

// Pools the split-phasing decision across every light of a junction cluster:
// axis maximum lane count and which direction legs exist anywhere in the
// cluster, applied back to all members identically (flags AND the leg-0 green
// duration, since only split axes budget a full green there). Perimeter
// nodes otherwise disagree -- each sees one direction of a divided road plus
// its internal legs -- and disagreeing members would read the mirrored phase
// indices as different movements.
void PhysicsProcessor::syncClusterSplitPhasing()
{
    for (auto& pair : intersections) {
        IntersectionState& state = pair.second;
        if (!state.bInCluster || state.syncMasterId != pair.first) continue;

        auto cit = junctionClusterOf.find(pair.first);
        if (cit == junctionClusterOf.end()) continue;

        // Gather the cluster's light states (master included).
        std::vector<IntersectionState*> members;
        for (uint64_t memberId : cit->second) {
            Node* member = network->getNode(memberId);
            auto sit = intersections.find(memberId);
            if (member == nullptr || sit == intersections.end()) continue;
            if (member->type != Node::TRAFFIC_LIGHT) continue;
            members.push_back(&sit->second);
        }

        for (int axis = 0; axis < 2; axis++) {
            int maxLanes = 0;
            bool anyEdges = false;
            bool legPresent[2] = {false, false};
            for (IntersectionState* st : members) {
                for (const Road* edge : st->axisEdges[axis]) {
                    maxLanes = std::max(maxLanes, edge->getLanes());
                    anyEdges = true;
                }
                for (int leg = 0; leg < 2; leg++) {
                    if (!st->axisLegEdges[axis][leg].empty()) legPresent[leg] = true;
                }
            }
            const bool split = anyEdges
                && (maxLanes <= 2 || !legPresent[0] || !legPresent[1]);

            for (IntersectionState* st : members) {
                st->axisSplit[axis] = split;
                const int legAGreen = (axis == 0) ? 0 : 5;
                st->phaseDurations[legAGreen] =
                    split ? st->phaseDurations[legAGreen + 2] : 6.0f;
            }
        }
    }
}

// Group a traffic light's incoming edges into two opposing axes by compass
// angle. Axis 0 holds the first edge and anything roughly opposite it; axis 1
// gets the cross streets.
void PhysicsProcessor::initializeLightAxes(Node* node, IntersectionState& state)
{
    state.isInitialized = true;

    // Re-derivable from topology, so a re-init (runtime road edits) starts
    // clean instead of stacking stale or duplicate edge pointers.
    state.axisEdges[0].clear();
    state.axisEdges[1].clear();
    for (int axis = 0; axis < 2; axis++) {
        state.axisLegEdges[axis][0].clear();
        state.axisLegEdges[axis][1].clear();
    }

    std::vector<std::pair<Road*, double>> edgeAngles;

    for (uint64_t predNodeId : node->incomingEdgeNodeIds) {
        Node* predNode = network->getNode(predNodeId);
        if (!predNode) continue;
        for (Road& edge : predNode->outgoingEdges) {
            if (edge.getDest() == node->getId()) {
                // Calculate incoming compass angle using atan2
                double dx = node->getX() - predNode->getX();
                double dy = node->getY() - predNode->getY();
                double angle = atan2(dy, dx) * 180.0 / M_PI;
                if (angle < 0) angle += 360.0;

                edgeAngles.push_back({&edge, angle});
            }
        }
    }

    // Group roads into Axis 0 (Main Street) and Axis 1 (Cross Streets / T-Stems).
    // The baseline direction is normally this node's own first edge, but every
    // light of a multi-node junction cluster shares the master's baseline --
    // otherwise "axis 0" would mean a different physical direction at each
    // perimeter node and mirrored phases would still conflict in the box.
    if (!edgeAngles.empty()) {
        const double baselineAngle = (state.axisBaselineDeg >= 0.0)
            ? state.axisBaselineDeg
            : edgeAngles[0].second;

        for (size_t i = 0; i < edgeAngles.size(); i++) {
            double diff = std::abs(baselineAngle - edgeAngles[i].second);
            if (diff > 180.0) diff = 360.0 - diff;

            // Along the baseline line (either direction) = axis 0; roughly
            // perpendicular = axis 1.
            const int axis = (diff < 45.0 || diff > 135.0) ? 0 : 1;
            state.axisEdges[axis].push_back(edgeAngles[i].first);

            // Direction leg within the axis, folded against the axis's own
            // reference direction (baseline for axis 0, baseline+90 for axis
            // 1): within a quarter turn = leg 0, opposing = leg 1. Clustered
            // lights share the baseline, so leg indices mean the same
            // physical direction at every member node.
            double ref = baselineAngle + (axis == 1 ? 90.0 : 0.0);
            double d = std::fmod(std::abs(ref - edgeAngles[i].second), 360.0);
            if (d > 180.0) d = 360.0 - d;
            state.axisLegEdges[axis][d < 90.0 ? 0 : 1].push_back(edgeAngles[i].first);
        }
    }

    // Split-phasing decision per axis (see IntersectionState::axisSplit).
    // Clustered lights get these re-harmonized cluster-wide afterwards.
    for (int axis = 0; axis < 2; axis++) {
        int maxLanes = 0;
        for (const Road* edge : state.axisEdges[axis]) {
            maxLanes = std::max(maxLanes, edge->getLanes());
        }
        const bool oneLeg = state.axisLegEdges[axis][0].empty()
                         || state.axisLegEdges[axis][1].empty();
        state.axisSplit[axis] = !state.axisEdges[axis].empty()
                             && (maxLanes <= 2 || oneLeg);
    }

    // Phase timings derived from the roads the light controls (speeds in
    // m/s). The busier/faster axis gets the larger share of the straight
    // green budget, and yellows follow the kinematic clearance rule
    // (reaction time + v / 2a) so fast approaches aren't forced through the
    // dilemma-zone override every single cycle.
    double importance[2] = {0.0, 0.0};
    double maxSpeed[2]   = {0.0, 0.0};
    for (int axis = 0; axis < 2; axis++) {
        for (const Road* edge : state.axisEdges[axis]) {
            importance[axis] = std::max(importance[axis],
                edge->getSpeedLimit() * std::max(1, edge->getLanes()));
            maxSpeed[axis] = std::max(maxSpeed[axis], edge->getSpeedLimit());
        }
    }

    for (int axis = 0; axis < 2; axis++) {
        float green = 15.0f;
        if (importance[0] > 0.0 && importance[1] > 0.0) {
            green = std::clamp(
                static_cast<float>(STRAIGHT_GREEN_BUDGET * importance[axis] / (importance[0] + importance[1])),
                8.0f, 22.0f);
        }
        // 1s reaction + v/(2 * 3 m/s^2 comfortable deceleration); ~3.5s on a
        // 35 mph street, ~5s on a 55 mph arterial.
        float yellow = 4.0f;
        if (maxSpeed[axis] > 0.0) {
            yellow = std::clamp(1.0f + static_cast<float>(maxSpeed[axis]) / 6.0f, 3.0f, 6.0f);
        }

        const int straightGreen = (axis == 0) ? 2 : 7;
        state.phaseDurations[straightGreen]     = green;
        state.phaseDurations[straightGreen + 1] = yellow; // straight yellow
        state.phaseDurations[straightGreen - 1] = yellow; // leg-0 / protected-left yellow

        // On a split axis the phase that would be a brief protected-left
        // arrow is a full solo green for leg 0, so it gets the same green
        // budget as the leg-1 phase (actuation gaps either out early). A
        // conventional axis resets to the fixed arrow time in case a re-init
        // flipped the axis back from split.
        state.phaseDurations[straightGreen - 2] = state.axisSplit[axis] ? green : 6.0f;
    }
}

// One physical intersection between divided roads maps as 2-4 controlled
// nodes joined by short internal legs. Union-find those legs into clusters,
// then stamp every clustered traffic light with a sync master (lowest light
// id) and a shared axis baseline so the whole cluster runs one coordinated
// signal plan. Deterministic across reloads: masters and baselines derive
// from node ids and topology only.
void PhysicsProcessor::rebuildSignalClusters()
{
    junctionClusterOf.clear();
    for (auto& pair : intersections) {
        pair.second.bInCluster = false;
        pair.second.syncMasterId = 0;
        pair.second.axisBaselineDeg = -1.0;
    }
    if (network == nullptr) return;

    // Union-find over the endpoints of internal junction legs.
    std::unordered_map<uint64_t, uint64_t> parent;
    auto findRoot = [&parent](uint64_t id) -> uint64_t {
        while (true) {
            auto it = parent.find(id);
            if (it == parent.end() || it->second == id) return id;
            // Path halving keeps the walk short without recursion.
            auto grand = parent.find(it->second);
            if (grand != parent.end()) it->second = grand->second;
            id = parent[id];
        }
    };

    for (const auto& pair : network->getNodes()) {
        Node* node = network->getNode(pair.first);
        if (node == nullptr) continue;
        for (const Road& edge : node->outgoingEdges) {
            if (!RoadIntersectionUtil::IsInternalJunctionLeg(network, edge)) continue;
            uint64_t a = findRoot(edge.getOriginId());
            uint64_t b = findRoot(edge.getDest());
            if (a == b) continue;
            // Merge into the lower root so the representative is stable.
            if (b < a) std::swap(a, b);
            parent[b] = a;
            parent.emplace(a, a);
        }
    }

    // Collect members per cluster root.
    std::unordered_map<uint64_t, std::vector<uint64_t>> members;
    for (const auto& pair : parent) {
        members[findRoot(pair.first)].push_back(pair.first);
    }

    for (auto& pair : members) {
        std::vector<uint64_t>& ids = pair.second;
        if (ids.size() < 2) continue;
        std::sort(ids.begin(), ids.end());

        for (uint64_t id : ids) junctionClusterOf[id] = ids;

        // Lights in this cluster mirror the lowest-id light.
        uint64_t masterId = 0;
        bool haveMaster = false;
        for (uint64_t id : ids) {
            Node* n = network->getNode(id);
            if (n != nullptr && n->type == Node::TRAFFIC_LIGHT) { masterId = id; haveMaster = true; break; }
        }
        if (!haveMaster) continue;

        // Shared axis baseline: the lowest-id approach direction of the first
        // member (in id order) that has one. Usually the master's, but a
        // light with no incoming approaches must not leave the cluster
        // baseline-less -- members would each self-derive a different one
        // and mirrored phase indices would mean different directions.
        double baseline = -1.0;
        for (size_t m = 0; m < ids.size() && baseline < 0.0; ++m) {
            Node* anchor = network->getNode(ids[m]);
            if (anchor == nullptr) continue;
            uint64_t bestPred = std::numeric_limits<uint64_t>::max();
            for (uint64_t predId : anchor->incomingEdgeNodeIds) {
                Node* pred = network->getNode(predId);
                if (pred == nullptr || predId >= bestPred) continue;
                bestPred = predId;
                double angle = atan2(anchor->getY() - pred->getY(),
                                     anchor->getX() - pred->getX()) * 180.0 / M_PI;
                if (angle < 0) angle += 360.0;
                baseline = angle;
            }
        }

        for (uint64_t id : ids) {
            Node* n = network->getNode(id);
            if (n == nullptr || n->type != Node::TRAFFIC_LIGHT) continue;
            IntersectionState& st = intersections[id];
            st.bInCluster = true;
            st.syncMasterId = masterId;
            st.axisBaselineDeg = baseline;
        }
    }
}

// Demand sensors for a light's ring, widened to its whole cluster: the master
// actuates for a car waiting at ANY perimeter node, not just its own.
bool PhysicsProcessor::clusterAxisHasDemand(Node* node, const IntersectionState& state, int axis)
{
    if (axisHasDemand(node, state, axis)) return true;
    if (!state.bInCluster) return false;

    auto cit = junctionClusterOf.find(node->getId());
    if (cit == junctionClusterOf.end()) return false;
    for (uint64_t memberId : cit->second) {
        if (memberId == node->getId()) continue;
        Node* member = network->getNode(memberId);
        auto sit = intersections.find(memberId);
        if (member == nullptr || sit == intersections.end()) continue;
        if (member->type != Node::TRAFFIC_LIGHT) continue;
        if (axisHasDemand(member, sit->second, axis)) return true;
    }
    return false;
}

bool PhysicsProcessor::clusterLeftTurnDemand(Node* node, const IntersectionState& state, int axis)
{
    if (checkLeftTurnDemand(node, state, axis)) return true;
    if (!state.bInCluster) return false;

    auto cit = junctionClusterOf.find(node->getId());
    if (cit == junctionClusterOf.end()) return false;
    for (uint64_t memberId : cit->second) {
        if (memberId == node->getId()) continue;
        Node* member = network->getNode(memberId);
        auto sit = intersections.find(memberId);
        if (member == nullptr || sit == intersections.end()) continue;
        if (member->type != Node::TRAFFIC_LIGHT) continue;
        if (checkLeftTurnDemand(member, sit->second, axis)) return true;
    }
    return false;
}

bool PhysicsProcessor::legHasDemand(Node* node, const IntersectionState& state, int axis, int leg)
{
    for (Road* incomingRoad : state.axisLegEdges[axis][leg]) {
        if (approachHasDemand(incomingRoad, node)) return true;
    }
    return false;
}

bool PhysicsProcessor::clusterLegHasDemand(Node* node, const IntersectionState& state, int axis, int leg)
{
    if (legHasDemand(node, state, axis, leg)) return true;
    if (!state.bInCluster) return false;

    auto cit = junctionClusterOf.find(node->getId());
    if (cit == junctionClusterOf.end()) return false;
    for (uint64_t memberId : cit->second) {
        if (memberId == node->getId()) continue;
        Node* member = network->getNode(memberId);
        auto sit = intersections.find(memberId);
        if (member == nullptr || sit == intersections.end()) continue;
        if (member->type != Node::TRAFFIC_LIGHT) continue;
        if (legHasDemand(member, sit->second, axis, leg)) return true;
    }
    return false;
}

float PhysicsProcessor::getRouteSegmentLength(VehicleState* vhcl, int routeIndex) {
    // Safety bounds check (Fixed to prevent unsigned underflow)
    if (routeIndex < 0 || routeIndex + 1 >= vhcl->currentRoute.size()) {
        return 0.0f; 
    }

    int startNodeId = vhcl->currentRoute[routeIndex];
    int endNodeId = vhcl->currentRoute[routeIndex + 1];

    Node* startNode = network->getNode(startNodeId);
    if (!startNode) return 0.0f;

    // Find the edge connecting these two nodes and return its length
    for (Road& edge : startNode->outgoingEdges) {
        if (edge.getDest() == endNodeId) {
            return edge.getLength();
        }
    }
    return 0.0f;
}

float PhysicsProcessor::calculateTrueGap(VehicleState* follower, VehicleState* leader) {
    // 1. If they are on the exact same physical road edge
    if (follower->getCurrentEdge() == leader->getCurrentEdge()) {
        if (follower->getPos() >= leader->getPos()) return 0.0f; // Follower is in front
        return std::max(0.0f, leader->getPos() - follower->getPos() - leader->getLength());
    }

    // 2. Otherwise, calculate the multi-segment gap
    float totalGap = 0.0f;
    float followerRoadLen = getRouteSegmentLength(follower, follower->currentRouteIndex);
    totalGap += (followerRoadLen - follower->getPos());

    bool leaderFound = false;

    // Trace forward strictly along the follower's route
    for (size_t i = follower->currentRouteIndex + 1; i < follower->currentRoute.size() - 1; i++) {
        int stepStartNode = follower->currentRoute[i];
        int stepEndNode = follower->currentRoute[i+1];
        
        // ---> FIX: Safely ask the physical road for its nodes instead of checking the route array <---
        int leaderStartNode = -1;
        int leaderEndNode = -1;
        if (leader->getCurrentEdge()) {
            leaderStartNode = leader->getCurrentEdge()->getOriginId();
            leaderEndNode = leader->getCurrentEdge()->getDest();
        }

        if (stepStartNode == leaderStartNode && stepEndNode == leaderEndNode) {
            leaderFound = true;
            break;
        }
        totalGap += getRouteSegmentLength(follower, i);
    }

    if (!leaderFound) {
        // Leader is not physically on the follower's remaining route 
        // (Could happen if leader is turning off the route or spatial hash is 1 frame stale)
        return 9999.0f; 
    }

    totalGap += leader->getPos();
    totalGap -= leader->getLength();

    return std::max(0.0f, totalGap);
}

float PhysicsProcessor::calculateDistanceToDestination(VehicleState* vhcl) 
{
    float totalDist = 0.0f;
    
    // 1. Remaining distance on the current road
    float currentRoadLen = getRouteSegmentLength(vhcl, vhcl->currentRouteIndex);
    totalDist += (currentRoadLen - vhcl->getPos());

    // 2. Sum of all remaining roads in the route
    for (size_t i = vhcl->currentRouteIndex + 1; i < vhcl->currentRoute.size() - 1; i++) {
        totalDist += getRouteSegmentLength(vhcl, i);
    }
    
    return std::max(0.0f, totalDist);
}

void PhysicsProcessor::update(float dt)
{   
    // hide cars marked for deletion
    std::vector<VehicleState*> livingVehicles;
    for(VehicleState* v : vehicleList) {
        if(!v->isMarkedForDeletion) {
            livingVehicles.push_back(v);
        }
    }

    // Rebuild hash ONLY with living vehicles to prevent dead-pointer reads
    spatialHash->rebuild(livingVehicles);
    
    updateIntersections(dt);

    // ==========================================
    // PASS 0: MOBIL & LANE CHANGING
    // ==========================================
    // Note on same-frame ordering: a change committed earlier in this pass
    // updates that car's live lane/isChangingLanes state, which the conflict
    // veto below reads through the edge bucket, so two cars flanking the
    // same gap cannot both converge into it in one frame.
    for (VehicleState* vhcl : vehicleList)
    {
        Road* currentEdge = vhcl->getCurrentEdge();
        
        // ---> CRITICAL FIX 1: Guard against missing edges <---
        // If a car spawned with a bad route and has no edge, trying to get lanes will segfault!
        if (currentEdge == nullptr) continue;

        // Advance any transition in progress; while sliding between lanes (or
        // cooling down afterwards) the car doesn't make a new MOBIL decision.
        vhcl->updateLaneChange(dt);
        if (!vhcl->canStartLaneChange()) continue;

        int currentLane = vhcl->getLane();
        int totalLanes = currentEdge->getLanes(); 

        // if within 150 meters of intersection, check if car needs to turn/move lanes
        float distanceToIntersection = currentEdge->getLength() - vhcl->getPos();
        std::string upcomingTurn = "through";
        if (distanceToIntersection < 150.0f) {
            // Box-aware: a left across a multi-node junction reads "through"
            // per hop, but the car must still be walked into the left-turn
            // lane. U-turns need the same lane as a left.
            bool uTurnAhead = false;
            upcomingTurn = getUpcomingBoxMovement(vhcl, uTurnAhead);
            if (uTurnAhead) upcomingTurn = "left";
        }
        Node* destNode = (network != nullptr) ? network->getNode(currentEdge->getDest()) : nullptr;

        // Lane guidance from the edge's turn map: aim for the nearest lane
        // that permits the upcoming movement (this also walks through-cars
        // out of dedicated turn lanes). Edges without a map -- runtime roads
        // before inference re-runs -- fall back to the old edge-of-road
        // heuristic. Guidance firms up as the intersection nears.
        uint8_t neededMovement = 0;
        int guidedLane = currentLane;
        bool haveGuidance = false;
        if (distanceToIntersection < 150.0f) {
            neededMovement = (upcomingTurn == "left")  ? TurnLane::Left
                           : (upcomingTurn == "right") ? TurnLane::Right
                                                       : TurnLane::Through;
            int allowedLane = currentEdge->nearestLaneAllowing(currentLane, neededMovement);
            if (allowedLane >= 0) {
                guidedLane = allowedLane;
                haveGuidance = true;
            } else if (upcomingTurn == "left") {
                guidedLane = 0;
                haveGuidance = true;
            } else if (upcomingTurn == "right") {
                guidedLane = totalLanes - 1;
                haveGuidance = true;
            }

            // Lane-drop guidance: a through car whose next edge is narrower
            // than its current lane index gets clamp-snapped left at the
            // handoff, and the pavement itself tapers away under it on the
            // approach -- merge out of the dropping lane instead of riding
            // it to the end.
            if (upcomingTurn == "through" && destNode != nullptr &&
                vhcl->currentRouteIndex + 2 < vhcl->currentRoute.size()) {
                for (Road& nextEdge : destNode->outgoingEdges) {
                    if (nextEdge.getDest() != vhcl->currentRoute[vhcl->currentRouteIndex + 2]) continue;
                    if (currentLane >= nextEdge.getLanes()) {
                        const int dropLane = std::max(0, nextEdge.getLanes() - 1);
                        guidedLane = haveGuidance ? std::min(guidedLane, dropLane) : dropLane;
                        haveGuidance = true;
                    }
                    break;
                }
            }
        }
        // No lane maneuvers inside a junction box: past the stop line at the
        // end of this edge, or still crossing the previous node's box at the
        // start of it. A change begun there plays out across the intersection
        // sweep and lands the car sideways across the arrival lanes.
        float stopLine = destNode != nullptr
            ? stopLineArcPos(network, currentEdge, destNode)
            : static_cast<float>(currentEdge->getLength());
        if (vhcl->getPos() > stopLine) continue;
        if (network != nullptr && destNode != nullptr && vhcl->currentRouteIndex > 0) {
            Node* originNode = network->getNode(currentEdge->getOriginId());
            if (originNode != nullptr) {
                float sbStart = RoadIntersectionUtil::GetNodeSetbackMeters(
                    network, *originNode, RoadIntersectionUtil::MedianGapMeters);
                float sbEnd = RoadIntersectionUtil::GetNodeSetbackMeters(
                    network, *destNode, RoadIntersectionUtil::MedianGapMeters);
                RoadIntersectionUtil::ClampSetbacksToLength(
                    static_cast<float>(currentEdge->getLength()), sbStart, sbEnd);
                if (vhcl->getPos() < sbStart) continue;
            }
        }

        // Held at a controlled intersection: a stopped or creeping car
        // waiting on a signal/stop it cannot enter gains nothing from
        // jockeying into the adjacent queue, and the lateral slide would
        // play out at zero speed exactly where cars are packed tightest.
        // Suppress discretionary changes; guidance toward a lane the
        // upcoming movement requires stays enabled.
        bool heldAtControl = false;
        if (vhcl->getSpeed() < 2.0f && destNode != nullptr && !canVehicleEnter(vhcl, destNode)) {
            heldAtControl = true;
        }

        // Stationary cars don't change lanes, period -- that lateral slide at
        // zero speed is what reads as cars randomly swapping lanes while
        // queued. The one exception: a car held AT the stop line in a lane
        // its movement isn't allowed from may still slide over into the
        // required lane (the alternative is a wrong-lane turn once the
        // wrong-lane gate in canVehicleEnter times out).
        bool stationary = vhcl->getSpeed() < 1.5f;
        bool stationaryGuidanceOk = heldAtControl && haveGuidance && guidedLane != currentLane
            && (stopLine - vhcl->getPos()) < 15.0f;
        if (stationary && !stationaryGuidanceOk) continue;
        if (heldAtControl && guidedLane == currentLane) continue;

        float urgency = 100.0f + std::max(0.0f, 150.0f - distanceToIntersection);

        // Bias a candidate lane change toward the guided lane; when already
        // in a valid lane, penalize drifting into one the movement can't be
        // made from. Skips crash-vetoed (-999) candidates.
        auto applyGuidance = [&](float incentive, int candidateLane) -> float {
            if (!haveGuidance || incentive <= -500.0f) return incentive;
            if (guidedLane == currentLane) {
                if (!currentEdge->laneAllows(candidateLane, neededMovement)) incentive -= urgency;
            } else if ((guidedLane < currentLane) == (candidateLane < currentLane)) {
                incentive += urgency; // toward the required lane
            } else {
                incentive -= urgency; // away from it
            }
            return incentive;
        };

        int bestLane = currentLane;
        float threshold = 0.1f;
        float bestIncentive = threshold;

        //check left
        if (currentLane > 0 && (!heldAtControl || guidedLane < currentLane)) {
            float leftIncentive = applyGuidance(MOBIL(vhcl, currentLane - 1), currentLane - 1);
            if (leftIncentive > bestIncentive) {
               bestLane = currentLane - 1;
               bestIncentive = leftIncentive;
           }
       }

       //check right
       if (currentLane < totalLanes - 1 && (!heldAtControl || guidedLane > currentLane)) {
            float rightIncentive = applyGuidance(MOBIL(vhcl, currentLane + 1), currentLane + 1);
            if (rightIncentive > bestIncentive) {
               bestLane = currentLane + 1;
               bestIncentive = rightIncentive;
           }
       }
        // take lane with best MOBIL incentive; the change plays out over a
        // politeness-scaled interval rather than snapping instantly
       if (bestLane != currentLane) {
           // Lateral-path conflict veto. Two sweeps may not cross or share a
           // gap: a car mid-change (or one that started earlier this pass --
           // its committed lane is already live in the hash bucket) occupies
           // every lane between its old and new lane, so if that interval
           // touches the interval this car would sweep through and the
           // longitudinal gap is too small, the two bodies would overlap on
           // screen. Settled cars only conflict when parked in the target
           // lane itself (MOBIL's leader/follower checks cover the dynamics;
           // this covers the geometry).
           auto sweepConflicts = [&](VehicleState* other) -> bool {
               if (other == nullptr || other == vhcl || other->isMarkedForDeletion) return false;

               int myLo = std::min(currentLane, bestLane);
               int myHi = std::max(currentLane, bestLane);
               int otLo, otHi;
               if (other->isChangingLanes()) {
                   otLo = std::min(other->getPreviousLane(), other->getLane());
                   otHi = std::max(other->getPreviousLane(), other->getLane());
               } else if (other->getLane() == bestLane) {
                   otLo = otHi = other->getLane();
               } else {
                   return false;
               }
               if (otHi < myLo || otLo > myHi) return false; // paths never meet

               bool otherAhead = other->getPos() >= vhcl->getPos();
               float gap = otherAhead
                   ? other->getPos() - other->getLength() - vhcl->getPos()
                   : vhcl->getPos() - vhcl->getLength() - other->getPos();
               float required = 0.5f * (otherAhead ? vhcl->getMinGap() : other->getMinGap());
               return gap < required;
           };

           bool conflict = false;
           for (VehicleState* other : spatialHash->getVehiclesOnRoad(currentEdge)) {
               if (sweepConflicts(other)) { conflict = true; break; }
           }
           if (!conflict) {
               vhcl->startLaneChange(bestLane);
           }
       }
    }
    vehicleUpdates.clear();
    
    // ==========================================
    // PASS 1: CALCULATE INTENDED PHYSICS
    // ==========================================
    for (VehicleState* vhcl : vehicleList)
    {
        // ---> ARRAY DESYNC FIX <---
        if (vhcl == nullptr) {
            vehicleUpdates.push_back(0.0f);
            continue;
        }

        applyJunctionTargetSpeed(vhcl);

        vhcl->setLeader(getLeader(vhcl, vhcl->getLane()));
        float acceleration = IDM(vhcl, vhcl->getLeader(), false);

        // While straddling two lanes mid-change, also respect the leader in
        // the lane being vacated and follow whichever is more restrictive.
        if (vhcl->isChangingLanes() && vhcl->getPreviousLane() != vhcl->getLane()) {
            VehicleState* oldLaneLeader = getLeader(vhcl, vhcl->getPreviousLane());
            if (oldLaneLeader != nullptr) {
                acceleration = std::min(acceleration, IDM(vhcl, oldLaneLeader, false));
            }
        }

        // Per-driver reaction lag: hold the launch for reactionTime after the
        // light/queue first lets this stopped car go. Aggressive drivers jump
        // the green in ~0.3s, cautious ones sit for up to ~2s.
        acceleration = vhcl->applyReactionDelay(acceleration, dt);

        float dv = acceleration * dt;

        vhcl->setAcceleration(acceleration);
        vhcl->updateWaitTime(dt);

        if (vhcl->getSpeed() + dv < 0.0f) {
            dv = -vhcl->getSpeed();
        }
        vehicleUpdates.push_back(dv);
    }

    // ==========================================
    // PASS 2: APPLY MOVEMENT & ROUTING
    // ==========================================
    int i = 0;
    for (VehicleState* vhcl : vehicleList)
    {
        if (vhcl == nullptr) {
            i++; 
            continue;
        }

        // ---> CONTINUOUS DESTINATION CHECK <---
        // Despawn at speed as the car reaches its destination's junction
        // boundary. Braking to a stop to "park" left dead cars sitting in
        // live lanes and junction boxes; the trip is over either way, so the
        // car vanishes at the box edge (or at the node for mid-road
        // destinations) without ever slowing the traffic behind it.
        float despawnReach = 0.5f;
        if (!vhcl->currentRoute.empty()) {
            Node* finalNode = network->getNode(vhcl->currentRoute.back());
            if (finalNode) {
                despawnReach += RoadIntersectionUtil::GetNodeSetbackMeters(
                    network, *finalNode, RoadIntersectionUtil::MedianGapMeters);
            }
        }
        if (calculateDistanceToDestination(vhcl) < despawnReach)
        {
            // Flag for removal from the active physics loop
            vhcl->isMarkedForDeletion = true;
            vehiclesToRemove.push_back(vhcl);

            for (VehicleState* otherCar : vehicleList) 
            {
                if (otherCar->getLeader() == vhcl) {
                    otherCar->setLeader(nullptr);
                }
            }
            i++; 
            continue; 
        }

        // Apply physics
        if (!vhcl->isMarkedForDeletion) {
            vhcl->accelerate(vehicleUpdates[i]);
            vhcl->move(vhcl->getSpeed() * dt);
        }

        bool routeAdvanced = true;
        while (routeAdvanced && !vhcl->currentRoute.empty() && vhcl->currentRouteIndex < vhcl->currentRoute.size() - 1) 
        {
            routeAdvanced = false;

            int currentNodeId = vhcl->currentRoute[vhcl->currentRouteIndex];
            int nextNodeId = vhcl->currentRoute[vhcl->currentRouteIndex + 1];

            Node* currentNode = network->getNode(currentNodeId);
            
            // ---> CRITICAL FIX 2: Null Check Node Lookups <---
            if (!currentNode) break; 
            
            // Look up the length of the road we are currently driving on
            double currentRoadLength = 0.0;
            for (Road& edge : currentNode->outgoingEdges) {
                if (edge.getDest() == nextNodeId) {
                    currentRoadLength = edge.getLength();
                    break;
                }
            }

            // ---> CRITICAL FIX 3: Prevent Infinite Loops <---
            // If the edge was missing, currentRoadLength is 0.0.
            // getPos() >= 0.0 is always true, causing an infinite while loop!
            if (currentRoadLength <= 0.001f) break;

            if (vhcl->getPos() >= currentRoadLength) 
            {
                // Prevent Zero-Length Infinite Loop
                if (currentRoadLength <= 0.01f) {
                    vhcl->setPos(0.0f);
                } else {
                    vhcl->setPos(vhcl->getPos() - currentRoadLength); 
                }

                vhcl->currentRouteIndex++;
                routeAdvanced = true;

                if (vhcl->currentRouteIndex >= vhcl->currentRoute.size() - 1)
                {
                    // End of route: keep rolling at the current target speed;
                    // the destination check above despawns the car within a
                    // frame or two instead of parking it in the road.
                    break; // EXIT THE WHILE LOOP IMMEDIATELY!
                }
                else 
                {
                    int newCurrentNodeId = vhcl->currentRoute[vhcl->currentRouteIndex];
                    int newNextNodeId = vhcl->currentRoute[vhcl->currentRouteIndex + 1];
                    Node* newCurrentNode = network->getNode(newCurrentNodeId);
                    
                    // ---> CRITICAL FIX 4: Null check the next node <---
                    if (newCurrentNode) 
                    {
                        for (Road& edge : newCurrentNode->outgoingEdges) {
                            if (edge.getDest() == newNextNodeId) {
                                vhcl->setDesiredSpeed(edge.getSpeedLimit());
                                
                                // NEW: Volume swapping
                                Road* oldEdge = vhcl->getCurrentEdge();
                                if (oldEdge) oldEdge->removeVehicle();
                                edge.addVehicle(); 
                                
                                vhcl->setCurrentEdge(&edge);

                                // Land in the lane the movement arrives in
                                // (GetArrivalLane, rank-aware: each of two
                                // side-by-side turn lanes feeds its own
                                // arrival lane; through keeps its lane
                                // clamped to the new road's width). The
                                // spatial hash sensors and the renderer's
                                // blend target use the same rule, so what
                                // the car braked for is what it lands behind.
                                std::string turnMade = getTurnDirectionAt(vhcl, vhcl->currentRouteIndex);
                                RoadIntersectionUtil::TurnDir dir =
                                      (turnMade == "right") ? RoadIntersectionUtil::TurnDir::Right
                                    : (turnMade == "left")  ? RoadIntersectionUtil::TurnDir::Left
                                                            : RoadIntersectionUtil::TurnDir::Through;
                                vhcl->setLane(RoadIntersectionUtil::GetArrivalLane(dir, vhcl->getLane(), edge.getLanes(), oldEdge));

                                break;
                            }
                        }
                    }
                }
            }
        }
        
        i++;
    }

    // ==========================================
    // DEAD CAR COLLECTION PHASE
    // ==========================================
    for (VehicleState* deadVhcl : vehiclesToDestroy)
    {
        if (network != nullptr) {
            for (auto& pair : intersections) {
                if (pair.second.currentOccupant == deadVhcl) {
                    pair.second.currentOccupant = nullptr;
                }
                
                // clear dead car from wait queues
                std::queue<VehicleState*> safeQueue;
                while (!pair.second.waitQueue.empty()) {
                    VehicleState* waitingCar = pair.second.waitQueue.front();
                    pair.second.waitQueue.pop();
                    
                    // Only keep cars that aren't about to be deleted
                    if (waitingCar != deadVhcl) {
                        safeQueue.push(waitingCar);
                    }
                }
                pair.second.waitQueue = safeQueue;
            }
        }
        delete deadVhcl;
    }
    vehiclesToDestroy.clear();

    // ==========================================
    // PARKED CAR COLLECTION PHASE
    // ==========================================
    for (VehicleState* parkedVehicle : vehiclesToRemove) 
    {
        // Unregister the vehicle from the road before deleting it
        if (parkedVehicle->getCurrentEdge()) {
            parkedVehicle->getCurrentEdge()->removeVehicle();
        }

        vehicleList.erase(
            std::remove(vehicleList.begin(), vehicleList.end(), parkedVehicle), 
            vehicleList.end()
        );

        vehiclesToDestroy.push_back(parkedVehicle); // Destroy it next frame
    }
    
    vehiclesToRemove.clear();
}

void PhysicsProcessor::despawnVehicle(VehicleState* vhcl)
{
    if (!vhcl) return;

    for (VehicleState* other : vehicleList)
    {
        if (other && other->getLeader() == vhcl) other->setLeader(nullptr);
    }

    // Callers despawn before mutating the graph, so currentEdge is still a
    // valid pointer here and the volume counter can be balanced.
    if (vhcl->getCurrentEdge()) vhcl->getCurrentEdge()->removeVehicle();

    vehicleList.erase(
        std::remove(vehicleList.begin(), vehicleList.end(), vhcl),
        vehicleList.end()
    );

    delete vhcl;
}

float PhysicsProcessor::IDM(VehicleState* vhcl, VehicleState* leader, bool mobil )
{
    if (vhcl == nullptr || vhcl->isMarkedForDeletion) return 0.0f;

    if (vhcl->getCurrentEdge() == nullptr) {
        return 0.0f; 
    }
    if (leader != nullptr && leader->isMarkedForDeletion) {
        leader = nullptr; // Pretend the road is clear
    }
    
    float safeDesiredSpeed = std::max(vhcl->getDesiredSpeed(), 0.001f);
    float freeRoadRatio = pow((vhcl->getSpeed() / safeDesiredSpeed), vhcl->getAccelExp());

    float interactionTerm = 0.0f;
    
    // Default to an infinitely far road
    float effectiveGap = 99999.0f; 
    float approachSpeed = 0.0f; // deltaV

    // --- 1. Check Physical Leader ---
    // (The destination is no longer a virtual leader: cars despawn at speed
    // when they reach it instead of braking to a stop in a live lane.)
    if (leader != nullptr) {
        effectiveGap = calculateTrueGap(vhcl, leader);
        approachSpeed = vhcl->getSpeed() - leader->getSpeed();
    }

    // --- 2. Standard IDM Calculation ---
    // Only calculate the interaction term if there is actually a reason to brake
    if (effectiveGap < 9999.0f) 
    {
        if (effectiveGap <= 0.0001f) effectiveGap = 0.001f; // prevent division by 0

        float top = vhcl->getSpeed() * approachSpeed; 
        float bottom = 2.0f * sqrt(vhcl->getMaxAccel() * vhcl->getSafeBrakePower()); 
        
        float dynamicGap = (vhcl->getSpeed() * vhcl->getSafeTimeHeadway()) + (top / bottom);
        float desiredGap = vhcl->getMinGap() + std::max(0.0f, dynamicGap);
        
        interactionTerm = pow((desiredGap / effectiveGap), 2);

    }

    float finalAccel = vhcl->getMaxAccel() * (1.0f - freeRoadRatio - interactionTerm);

    // Standing-start kick: amplify drive acceleration (only positive accel,
    // never braking) while the car is pulling away from a full stop, fading
    // out as it gets up to speed. Mimics how real drivers launch from lights
    // and stop signs rather than easing away at the free-road ramp.
    if (finalAccel > 0.0f) {
        finalAccel *= vhcl->getLaunchBoost();
    }

    // Apply a realistic physical limit for a hard emergency stop.
    // Tires lose grip around -9.8 m/s^2. Clamping it here prevents math explosions
    // while still simulating heavy emergency braking telemetry.
    float maxPhysicalDeceleration = -10.0f; 

    return std::max(maxPhysicalDeceleration, finalAccel);
}

void PhysicsProcessor::addVehicle(VehicleState* vhcl)
{
    vehicleList.push_back(vhcl);

    // Initialize the vehicle's current road edge upon entering the simulation
    if (vhcl->getCurrentEdge() == nullptr && vhcl->currentRoute.size() > 1) 
    {
        int currentNodeId = vhcl->currentRoute[vhcl->currentRouteIndex];
        int nextNodeId = vhcl->currentRoute[vhcl->currentRouteIndex + 1];

        Node* currentNode = network->getNode(currentNodeId);
        if (currentNode) {
            for (Road& edge : currentNode->outgoingEdges) {
                if (edge.getDest() == nextNodeId) {
                    vhcl->setCurrentEdge(&edge); // Set the initial edge
                    edge.addVehicle();
                    break;
                }
            }
        }
        // debugging
        if (vhcl->getCurrentEdge() == nullptr) {
            std::cerr << "[WARNING] Vehicle " << vhcl->getId() 
                      << " failed to bind to Edge! Route: " 
                      << currentNodeId << " -> " << nextNodeId << ". Aborting spawn." << std::endl;
            
            // Delete the car and remove it from the list immediately
            vehicleList.pop_back(); 
            delete vhcl;
        }
    }
}

float  PhysicsProcessor::MOBIL(VehicleState* vhcl, int targetLane)
{
    // need to implement spatial logic for finding leaders and followers
    VehicleState* newLeader = getLeader(vhcl, targetLane);
    VehicleState* newFollower = getFollower(vhcl, targetLane);
    VehicleState* oldFollower = getFollower(vhcl, vhcl->getLane());
    VehicleState* curLeader = vhcl->getLeader();

    float politeness = vhcl->getPoliteness(); // 0 is selfish, 1 is selfless
    float safeBrake = 2.0f; // b_safe, max deceleration vehicle can cause on new follower

    // saftey criterion, check if lane change is safe to do 
    
    float newFollowerAccel = 0.0f;
    if (newFollower != nullptr) {
        newFollowerAccel = IDM(newFollower, vhcl, true);
        if (newFollowerAccel < -safeBrake) { //note accel is negative for braking
            return -999.0f; // not safe to change
        }
    }

    // Leader-side crash veto, symmetric with the follower check: sliding in
    // behind a leader with no physical gap is a collision, and it must come
    // back as -999 (not merely a bad accel gain) because the intersection
    // guidance bias can outweigh any finite incentive.
    if (newLeader != nullptr &&
        calculateTrueGap(vhcl, newLeader) < 0.5f * vhcl->getMinGap()) {
        return -999.0f; // not safe to change
    }

    // incentive criterion, acceralation gained
    float curAccel = IDM(vhcl, curLeader, true);
    float newAccel = IDM(vhcl, newLeader, true);
    float newAccelGain = newAccel - curAccel;

    // effect on new follower
    float newFollowerGain = 0.0f;
    if (newFollower != nullptr) {
        float newFAccelBefore = IDM(newFollower, newLeader, true); 
        newFollowerGain = newFollowerAccel - newFAccelBefore;
    }

    // effect on new follower 
    float oldFollowerGain = 0.0f;
    if (oldFollower != nullptr) {
        float oldFollowerAccel = IDM(oldFollower, vhcl, true);
        float oldFAccelAfter = IDM(oldFollower, curLeader, true);
        oldFollowerGain = oldFAccelAfter - oldFollowerAccel;
    }

    float incentive= newAccelGain +politeness*(newFollowerGain + oldFollowerGain);
    return incentive;
    
}

VehicleState* PhysicsProcessor::getLeader(VehicleState* vhcl, int targetLane)
{
    // 1. Get the physical car ahead using the optimized spatial hash (from your teammate)
    VehicleState* physicalLeader = spatialHash->getLeader(vhcl, targetLane, network);
    
    // another safety check 
    if (!VehicleState::isSafe(physicalLeader)) {
        physicalLeader = nullptr;
    }
    if (physicalLeader != nullptr) {
        if(physicalLeader->isMarkedForDeletion) {
            return  nullptr;
        }
    }
    // Calculate distance to the physical leader
    float physicalDistance = std::numeric_limits<float>::max();
    if (physicalLeader != nullptr) {
        physicalDistance = calculateTrueGap(vhcl, physicalLeader);
    }

    // 2. Check intersection and place virtual/ghost vehicle if needed (from your branch)
    if (network != nullptr) {
        Road* currentRoad = vhcl->getCurrentEdge();
        
        if (currentRoad != nullptr) {
            uint64_t destNodeId = currentRoad->getDest();
            Node* destNode = network->getNode(destNodeId);

            // If the vehicle cannot enter the intersection...
            if (destNode != nullptr && !canVehicleEnter(vhcl, destNode)) {

                float stopLinePos = stopLineArcPos(network, currentRoad, destNode);
                float distanceToStopLine = stopLinePos - vhcl->getPos();

                // If the stop line is closer than the physical leader, yield to the stop line
                if (distanceToStopLine > 0.0f && distanceToStopLine < physicalDistance) {

                    std::pair<Road*, int> laneKey = std::make_pair(currentRoad, vhcl->getLane());

                    if (ghostVehicles.find(laneKey) == ghostVehicles.end()) {
                        IDMParameters dummyParams = {1.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f};
                        VehicleState* ghost = new VehicleState(0, 0, 0.0f, stopLinePos, vhcl->getLane(), dummyParams);
                        // calculateTrueGap locates a leader by its edge pointer;
                        // without an edge the ghost reads as "not on the route"
                        // and the gap comes back 9999, so nothing brakes for it.
                        ghost->setCurrentEdge(currentRoad);
                        ghostVehicles[laneKey] = ghost;
                    }

                    VehicleState* ghost = ghostVehicles[laneKey];
                    // Runtime road edits can change lane counts at the node and
                    // move the junction-box edge, so refresh a cached ghost.
                    ghost->setPos(stopLinePos);
                    ghost->currentRouteIndex = vhcl->currentRouteIndex;

                    return ghost;
                }
            }
        }
    }

    // 3. Otherwise, return the physical leader (or nullptr if the road is completely clear)
    return physicalLeader;
}

VehicleState* PhysicsProcessor::getFollower(VehicleState* vhcl, int targetLane)
{
    return spatialHash->getFollower(vhcl, targetLane, network);
}

PhysicsProcessor::~PhysicsProcessor() 
{
    for (VehicleState* v : vehicleList)
    {
        delete(v);
    }
    
    // --- Memory Cleanup from main ---
    for (VehicleState* v : vehiclesToDestroy)
    {
        delete(v);
    }

    // --- Delete ghost vehicles from intersection branch ---
    for (auto& pair : ghostVehicles) 
    {
        delete pair.second;
    }
}

// --- Intersection Logic ---

// gets called in update(), maybe need to look at performance 
void PhysicsProcessor::updateIntersections(float dt)
{
     if (network == nullptr) return;

    for (auto& pair : intersections) {
        uint64_t nodeId = pair.first; 
        IntersectionState& state = pair.second;
        Node* node = network->getNode(nodeId);

        if (node == nullptr) continue;

    // 4 way stop logic
    if (node->type == Node::FOUR_WAY_STOP) {

        // safety check
        if (state.currentOccupant != nullptr && !state.currentOccupant->isAlive()) {
            state.currentOccupant = nullptr; 
        }
        // check if there is a car already in the intersection
        if (state.currentOccupant != nullptr) {
            state.occupantHeldTime += dt;

            // deletion check
            if (state.currentOccupant->isMarkedForDeletion) {
                state.currentOccupant = nullptr;
            }
            // check if car cleared intersection
            else {
                Road* currentEdge = state.currentOccupant->getCurrentEdge();
                if (currentEdge) {
                    if (currentEdge->getDest() != nodeId || state.currentOccupant->getPos() > currentEdge->getLength() + 5.0f) {
                        if (state.currentOccupant->getPos() > 5.0f) {
                            state.currentOccupant = nullptr;
                        }
                    }
                }

                // Watchdog: a normal grant-to-clear traversal takes a few
                // seconds even for a truck from a standstill. Anything held
                // far longer is wedged (blocked exit edge, car that slipped
                // through without clearing) -- release it so the rest of the
                // intersection keeps flowing. The released car simply
                // re-queues from the stop line if it still needs to cross.
                if (state.currentOccupant != nullptr && state.occupantHeldTime > 12.0f) {
                    state.currentOccupant = nullptr;
                }
            }
        }

        // pop queue: grant only to a car actually waiting at its stop line
        // with a clear path into the box. Entries that queued and then got
        // boxed in behind a non-occupant (lane change, follower queueing
        // before its leader) or already drove through are discarded; they
        // re-queue once they are genuinely first at the line.
        if (state.currentOccupant == nullptr) {
            while (!state.waitQueue.empty()) {
                VehicleState* candidate = state.waitQueue.front();
                state.waitQueue.pop();

                if (candidate->isMarkedForDeletion) continue;
                if (!isAtStopLine(candidate, node)) continue;

                state.currentOccupant = candidate;
                state.occupantHeldTime = 0.0f;
                break;
            }
        }
    }
        
        // traffic light logic, working on multi directional phases
        else if (node->type == Node::TRAFFIC_LIGHT) {

            // Lights are initialized in the constructor; this covers nodes
            // created after startup (e.g. runtime road edits).
            if (!state.isInitialized) {
                initializeLightAxes(node, state);
            }

            // Clustered perimeter light: mirror the master's phase verbatim
            // (axes were grouped against the shared baseline, so phase
            // indices mean the same directions here). Only the master runs
            // the ring below -- two nodes of one physical junction stepping
            // their own timers is exactly the desync that sent cross traffic
            // into the box together.
            if (state.bInCluster && state.syncMasterId != nodeId) {
                auto masterIt = intersections.find(state.syncMasterId);
                if (masterIt != intersections.end()) {
                    state.currentPhase = masterIt->second.currentPhase;
                    state.lightTimer = masterIt->second.lightTimer;
                    continue;
                }
            }

            // 10-phase actuated ring. Ring order is fixed (0 N/S left green,
            // 1 its yellow, 2 N/S straight green, 3 its yellow, 4 all-red,
            // 5-9 the E/W mirror); durations come from phaseDurations and
            // demand decides which phases actually get served. On a
            // split-phased axis the same slots mean the two solo direction
            // greens instead: 0/5 serves leg 0 alone (all movements
            // protected), 2/7 serves leg 1 alone.
            if (state.currentPhase < 0 || state.currentPhase > 9) {
                state.currentPhase = 2; state.lightTimer = 0.0f;
            }
            state.lightTimer += dt;
            const int phase = state.currentPhase;
            const float dur = state.phaseDurations[phase];

            const int phaseAxis = (phase < 5) ? 0 : 1;
            const bool splitAxis = state.axisSplit[phaseAxis];
            const bool legAGreen = (phase == 0 || phase == 5);
            const bool legBGreen = (phase == 2 || phase == 7);

            // Actuated greens: the mirrored straight green of a conventional
            // axis, or either solo leg green of a split axis. Rest on green
            // while nothing conflicts, gap out early once own demand clears,
            // never end before MIN_GREEN. Demand scans hit the spatial hash,
            // so poll at 2 Hz. (A conventional axis's 0/5 arrow stays
            // fixed-time and falls to the catch-all below.)
            if (legBGreen || (splitAxis && legAGreen)) {
                const int ownAxis = phaseAxis;
                const int crossAxis = 1 - ownAxis;
                if (state.lightTimer >= MIN_GREEN_SECONDS) {
                    state.demandPollTimer += dt;
                    if (state.demandPollTimer >= DEMAND_POLL_SECONDS) {
                        state.demandPollTimer = 0.0f;
                        bool conflicting, ownDemand;
                        if (splitAxis) {
                            // The opposing leg of the own axis is red during
                            // a solo green, so it is conflicting demand.
                            const int ownLeg = legAGreen ? 0 : 1;
                            conflicting = clusterAxisHasDemand(node, state, crossAxis)
                                       || clusterLegHasDemand(node, state, ownAxis, 1 - ownLeg);
                            ownDemand = clusterLegHasDemand(node, state, ownAxis, ownLeg);
                        } else {
                            // A car stuck yielding a permissive left on the
                            // green axis is conflicting demand too: only
                            // cycling through the cross phases reaches its
                            // protected arrow.
                            conflicting = clusterAxisHasDemand(node, state, crossAxis)
                                       || clusterLeftTurnDemand(node, state, ownAxis);
                            ownDemand = clusterAxisHasDemand(node, state, ownAxis);
                        }
                        if (conflicting) {
                            if (state.lightTimer >= dur || !ownDemand) {
                                state.currentPhase = phase + 1; // this green's yellow
                                state.lightTimer = 0.0f;
                            }
                        } else {
                            // Rest on green. Hold the timer at the phase
                            // length so newly arrived cross traffic gets its
                            // yellow at the next poll instead of waiting out
                            // a fresh green.
                            state.lightTimer = std::min(state.lightTimer, dur);
                        }
                    }
                }
            }
            // All-red decision points (4, 9): serve the axis that is due only
            // if anyone is actually there (the leg-0/protected-left phase
            // first when it is wanted); otherwise skip its phases and return
            // to the axis just served, where the green then rests until
            // demand appears.
            else if (phase == 4 || phase == 9) {
                if (state.lightTimer >= dur) {
                    const int dueAxis = (phase == 4) ? 1 : 0;
                    const int servedAxis = 1 - dueAxis;

                    // Entry phase for an axis: on a split axis, leg 0's solo
                    // green when leg 0 is waiting, else the leg-1 phase (the
                    // ring then reaches leg 0 next cycle); conventionally,
                    // the protected arrow when a turner waits, else straight.
                    auto entryPhaseFor = [&](int axis) -> int {
                        const int legA = (axis == 0) ? 0 : 5;
                        const int legB = (axis == 0) ? 2 : 7;
                        if (state.axisSplit[axis]) {
                            return clusterLegHasDemand(node, state, axis, 0) ? legA : legB;
                        }
                        return clusterLeftTurnDemand(node, state, axis) ? legA : legB;
                    };

                    state.currentPhase = clusterAxisHasDemand(node, state, dueAxis)
                                       ? entryPhaseFor(dueAxis)
                                       : entryPhaseFor(servedAxis);
                    state.lightTimer = 0.0f;
                    state.demandPollTimer = 0.0f;
                }
            }
            // Conventional protected-left greens and every yellow are
            // fixed-time.
            else if (state.lightTimer >= dur) {
                state.currentPhase = phase + 1;
                state.lightTimer = 0.0f;
                state.demandPollTimer = 0.0f;
            }
        }
    }
}
// The junction-box crossing is rendered at the car's physical speed, so the
// pace a turn "plays" at is whatever speed target the car carries through the
// box. Derive that target from the road being turned onto: through movements
// adopt the next road's limit outright, turns take a fraction of it -- which
// makes a right onto a fast arterial sweep visibly quicker than one into a
// residential street. Outside any box the target is simply the current road's
// limit (previously that reset only happened at edge transitions).
void PhysicsProcessor::applyJunctionTargetSpeed(VehicleState* vhcl)
{
    if (network == nullptr) return;

    Road* currentEdge = vhcl->getCurrentEdge();
    const size_t i = vhcl->currentRouteIndex;
    if (!currentEdge || vhcl->currentRoute.empty() || i + 1 >= vhcl->currentRoute.size()) return;

    auto turnTarget = [](const std::string& turn, double limit) -> float {
        const float lim = static_cast<float>(limit);
        // The rendered turn path is the straight chord across the box, which
        // is shorter than the arc distance the physics covers, so the sweep
        // plays back visibly slower than this target -- keep the fractions
        // and floors generous or turns crawl on screen.
        if (turn == "left")  return std::clamp(lim * 0.70f, 5.5f, lim);
        if (turn == "right") return std::clamp(lim * 0.60f, 5.0f, lim);
        return lim;
    };

    // Crossing the box at the far end of this edge: target the next road.
    Node* destNode = network->getNode(currentEdge->getDest());
    if (destNode && i + 2 < vhcl->currentRoute.size() &&
        vhcl->getPos() > stopLineArcPos(network, currentEdge, destNode))
    {
        for (Road& next : destNode->outgoingEdges) {
            if (next.getDest() == vhcl->currentRoute[i + 2]) {
                vhcl->setDesiredSpeed(turnTarget(getTurnDirectionAt(vhcl, i + 1), next.getSpeedLimit()));
                return;
            }
        }
    }

    // Just crossed a node: still inside the entry half of that box.
    Node* originNode = network->getNode(currentEdge->getOriginId());
    if (originNode && destNode && i > 0)
    {
        float sbStart = RoadIntersectionUtil::GetNodeSetbackMeters(
            network, *originNode, RoadIntersectionUtil::MedianGapMeters);
        float sbEnd = RoadIntersectionUtil::GetNodeSetbackMeters(
            network, *destNode, RoadIntersectionUtil::MedianGapMeters);
        RoadIntersectionUtil::ClampSetbacksToLength(
            static_cast<float>(currentEdge->getLength()), sbStart, sbEnd);

        if (vhcl->getPos() < sbStart) {
            vhcl->setDesiredSpeed(turnTarget(getTurnDirectionAt(vhcl, i), currentEdge->getSpeedLimit()));
            return;
        }
    }

    // Normal driving: track the current road's limit.
    vhcl->setDesiredSpeed(static_cast<float>(currentEdge->getSpeedLimit()));
}

// True when the lane vhcl will land in on its exit edge out of destNode has
// space for the whole car beyond the junction box: the queue tail's rear must
// sit past the box edge by at least a car length plus minGap, so the arrival
// clears the box instead of stopping inside it (or on top of the tail). Cars
// with no further edge (destination at/next node) always pass.
bool PhysicsProcessor::exitLaneHasRoom(VehicleState* vhcl, Node* destNode)
{
    const size_t i = vhcl->currentRouteIndex;
    if (i + 2 >= vhcl->currentRoute.size()) return true;

    Road* exitEdge = nullptr;
    for (Road& edge : destNode->outgoingEdges) {
        if (edge.getDest() == vhcl->currentRoute[i + 2]) {
            exitEdge = &edge;
            break;
        }
    }
    if (!exitEdge) return true;

    // Lane the edge transition will seat the car in.
    std::string turnMade = getTurnDirectionAt(vhcl, i + 1);
    RoadIntersectionUtil::TurnDir dir =
          (turnMade == "right") ? RoadIntersectionUtil::TurnDir::Right
        : (turnMade == "left")  ? RoadIntersectionUtil::TurnDir::Left
                                : RoadIntersectionUtil::TurnDir::Through;
    int landingLane = RoadIntersectionUtil::GetArrivalLane(
        dir, vhcl->getLane(), exitEdge->getLanes(), vhcl->getCurrentEdge());

    // Arc position on the exit edge where the junction box ends.
    float sbStart = RoadIntersectionUtil::GetNodeSetbackMeters(
        network, *destNode, RoadIntersectionUtil::MedianGapMeters);
    float sbEnd = 0.0f;
    Node* exitDestNode = network->getNode(exitEdge->getDest());
    if (exitDestNode) {
        sbEnd = RoadIntersectionUtil::GetNodeSetbackMeters(
            network, *exitDestNode, RoadIntersectionUtil::MedianGapMeters);
    }
    RoadIntersectionUtil::ClampSetbacksToLength(
        static_cast<float>(exitEdge->getLength()), sbStart, sbEnd);

    // Rear of the nearest car in the landing lane (queue tail).
    float nearestRear = std::numeric_limits<float>::max();
    VehicleState* tailCar = nullptr;
    for (VehicleState* other : spatialHash->getVehiclesOnRoad(exitEdge)) {
        if (other == nullptr || other == vhcl || other->isMarkedForDeletion) continue;
        if (other->getLane() != landingLane) continue;
        const float rear = other->getPos() - other->getLength();
        if (rear < nearestRear) {
            nearestRear = rear;
            tailCar = other;
        }
    }

    if (nearestRear >= sbStart + vhcl->getLength() + vhcl->getMinGap()) return true;

    // A discharging queue is not a blocked box. The car ahead hops onto the
    // exit edge the moment it enters the junction, so a static room check
    // re-arms against it and plants a stop-line ghost on every follower --
    // the whole green wave then crosses one full stop at a time. If the tail
    // is rolling forward at pace, the space will exist by the time this car
    // is across; IDM keeps the following gap either way. A slow or stopped
    // tail (spillback, mid-box stall) still blocks entry.
    return tailCar != nullptr && tailCar->getSpeed() >= 2.0f;
}

bool PhysicsProcessor::canVehicleEnter(VehicleState* vhcl, Node* destNode)
{
    // destNode represents intersection at end of a road
    if (destNode == nullptr) return true;

    // Internal leg of one physical multi-node junction (divided carriageways
    // map as 2-4 controlled nodes meters apart): the perimeter node already
    // gated this car, so the interior imposes no stop line, queue, or phase
    // -- otherwise cars stop again in the middle of the junction box. The
    // renderer plants no fixture on these legs for the same reason.
    Road* approachEdge = vhcl->getCurrentEdge();
    if (approachEdge != nullptr && approachEdge->getDest() == destNode->getId() &&
        RoadIntersectionUtil::IsInternalJunctionLeg(network, *approachEdge)) {
        return true;
    }

    // Don't-block-the-box: whatever the signal or right-of-way says, a car
    // may only enter the junction when the lane it lands in has room for the
    // whole car past the box exit. Without this, congested turns keep being
    // granted and each new arrival crosses the box straight into the queue
    // tail spilling back to the intersection.
    if (RoadIntersectionUtil::IsIntersectionNode(*destNode) && !exitLaneHasRoom(vhcl, destNode)) {
        return false;
    }

    // Wrong-lane gate: at a controlled intersection, a car whose current
    // lane does not permit its upcoming movement is held at the stop line
    // (where the stationary-guidance exception in update() lets it slide
    // into the required lane) instead of turning across the lanes between.
    // Skipped when the car can no longer stop comfortably. Holding is free
    // while the control denies entry anyway (red light, stop-sign queue) --
    // that is when real drivers merge over. The moment the control would
    // grant entry and the car still hasn't merged, it reroutes around the
    // turn instead of camping in a live lane; the wrong-lane turn survives
    // only as a last resort when no alternative route reaches the
    // destination. The gate stays one-shot per edge (m_wrongLaneHoldServed):
    // once served it NEVER re-arms until the next edge, so a mid-box lane
    // clamp can't trap the car in a stop/creep/stop loop.
    if (destNode->type != Node::PASS_THROUGH &&
        RoadIntersectionUtil::IsIntersectionNode(*destNode) &&
        !vhcl->hasServedWrongLaneHold() &&
        vhcl->currentRouteIndex + 2 < vhcl->currentRoute.size())
    {
        Road* approach = vhcl->getCurrentEdge();
        if (approach != nullptr && approach->hasLaneTurnData())
        {
            // Box-aware, matching the inference pass: at a multi-node
            // junction the movement that must be gated is the box-wide one,
            // not the near-straight first hop. U-turns gate like lefts.
            bool uTurnAhead = false;
            std::string turn = getUpcomingBoxMovement(vhcl, uTurnAhead);
            if (uTurnAhead) turn = "left";
            uint8_t movement = (turn == "left")  ? TurnLane::Left
                             : (turn == "right") ? TurnLane::Right
                                                 : TurnLane::Through;
            if (!approach->laneAllows(vhcl->getLane(), movement)) {
                float distToLine = stopLineArcPos(network, approach, destNode) - vhcl->getPos();
                float comfortableBrake = vhcl->getSafeBrakePower() * 2.0f;
                float speed = vhcl->getSpeed();
                if (distToLine > 0.0f && speed * speed <= 2.0f * comfortableBrake * distToLine) {
                    // Hold while the light/queue would stop us regardless.
                    if (!controlGrantsEntry(vhcl, destNode)) return false;

                    // Control would let us go. Give the stationary slide a
                    // brief human hesitation to land a merge; a car that
                    // waited out a red has already had its window (the
                    // clock accumulates through the hold, creep included).
                    constexpr float WrongLaneHesitationSeconds = 1.0f;
                    if (vhcl->getStopDuration() < WrongLaneHesitationSeconds) return false;

                    // Reroutes are rationed per car: without the cap, a car
                    // whose alternatives keep landing it in wrong lanes
                    // orbits the same blocks forever, and the orbiting
                    // traffic gridlocks the ring (seen in headless telemetry
                    // as one car rerouting 20+ times at one node).
                    constexpr int MaxWrongLaneReroutes = 3;
                    if (vhcl->getRerouteCount() < MaxWrongLaneReroutes &&
                        tryRerouteAroundWrongLaneTurn(vhcl, approach, destNode)) {
                        // Route tail replaced; the now-allowed movement is
                        // re-evaluated against the control next tick.
                        return false;
                    }

                    // No alternative route to the destination: one
                    // wrong-lane turn beats a plugged approach.
                    vhcl->markWrongLaneHoldServed();
                    return true; // control already granted entry above
                }
                // Too fast to stop comfortably: skip the hold, control decides.
            } else if (vhcl->getSpeed() < 1.0f) {
                // Reached an allowed lane while held at the line (the
                // stationary guidance slide worked): retire the hold so a
                // later mid-box lane clamp can't re-trigger it.
                vhcl->markWrongLaneHoldServed();
            }
        }
    }

    return controlGrantsEntry(vhcl, destNode);
}

bool PhysicsProcessor::controlGrantsEntry(VehicleState* vhcl, Node* destNode)
{
    if (destNode->type == Node::PASS_THROUGH) return true;

    uint64_t nodeId = destNode->getId();
    IntersectionState& state = intersections[nodeId];

    // four way stop
    if (destNode->type == Node::FOUR_WAY_STOP) {
        // allow car to move thru intersection
        if (state.currentOccupant == vhcl) return true;

        // setup queue for cars approaching stop
        bool inQueue = false;
        std::queue<VehicleState*> tempQueue = state.waitQueue;
        while (!tempQueue.empty()) {
            if (tempQueue.front() == vhcl) {
                inQueue = true;
                break;
            }
            tempQueue.pop();
        }

        if (!inQueue) {
            // Only claim a spot after a full stop at the front of the lane.
            // Joining while rolling anywhere within the approach let a
            // follower enter the FIFO before the car ahead of it; granting
            // that follower occupancy deadlocks the whole intersection (it
            // can never reach the box, so it never clears).
            if (vhcl->getSpeed() < 0.5f && isAtStopLine(vhcl, destNode)) {
                state.waitQueue.push(vhcl);
            }
        }
        return false; // car cannot enter
    }

    if (destNode->type == Node::TRAFFIC_LIGHT) {

        Road* myRoad = vhcl->getCurrentEdge();

        // Box-level movement: at a cluster perimeter node the per-hop chord
        // reads "through" for a left that actually turns mid-box, so the
        // gate must classify across the whole box. U-turns come from
        // topology, not the chord classifier (which splits them randomly
        // between "left" and "right"). Leftish movements carry a left's
        // permissions everywhere below: protected on arrows and solo
        // greens, gap-checked on permissive greens, never granted
        // right-on-red.
        bool uTurn = false;
        std::string turn = getUpcomingBoxMovement(vhcl, uTurn);
        const bool leftish = uTurn || (turn == "left");

        // Find which geometric axis my road belongs to
        auto findAxis = [&state](const Road* road) -> int {
            if (std::find(state.axisEdges[0].begin(), state.axisEdges[0].end(), road) != state.axisEdges[0].end()) return 0;
            if (std::find(state.axisEdges[1].begin(), state.axisEdges[1].end(), road) != state.axisEdges[1].end()) return 1;
            return -1;
        };
        int myAxis = findAxis(myRoad);

        // Not in the axis map: this approach was added (or outgoingEdges
        // reallocated) after the light's axes were built. Rebuild from the
        // live graph and look again -- without this the car faces an eternal
        // red, since no phase ever matches axis -1. An approach that is still
        // unclassifiable behaves as a cross street rather than deadlocking.
        if (myAxis == -1) {
            initializeLightAxes(destNode, state);
            myAxis = findAxis(myRoad);
            if (myAxis == -1) myAxis = 1;
        }

        const int phase = state.currentPhase;
        const int legAGreen = (myAxis == 0) ? 0 : 5;
        const int legBGreen = (myAxis == 0) ? 2 : 7;

        // Dilemma-zone test shared by the yellow phases: a car that cannot
        // stop with firm-but-comfortable braking carries the permissions of
        // the green its yellow follows; otherwise the yellow is a red.
        // Without this, the instant a green flips every approaching car --
        // even one a few meters from the line at full speed -- gets a
        // zero-speed ghost at the stop line and slams into the -10 m/s^2
        // clamp.
        auto cannotStop = [&]() -> bool {
            float distToLine = myRoad ? stopLineArcPos(network, myRoad, destNode) - vhcl->getPos() : -1.0f;
            float comfortableBrake = vhcl->getSafeBrakePower() * 2.0f;
            float speed = vhcl->getSpeed();
            return distToLine > 0.0f && speed * speed > 2.0f * comfortableBrake * distToLine;
        };

        if (state.axisSplit[myAxis]) {
            // Split axis: my whole direction goes alone -- every movement,
            // lefts and U-turns included, is protected because the opposing
            // leg and the cross axis are both red.
            int myLeg = 1;
            if (std::find(state.axisLegEdges[myAxis][0].begin(),
                          state.axisLegEdges[myAxis][0].end(), myRoad)
                    != state.axisLegEdges[myAxis][0].end()) {
                myLeg = 0;
            }
            const int myGreen = (myLeg == 0) ? legAGreen : legBGreen;
            if (phase == myGreen) return true;
            if (phase == myGreen + 1 && cannotStop()) return true;
        } else {
            if (phase == legAGreen) { // protected-left arrow
                return leftish;
            }
            if (phase == legBGreen) { // mirrored straight/right green
                // Permissive left: the oncoming leg has green too, so the
                // gap must cover the entire crossing (hasSafeGap folds the
                // crossing time in).
                if (leftish) return hasSafeGap(vhcl, destNode, 5.0f);
                return true;
            }
            if ((phase == legAGreen + 1 || phase == legBGreen + 1) && cannotStop()) {
                if (phase == legAGreen + 1) return leftish; // arrow's yellow
                if (leftish) return hasSafeGap(vhcl, destNode, 5.0f);
                return true;
            }
        }

        // RED LIGHT FALLBACK: Check for Right-on-Red. Never for a U-turn --
        // its path sweeps the whole box, not the near corner a right merges
        // into.
        if (turn == "right" && !uTurn && vhcl->getSpeed() < 1.0f) {
            return hasSafeGap(vhcl, destNode, 4.5f);
        }

        return false; // Wait for green
    }
    // yield stops, uses major and minor road classification
    if (destNode->type == Node::YIELD_STOP) {
        
        uint64_t comingFromId = vhcl->getCurrentEdge()->getOriginId();

        // check origin road to see if on minor road (yielding)
        bool isOnMinorRoad = std::find(destNode->minorRoadOriginIds.begin(), destNode->minorRoadOriginIds.end(), comingFromId) != destNode->minorRoadOriginIds.end();

        if (!isOnMinorRoad) {
            // Major road: through and right keep the right of way, but a
            // left or U-turn crosses the opposing major flow, which has no
            // reason to slow down -- granting it unchecked is a broadside in
            // the box. Denial plants the stop-line ghost, so the car brakes,
            // waits at the line, and goes when a gap long enough to cover
            // the whole crossing opens (hasSafeGap folds crossing time in).
            bool uTurn = false;
            if (getUpcomingBoxMovement(vhcl, uTurn) == "left" || uTurn) {
                return hasSafeGap(vhcl, destNode, 4.5f);
            }
            return true;
        }

        // minor road, slow down
        if (vhcl->getSpeed() > 1.0f) return false; 

        // yield and get safe gap
        if (hasSafeGap(vhcl, destNode, 4.5f)) {
            state.currentOccupant = vhcl; 
            return true;
        } else {
            return false;
        }
    }
    return true;
}

// Replans the tail of a wrong-lane car's route: every exit edge out of
// destNode whose movement IS allowed from the car's current lane is tried as
// the new first hop, each candidate is routed to the original destination,
// and the cheapest complete route is spliced into currentRoute past destNode
// (the travelled prefix is kept so route history stays intact). U-turns back
// onto the approach are never considered. Runs only for a car stopped at an
// intersection that already granted it entry, so the search cost is rare and
// off the hot path. Each DStarLite instance carries its own state map, so no
// global pathfinding reset is needed.
bool PhysicsProcessor::tryRerouteAroundWrongLaneTurn(VehicleState* vhcl, Road* approach, Node* destNode)
{
    const size_t i = vhcl->currentRouteIndex;
    if (i + 2 >= vhcl->currentRoute.size()) return false;

    Node* cameFrom = network->getNode(vhcl->currentRoute[i]);
    Node* goal = network->getNode(vhcl->currentRoute.back());
    if (cameFrom == nullptr || goal == nullptr || goal == destNode) return false;

    double bestCost = std::numeric_limits<double>::infinity();
    std::vector<uint64_t> bestTail;

    for (Road& exit : destNode->outgoingEdges) {
        if (exit.getDest() == vhcl->currentRoute[i]) continue;     // no U-turns
        // Skip the escaping turn's next hop -- except when that hop is an
        // internal junction leg: crossing the box is not itself a movement,
        // and a tail that leaves it by a different exit (straight over
        // instead of a box left) is a legitimate alternative.
        const bool internalExit = RoadIntersectionUtil::IsInternalJunctionLeg(network, exit);
        if (exit.getDest() == vhcl->currentRoute[i + 2] && !internalExit) continue;
        Node* next = network->getNode(exit.getDest());
        if (next == nullptr) continue;

        // Cheap pre-filter for plain exits: their movement doesn't depend
        // on the tail, so a disallowed one is rejected before any routing
        // work. Internal-leg exits are classified after routing, box-wide.
        if (!internalExit) {
            // Tangent-based to match the lane maps the laneAllows check below
            // reads (assignInferredTurnLanes is now tangent-based too).
            const RoadIntersectionUtil::TurnDir dir =
                RoadIntersectionUtil::ClassifyTurnAtNodeTangent(
                    network, vhcl->currentRoute[i], destNode->getId(), exit.getDest());
            const uint8_t movement =
                  (dir == RoadIntersectionUtil::TurnDir::Left)  ? TurnLane::Left
                : (dir == RoadIntersectionUtil::TurnDir::Right) ? TurnLane::Right
                                                                : TurnLane::Through;
            if (!approach->laneAllows(vhcl->getLane(), movement)) continue;
        }

        DStarLite router(network, next, goal, Heuristics3D::Euclidean);
        router.ComputeShortestPath();
        std::vector<uint64_t> tail = router.ExtractRoute(*network, next, goal);
        if (tail.empty() || tail.back() != goal->getId()) continue; // unreachable

        // Reject detours that come back through this intersection: the
        // forbidden turn is usually on the shortest path, so an unchecked
        // D* tail loops the block straight back here and the car reroutes
        // again on arrival -- an endless orbit. Better to fail over to the
        // one-shot wrong-lane turn than to circle.
        if (std::find(tail.begin(), tail.end(), destNode->getId()) != tail.end()) continue;

        // Same box-aware classifier and movement mapping as the wrong-lane
        // gate, so the spliced route is guaranteed to pass it next tick
        // (for an internal-leg exit the movement depends on where the tail
        // leaves the box, so it can only be judged against the full tail).
        std::vector<uint64_t> cand;
        cand.reserve(tail.size() + 2);
        cand.push_back(vhcl->currentRoute[i]);
        cand.push_back(destNode->getId());
        cand.insert(cand.end(), tail.begin(), tail.end());
        bool candUTurn = false;
        std::string candTurn = boxMovementOnRoute(cand, 0, candUTurn);
        if (candUTurn) candTurn = "left";
        const uint8_t candMovement =
              (candTurn == "left")  ? TurnLane::Left
            : (candTurn == "right") ? TurnLane::Right
                                    : TurnLane::Through;
        if (!approach->laneAllows(vhcl->getLane(), candMovement)) continue;

        // Candidate cost: the exit edge plus every leg of the tail, using
        // the same dynamic edge costs the router optimized over.
        double cost = exit.getDynamicCost();
        bool walkable = true;
        for (size_t k = 0; k + 1 < tail.size(); ++k) {
            Node* legOrigin = network->getNode(tail[k]);
            Road* leg = nullptr;
            if (legOrigin != nullptr) {
                for (Road& e : legOrigin->outgoingEdges) {
                    if (e.getDest() == tail[k + 1]) { leg = &e; break; }
                }
            }
            if (leg == nullptr) { walkable = false; break; }
            cost += leg->getDynamicCost();
        }
        if (!walkable) continue;

        if (cost < bestCost) {
            bestCost = cost;
            bestTail = std::move(tail);
        }
    }

    if (bestTail.empty()) return false;

    // Keep route[0..i+1] (through destNode) and graft the new tail on.
    vhcl->currentRoute.resize(i + 2);
    vhcl->currentRoute.insert(vhcl->currentRoute.end(), bestTail.begin(), bestTail.end());
    vhcl->incrementRerouteCount();
    return true;
}

// A car counts as "at the stop line" when it is on an edge into destNode,
// within a short reach of the line (cars rest ~minGap behind it, up to ~4m
// for trucks), and nothing in its lane sits between it and the line. This is
// the only state from which a granted car can actually enter the junction,
// so it gates both queue admission and the occupancy grant itself.
bool PhysicsProcessor::isAtStopLine(VehicleState* vhcl, Node* destNode)
{
    Road* edge = vhcl->getCurrentEdge();
    if (edge == nullptr || destNode == nullptr || edge->getDest() != destNode->getId()) return false;

    float distToLine = stopLineArcPos(network, edge, destNode) - vhcl->getPos();
    if (distToLine > 6.0f) return false;

    // Front-of-lane check: any same-lane car ahead on this edge (queued,
    // creeping, or still crossing the box) means this car cannot move yet.
    for (VehicleState* other : spatialHash->getVehiclesOnRoad(edge)) {
        if (other == nullptr || other == vhcl || other->isMarkedForDeletion) continue;
        if (other->getLane() != vhcl->getLane()) continue;
        if (other->getPos() > vhcl->getPos()) return false;
    }
    return true;
}

// Turn direction at an arbitrary route node: the movement from the edge
// entering route[nodeIndex] to the edge leaving it. Shared classifier from
// intersection_geometry.h so lane inference, lane guidance, and rendering
// all agree on what counts as a turn.
std::string PhysicsProcessor::getTurnDirectionAt(VehicleState* vhcl, size_t nodeIndex)
{
    if (nodeIndex == 0 || nodeIndex + 1 >= vhcl->currentRoute.size()) return "through";

    Node* prev = network->getNode(vhcl->currentRoute[nodeIndex - 1]);
    Node* curr = network->getNode(vhcl->currentRoute[nodeIndex]);
    Node* next = network->getNode(vhcl->currentRoute[nodeIndex + 1]);

    if (!prev || !curr || !next) return "through";

    // Tangent-based (chord fallback) so lane guidance and the render turn agree
    // with the tangent lane maps -- a curved through no longer reads as a turn.
    switch (RoadIntersectionUtil::ClassifyTurnAtNodeTangent(
        network, vhcl->currentRoute[nodeIndex - 1],
        vhcl->currentRoute[nodeIndex], vhcl->currentRoute[nodeIndex + 1]))
    {
        case RoadIntersectionUtil::TurnDir::Left:  return "left";
        case RoadIntersectionUtil::TurnDir::Right: return "right";
        default:                                   return "through";
    }
}

std::string PhysicsProcessor::getUpcomingTurnDirection(VehicleState* vhcl)
{
    return getTurnDirectionAt(vhcl, vhcl->currentRouteIndex + 1);
}

// The movement at the end of the current edge doubles back to the node the
// car came from (route ... A -> B -> A ...).
bool PhysicsProcessor::isUpcomingUTurn(VehicleState* vhcl) const
{
    const size_t i = vhcl->currentRouteIndex;
    return i + 2 < vhcl->currentRoute.size()
        && vhcl->currentRoute[i] == vhcl->currentRoute[i + 2];
}

// Movement across the whole junction box at the end of the current edge.
// A left across a multi-node cluster is a chain of near-straight hops (the
// approach into the perimeter node, internal legs around the box, the exit),
// so per-hop classification calls it "through" at the one node that gates it
// and the turn happens mid-box where nothing checks it. Resolve the box exit
// first, then classify entry direction vs exit direction.
std::string PhysicsProcessor::getUpcomingBoxMovement(VehicleState* vhcl, bool& outUTurn)
{
    return boxMovementOnRoute(vhcl->currentRoute, vhcl->currentRouteIndex, outUTurn);
}

std::string PhysicsProcessor::boxMovementOnRoute(const std::vector<uint64_t>& route, size_t i, bool& outUTurn)
{
    outUTurn = (i + 2 < route.size() && route[i] == route[i + 2]);

    std::string chordTurn = "through";
    if (network != nullptr && i + 2 < route.size()) {
        // Tangent-based so a curved single-node left is not misread as
        // "through" (and then granted unchecked); consistent with the
        // tangent lane maps assignInferredTurnLanes now produces.
        switch (RoadIntersectionUtil::ClassifyTurnAtNodeTangent(
                    network, route[i], route[i + 1], route[i + 2])) {
            case RoadIntersectionUtil::TurnDir::Left:  chordTurn = "left";  break;
            case RoadIntersectionUtil::TurnDir::Right: chordTurn = "right"; break;
            default: break;
        }
    }

    if (network == nullptr || i + 2 >= route.size()) return chordTurn;

    Node* entryFrom = network->getNode(route[i]);
    Node* entryTo   = network->getNode(route[i + 1]);
    if (entryFrom == nullptr || entryTo == nullptr) return chordTurn;

    // Walk internal legs to the box exit edge. No internal legs on the path
    // means a plain intersection: the chord classification stands.
    size_t k = i + 1;
    bool crossedInternal = false;
    Road* exitEdge = nullptr;
    Node* exitFrom = nullptr;
    while (k + 1 < route.size()) {
        Node* from = network->getNode(route[k]);
        if (from == nullptr) break;
        Road* leg = nullptr;
        for (Road& e : from->outgoingEdges) {
            if (e.getDest() == route[k + 1]) { leg = &e; break; }
        }
        if (leg == nullptr) break;
        if (RoadIntersectionUtil::IsInternalJunctionLeg(network, *leg)) {
            crossedInternal = true;
            ++k;
            continue;
        }
        exitEdge = leg;
        exitFrom = from;
        break;
    }
    if (!crossedInternal || exitEdge == nullptr || exitFrom == nullptr) return chordTurn;

    Node* exitTo = network->getNode(exitEdge->getDest());
    if (exitTo == nullptr) return chordTurn;

    // Entry/exit headings from edge TANGENTS, not node-to-node chords. A left
    // across a divided road enters and leaves along curved carriageways whose
    // chords can read near-parallel; the chord classifier then calls the box
    // movement "through", and a "through" is granted on a permissive green with
    // NO gap check -- a left turned straight across opposing through traffic.
    // The tangent sees the real turn. Chord fallback per edge (no shape data).
    double ex, ey, xx, xy;
    Road* apprEdge = nullptr;
    for (Road& e : entryFrom->outgoingEdges)
        if (e.getDest() == entryTo->getId()) { apprEdge = &e; break; }
    if (apprEdge == nullptr ||
        !RoadIntersectionUtil::GetEdgeEndDirection(network, *apprEdge, /*AtEnd=*/true, ex, ey))
    {
        ex = entryTo->getX() - entryFrom->getX();
        ey = entryTo->getY() - entryFrom->getY();
    }
    if (!RoadIntersectionUtil::GetEdgeEndDirection(network, *exitEdge, /*AtEnd=*/false, xx, xy))
    {
        xx = exitTo->getX() - exitFrom->getX();
        xy = exitTo->getY() - exitFrom->getY();
    }

    // Exit heading back against the entry direction = box U-turn (e.g. onto
    // the opposing carriageway of the road arrived on). The cross-product
    // classifier is numerical noise near anti-parallel, so call it by the
    // dot product; it carries a left's permissions everywhere anyway.
    const double elen = std::sqrt(ex * ex + ey * ey);
    const double xlen = std::sqrt(xx * xx + xy * xy);
    if (elen > 1e-9 && xlen > 1e-9 &&
        (ex * xx + ey * xy) / (elen * xlen) < -0.5) {
        outUTurn = true;
        return "left";
    }

    switch (RoadIntersectionUtil::ClassifyTurn(ex, ey, xx, xy)) {
        case RoadIntersectionUtil::TurnDir::Left:  return "left";
        case RoadIntersectionUtil::TurnDir::Right: return "right";
        default:                                   return "through";
    }
}

// Dest node id of the first non-internal edge from vhcl's next hop onward.
uint64_t PhysicsProcessor::boxExitDestId(VehicleState* vhcl)
{
    if (network == nullptr) return 0;
    size_t k = vhcl->currentRouteIndex + 1;
    while (k + 1 < vhcl->currentRoute.size()) {
        Node* from = network->getNode(vhcl->currentRoute[k]);
        if (from == nullptr) return 0;
        Road* leg = nullptr;
        for (Road& e : from->outgoingEdges) {
            if (e.getDest() == vhcl->currentRoute[k + 1]) { leg = &e; break; }
        }
        if (leg == nullptr) return 0;
        if (!RoadIntersectionUtil::IsInternalJunctionLeg(network, *leg)) {
            return leg->getDest();
        }
        ++k;
    }
    return 0;
}

// Seconds for vhcl to fully clear destNode's box: the path from its current
// position through the box -- internal legs of a clustered junction included
// -- until its tail is past the exit-side boundary, covered accelerating from
// its current speed toward the movement's junction pacing (same fractions as
// applyJunctionTargetSpeed). Slight overestimates are fine, the caller adds
// this to a gap requirement; underestimates put the car's tail in front of
// traffic that was told it had time.
float PhysicsProcessor::estimateCrossingSeconds(VehicleState* vhcl, Node* destNode)
{
    Road* approach = vhcl->getCurrentEdge();
    if (approach == nullptr || destNode == nullptr) return 0.0f;

    // Remaining approach; getPos() is the front bumper, the edge ends at the
    // node center.
    float dist = std::max(0.0f, static_cast<float>(approach->getLength()) - vhcl->getPos());

    // Walk the route across the box interior: internal legs of a multi-node
    // junction are crossing distance too (a left across a divided arterial
    // covers the whole median), and the first non-internal edge is the exit.
    size_t k = vhcl->currentRouteIndex + 1;
    Node* exitFrom = destNode;
    Road* exitEdge = nullptr;
    while (k + 1 < vhcl->currentRoute.size()) {
        Node* from = network->getNode(vhcl->currentRoute[k]);
        if (from == nullptr) break;
        Road* leg = nullptr;
        for (Road& e : from->outgoingEdges) {
            if (e.getDest() == vhcl->currentRoute[k + 1]) { leg = &e; break; }
        }
        if (leg == nullptr) break;
        if (RoadIntersectionUtil::IsInternalJunctionLeg(network, *leg)) {
            dist += static_cast<float>(leg->getLength());
            exitFrom = network->getNode(leg->getDest());
            ++k;
            continue;
        }
        exitEdge = leg;
        break;
    }

    bool uTurn = false;
    const std::string turn = getUpcomingBoxMovement(vhcl, uTurn);

    // Box portion of the exit edge, then the car's own length so the TAIL is
    // clear, not just the bumper.
    if (exitEdge != nullptr && exitFrom != nullptr) {
        float sb = RoadIntersectionUtil::GetNodeSetbackMeters(
            network, *exitFrom, RoadIntersectionUtil::MedianGapMeters);
        // A right turn hugs the near corner instead of crossing the far half
        // of the box.
        if (turn == "right" && !uTurn) sb *= 0.5f;
        dist += std::min(sb, static_cast<float>(exitEdge->getLength()));
    }
    dist += vhcl->getLength();

    // A single-node U-turn sweeps ~half a circle where the spans above
    // measure straight lines; cluster U-turns already walked their real
    // interior legs.
    if (uTurn && exitFrom == destNode) dist *= 1.4f;

    // Target speed through the box, matching applyJunctionTargetSpeed's turn
    // pacing so the estimate reflects how the crossing actually plays out.
    float target;
    const float exitLimit = static_cast<float>(
        exitEdge != nullptr ? exitEdge->getSpeedLimit() : approach->getSpeedLimit());
    if (uTurn)                target = 5.0f;
    else if (turn == "left")  target = std::clamp(exitLimit * 0.70f, 5.5f, exitLimit);
    else if (turn == "right") target = std::clamp(exitLimit * 0.60f, 5.0f, exitLimit);
    else                      target = exitLimit;
    target = std::max(target, 2.0f);

    // Constant-acceleration kinematics from the current speed; IDM tapers
    // near the target, so the clearance margin the caller adds absorbs the
    // difference.
    const float v0 = std::max(0.0f, vhcl->getSpeed());
    const float a = std::max(0.8f, vhcl->getMaxAccel());
    if (target <= v0) return dist / std::max(v0, 0.1f);

    const float accelDist = (target * target - v0 * v0) / (2.0f * a);
    if (dist <= accelDist) {
        return (std::sqrt(v0 * v0 + 2.0f * a * dist) - v0) / a;
    }
    return (target - v0) / a + (dist - accelDist) / target;
}

// updated with spatial hash
bool PhysicsProcessor::hasSafeGap(VehicleState* yieldingCar, Node* destNode, float criticalGapSeconds)
{
    // Box-level movement: a cluster left reads "through" hop-by-hop, and
    // the right-turn conflict exemption below must never apply to it.
    bool myUTurn = false;
    std::string myTurn = getUpcomingBoxMovement(yieldingCar, myUTurn);

    // The gap must cover the WHOLE crossing, not just the moment of entry: a
    // granted car needs several seconds to sweep the box (longer turning
    // left or U-turning, much longer across a clustered divided junction),
    // and once it is mid-box nothing brakes for it -- IDM only couples cars
    // on the same edge/route. So the required gap is the crossing time plus
    // a clearance margin, never less than the movement's base critical gap.
    constexpr float CrossingClearanceMarginSeconds = 1.0f;
    const float requiredGapSeconds = std::max(criticalGapSeconds,
        estimateCrossingSeconds(yieldingCar, destNode) + CrossingClearanceMarginSeconds);

    // At a multi-node junction the conflicting traffic mostly enters the
    // shared box through the OTHER perimeter nodes (oncoming through cars
    // approach the node across the median, not this one), so the gap check
    // must scan every member node's approaches -- including cars already on
    // the internal legs, i.e. mid-box.
    std::vector<Node*> gateNodes;
    gateNodes.push_back(destNode);
    auto clusterIt = junctionClusterOf.find(destNode->getId());
    if (clusterIt != junctionClusterOf.end()) {
        for (uint64_t memberId : clusterIt->second) {
            if (memberId == destNode->getId()) continue;
            Node* member = network->getNode(memberId);
            if (member != nullptr) gateNodes.push_back(member);
        }
    }

    for (Node* gateNode : gateNodes)
    // Only look at roads that physically connect to this intersection
    for (uint64_t predNodeId : gateNode->incomingEdgeNodeIds) {
        Node* predNode = network->getNode(predNodeId);
        if (!predNode) continue;

        for (Road& oncomingRoad : predNode->outgoingEdges) {

            // Skip the road the yielding car is currently on
            if (oncomingRoad.getEdgeId() == yieldingCar->getEdgeId() || oncomingRoad.getDest() != gateNode->getId()) {
                continue;
            }

            // Retrieve only the cars on this specific oncoming road from the spatial hash
            // (Assumes you have a getter in spatialHash or you make edgeBuckets accessible)
            std::vector<VehicleState*> oncomingCars = spatialHash->getVehiclesOnRoad(&oncomingRoad); 

            for (VehicleState* otherCar : oncomingCars) {
                if (otherCar == nullptr || otherCar->isMarkedForDeletion) continue; // check for deleted cars
                float distToIntersection = oncomingRoad.getLength() - otherCar->getPos();
                float speed = std::max(otherCar->getSpeed(), 0.1f);
                
                if (distToIntersection > 0.0f) {
                    float timeToArrival = distToIntersection / speed;
                    
                    bool pathsConflict = true;
                    if (myTurn == "right" && !myUTurn) {
                        // A right only conflicts with traffic that lands on
                        // the same road it merges onto. Compare BOX-EXIT
                        // dest ids, not route[i+2]: a through car crossing a
                        // cluster has an internal node there, which never
                        // matched and made every conflicting through car on
                        // the cluster's approaches invisible to
                        // right-on-red.
                        uint64_t yieldingNextId = boxExitDestId(yieldingCar);
                        uint64_t otherNextId = boxExitDestId(otherCar);

                        if (yieldingNextId != otherNextId) pathsConflict = false;
                    }

                    if (pathsConflict && timeToArrival < requiredGapSeconds) {
                        return false;
                    }
                } 
                else if (distToIntersection < 5.0f && distToIntersection > -15.0f) {
                    return false;
                }
            }
        }
    }
    return true; 
}

//sensor to activate protected left turn when needed
bool PhysicsProcessor::checkLeftTurnDemand(Node* node, const IntersectionState& state, int axis) {
    for (Road* incomingRoad : state.axisEdges[axis]) {
        const float stopLine = stopLineArcPos(network, incomingRoad, node);
        std::vector<VehicleState*> cars = spatialHash->getVehiclesOnRoad(incomingRoad);

        for (VehicleState* car : cars) {
            float distToStopLine = stopLine - car->getPos();

            // check if car is close to intersection and stopped
            if (distToStopLine > 0.0f && distToStopLine < 40.0f && car->getSpeed() < 1.0f) {

                // Short wait threshold: the all-red decision point comes only
                // ~2s of clearance after this axis's green ended, so a longer
                // one skipped the arrow for turners who arrived on the red.
                // Box-level movement so cluster lefts (per-hop "through")
                // and U-turners (chord angle unreliable) both call up the
                // arrow.
                if (car->getWaitTime() > LEFT_DEMAND_WAIT_SECONDS) {
                    bool uTurn = false;
                    if (getUpcomingBoxMovement(car, uTurn) == "left" || uTurn) {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

bool PhysicsProcessor::approachHasDemand(Road* road, Node* node) {
    const float stopLine = stopLineArcPos(network, road, node);
    std::vector<VehicleState*> cars = spatialHash->getVehiclesOnRoad(road);

    for (VehicleState* car : cars) {
        if (car->isMarkedForDeletion) continue;
        float distToStopLine = stopLine - car->getPos();
        // Anything bearing down on (or sitting at) the stop line counts;
        // the small negative tolerance keeps a car nosing past the line
        // registered until it actually clears into the box.
        if (distToStopLine > -2.0f && distToStopLine < DEMAND_DETECTOR_METERS) {
            return true;
        }
    }
    return false;
}

bool PhysicsProcessor::axisHasDemand(Node* node, const IntersectionState& state, int axis) {
    for (Road* incomingRoad : state.axisEdges[axis]) {
        if (approachHasDemand(incomingRoad, node)) return true;
    }
    return false;
}

void PhysicsProcessor::refreshIntersectionStates() {
    if (network == nullptr) return;

    // Clusters (and the shared baselines the axis grouping below reads) must
    // reflect the edited topology first.
    rebuildSignalClusters();

    // Rebuild axis pointers and timings for every tracked intersection from
    // the live graph. Queue/occupant/phase survive, so a car mid-crossing is
    // not forgotten; only the re-derivable topology data is rebuilt.
    for (auto it = intersections.begin(); it != intersections.end(); ) {
        Node* node = network->getNode(it->first);
        if (node == nullptr) {
            it = intersections.erase(it); // node deleted with its last road
            continue;
        }
        IntersectionState& state = it->second;
        state.axisEdges[0].clear(); // demoted lights must drop stale Road*
        state.axisEdges[1].clear();
        for (int axis = 0; axis < 2; axis++) {
            state.axisLegEdges[axis][0].clear();
            state.axisLegEdges[axis][1].clear();
        }
        state.isInitialized = false;
        if (node->type == Node::TRAFFIC_LIGHT) {
            initializeLightAxes(node, state);
        }
        ++it;
    }

    // Lights the edit just promoted get their state now rather than lazily,
    // matching the constructor's eager build (frontend renders the fixture
    // before the first car arrives).
    for (const auto& pair : network->getNodes()) {
        if (pair.second.type != Node::TRAFFIC_LIGHT) continue;
        if (intersections.find(pair.first) != intersections.end()) continue;
        Node* node = network->getNode(pair.first);
        if (node) initializeLightAxes(node, intersections[pair.first]);
    }

    // Split-phasing flags must agree across every light of a cluster; the
    // per-node decisions above only see local approaches.
    syncClusterSplitPhasing();
}