#include "TrafficSimulation.h"
#include <cmath>
#include <algorithm>
#include "physics_processor.h"
#include "traffic_manager.h"
#include "dstarlite.h"
#include "heuristics3d.h"
#include "IntersectionGeometry.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

TrafficSimulation::TrafficSimulation() : currentTime(0.0f), orlandoMap(nullptr), logger(nullptr), spatialHash(nullptr), controller(nullptr), spawner(nullptr)
{
}

TrafficSimulation::~TrafficSimulation() 
{
    // Clean up dynamically allocated memory when the simulation is destroyed
    delete spawner;
    delete controller;
    delete spatialHash;
    delete logger;
    delete orlandoMap;
}

void TrafficSimulation::Initialize(const std::string& nodesPath, const std::string& edgesPath) {
    currentTime = 0.0f;

    // 1. Instantiate the network map on the heap
    orlandoMap = new Network(NetworkBuilder::buildNetworkFromJSONL(nodesPath, edgesPath));

    std::vector<uint64_t> westEdgeNodes;
    std::vector<uint64_t> eastEdgeNodes;

    if (orlandoMap)
    {
        double MinX = std::numeric_limits<double>::max();
        double MinY = std::numeric_limits<double>::max();
        double MaxX = std::numeric_limits<double>::lowest();
        double MaxY = std::numeric_limits<double>::lowest();

        // --- PASS 1: Find the global bounds ---
        for (const auto& NodePair : orlandoMap->getNodes())
        {
            const Node& N = NodePair.second;
            if (N.getX() < MinX) MinX = N.getX();
            if (N.getX() > MaxX) MaxX = N.getX();
            if (N.getY() < MinY) MinY = N.getY();
            if (N.getY() > MaxY) MaxY = N.getY();
        }

        originOffsetX = (MinX + MaxX) / 2.0;
        originOffsetY = (MinY + MaxY) / 2.0;

        // --- PASS 2: Categorize source/sink nodes for through-traffic ---
        // Define what constitutes an "edge" node. Here, we use the outer 15% of the X-axis.
        double mapWidth = MaxX - MinX;
        double edgeMargin = mapWidth * 0.15;

        for (const auto& NodePair : orlandoMap->getNodes())
        {
            const Node& N = NodePair.second;
            uint64_t nodeId = NodePair.first; // Grab the ID directly from the map key

            if (N.getX() <= (MinX + edgeMargin))
            {
                westEdgeNodes.push_back(nodeId);
            }
            else if (N.getX() >= (MaxX - edgeMargin))
            {
                eastEdgeNodes.push_back(nodeId);
            }
        }

        UE_LOG(LogTemp, Warning, TEXT("Total Nodes Loaded: %d"), orlandoMap->getNodes().size());
    }

    // 2. Instantiate the rest of the simulation components
    FString CsvPath = FPaths::Combine(FPaths::ProjectDir(), TEXT("simulation_output.csv"));
    FPaths::NormalizeFilename(CsvPath);
    // Create the telemetry logger that records simulation data to a CSV.
    logger = new TelemetryLogger(TCHAR_TO_UTF8(*CsvPath));

    spatialHash = new VehicleSpatialHash();

    // Pass pointers to the dependent components
    controller = new PhysicsProcessor(orlandoMap, spatialHash);
    spawner = new TrafficManager(orlandoMap, controller, 1500);

    // 3. Apply the through-traffic bounds
    // This routes traffic from West to East. 
    spawner->setThroughTrafficNodes(westEdgeNodes, eastEdgeNodes);
}

void TrafficSimulation::Step(float dt) 
{
    // Safety check to ensure things were initialized
    if (!spawner || !controller || !logger) return; 

    // A few queued route replans per step: spread out so a big road edit
    // never causes a frame hitch (each replan is one D* Lite search).
    ProcessPendingReplans(2);

    // The core of your previous while-loop lives here now.
    // Notice we use the arrow operator (->) because they are now pointers.
    spawner->update(dt);
    controller->update(dt);
    // Record the current state of all active vehicles for this frame.
    logger->logFrame(currentTime, controller->getActiveVehicles());

    currentTime += dt;
}

// Example getter implementation:
const std::vector<VehicleState*>& TrafficSimulation::GetActiveVehicles() const 
{
    if (!controller)
    {
        static std::vector<VehicleState*> EmptyList;
        return EmptyList;
    }

    return controller->getActiveVehicles();
}

std::vector<VehicleRenderState> TrafficSimulation::GetVehicleRenderStates()
{
    std::vector<VehicleRenderState> renderStates;

    if (!controller || !orlandoMap) return renderStates;

    const float MEDIAN_GAP_METERS = 1.0f;
    const float LANE_WIDTH = RoadIntersectionUtil::LaneWidthMeters;

    // A rendered point on (or between) road edges, in raw map coordinates.
    struct EdgePoint { double x, y, z; float yaw; float pitch; };

    // Lane-offset point at 'dist' meters along the edge nA -> nB. Lane 0 is the
    // fast lane (closest to the median), matching the road HISM layout. Lane is
    // continuous so mid-lane-change vehicles render between lane centers.
    // Edges with OSM shape data follow their curved centerline -- the same
    // polyline the road visuals are built from -- so vehicles stay on the
    // pavement through bends; shapeless edges fall back to the straight chord.
    auto PointOnEdge = [&](Node* a, Node* b, const Road* edge, double edgeLen, double dist, double lane, EdgePoint& out) -> bool
    {
        if (edgeLen <= 0.0) return false;

        // Half-span (m) of the slope probe that pitches the car to the road
        // surface: z is sampled this far behind and ahead so the car reads
        // the average grade under its wheelbase instead of snapping at the
        // knees of the vertical profile.
        const double PITCH_PROBE_M = 2.0;

        double px, py, tx, ty;
        double pz = 0.0;
        double pitch = 0.0;
        if (edge && edge->samplePointAt(dist, px, py, tx, ty, pz))
        {
            double qx, qy, qtx, qty;
            double z0 = pz, z1 = pz;
            const double d0 = std::max(0.0, dist - PITCH_PROBE_M);
            const double d1 = std::min(edgeLen, dist + PITCH_PROBE_M);
            if (d1 - d0 > 0.01 &&
                edge->samplePointAt(d0, qx, qy, qtx, qty, z0) &&
                edge->samplePointAt(d1, qx, qy, qtx, qty, z1))
            {
                pitch = std::atan2(z1 - z0, d1 - d0);
            }
        }
        else
        {
            // Straight fallback: lerp node to node.
            double dx = b->getX() - a->getX();
            double dy = b->getY() - a->getY();
            double len = std::sqrt(dx * dx + dy * dy);
            if (len < 0.0001) return false;
            double t = std::clamp(dist / edgeLen, 0.0, 1.0);
            px = a->getX() + t * dx;
            py = a->getY() + t * dy;
            pz = a->getZ() + t * (b->getZ() - a->getZ());
            tx = dx / len;
            ty = dy / len;
            pitch = std::atan2(b->getZ() - a->getZ(), len);
        }

        out.x = px;
        out.y = py;
        out.z = pz;
        out.yaw = static_cast<float>(std::atan2(ty, tx));
        out.pitch = static_cast<float>(pitch);

        double laneOffset = MEDIAN_GAP_METERS + (LANE_WIDTH / 2.0) + lane * LANE_WIDTH;
        out.x += (-ty) * laneOffset;
        out.y += ( tx) * laneOffset;
        return true;
    };

    auto FindEdge = [&](Node* from, uint64_t destId) -> const Road*
    {
        for (const Road& e : from->outgoingEdges)
            if (e.getDest() == destId) return &e;
        return nullptr;
    };

    // Shortest-arc angle interpolation, so a left turn doesn't spin the long way.
    auto LerpAngle = [](float a, float b, float s) -> float
    {
        float d = std::atan2(std::sin(b - a), std::cos(b - a));
        return a + d * s;
    };

    for (VehicleState* v : controller->getActiveVehicles())
    {
        if (!v) continue;
        // 1. Safety Guard: Skip if route is invalid, empty, or at the end
        if (v->currentRoute.empty() || v->currentRouteIndex >= v->currentRoute.size() - 1) continue;

        int startId = v->currentRoute[v->currentRouteIndex];
        int endId = v->currentRoute[v->currentRouteIndex + 1];

        Node* nA = orlandoMap->getNode(startId);
        Node* nB = orlandoMap->getNode(endId);

        // Explicit null-check for the currentEdge to prevent segmentation faults!
        if (!nA || !nB || !v->getCurrentEdge() || v->getCurrentEdge()->getLength() == 0) continue;

        const double L = v->getCurrentEdge()->getLength();
        const float pos = v->getPos();

        // Continuous lane position: mid-lane-change this eases between the old
        // and new lane centers, so the transition sweeps across instead of
        // teleporting sideways. Clamp per edge in case a transition started on
        // a wider road than the one being rendered against.
        auto ClampLaneToEdge = [](float laneValue, const Road* edge) -> float
        {
            float maxLane = static_cast<float>(std::max(0, edge->getLanes() - 1));
            return std::clamp(laneValue, 0.0f, maxLane);
        };
        const float lane = ClampLaneToEdge(v->getRenderLane(), v->getCurrentEdge());

        // Roads are drawn stopping short of intersection centers (see
        // RoadNetworkVisualizer). Mirror the same setbacks here: inside the
        // junction box, blend between the exit point of one edge and the entry
        // point of the next so the car sweeps through the turn instead of
        // driving to the node center and warping backwards onto the new road.
        float sbStart = RoadIntersectionUtil::GetNodeSetbackMeters(orlandoMap, *nA, MEDIAN_GAP_METERS);
        float sbEnd   = RoadIntersectionUtil::GetNodeSetbackMeters(orlandoMap, *nB, MEDIAN_GAP_METERS);
        RoadIntersectionUtil::ClampSetbacksToLength(static_cast<float>(L), sbStart, sbEnd);

        EdgePoint p{};
        bool resolved = false;

        if (pos > L - sbEnd && v->currentRouteIndex + 2 < v->currentRoute.size())
        {
            // Approaching / crossing the intersection at the end of this edge.
            Node* nC = orlandoMap->getNode(v->currentRoute[v->currentRouteIndex + 2]);
            const Road* next = nC ? FindEdge(nB, nC->getId()) : nullptr;
            if (next && next->getLength() > 0.0)
            {
                float sbNextStart = RoadIntersectionUtil::GetNodeSetbackMeters(orlandoMap, *nB, MEDIAN_GAP_METERS);
                float sbNextEnd   = RoadIntersectionUtil::GetNodeSetbackMeters(orlandoMap, *nC, MEDIAN_GAP_METERS);
                RoadIntersectionUtil::ClampSetbacksToLength(static_cast<float>(next->getLength()), sbNextStart, sbNextEnd);

                float nextLane = ClampLaneToEdge(lane, next);
                EdgePoint exitPt, entryPt;
                float denom = sbEnd + sbNextStart;
                if (denom > 0.001f &&
                    PointOnEdge(nA, nB, v->getCurrentEdge(), L, L - sbEnd, lane, exitPt) &&
                    PointOnEdge(nB, nC, next, next->getLength(), sbNextStart, nextLane, entryPt))
                {
                    float s = std::clamp((pos - (static_cast<float>(L) - sbEnd)) / denom, 0.0f, 1.0f);
                    p.x = exitPt.x + s * (entryPt.x - exitPt.x);
                    p.y = exitPt.y + s * (entryPt.y - exitPt.y);
                    p.z = exitPt.z + s * (entryPt.z - exitPt.z);
                    p.yaw = LerpAngle(exitPt.yaw, entryPt.yaw, s);
                    p.pitch = exitPt.pitch + s * (entryPt.pitch - exitPt.pitch);
                    resolved = true;
                }
            }
        }
        else if (pos < sbStart && v->currentRouteIndex > 0)
        {
            // Still crossing the intersection at the start of this edge.
            Node* nP = orlandoMap->getNode(v->currentRoute[v->currentRouteIndex - 1]);
            const Road* prev = nP ? FindEdge(nP, nA->getId()) : nullptr;
            if (prev && prev->getLength() > 0.0)
            {
                float sbPrevStart = RoadIntersectionUtil::GetNodeSetbackMeters(orlandoMap, *nP, MEDIAN_GAP_METERS);
                float sbPrevEnd   = RoadIntersectionUtil::GetNodeSetbackMeters(orlandoMap, *nA, MEDIAN_GAP_METERS);
                RoadIntersectionUtil::ClampSetbacksToLength(static_cast<float>(prev->getLength()), sbPrevStart, sbPrevEnd);

                float prevLane = ClampLaneToEdge(lane, prev);
                EdgePoint exitPt, entryPt;
                float denom = sbPrevEnd + sbStart;
                if (denom > 0.001f &&
                    PointOnEdge(nP, nA, prev, prev->getLength(), prev->getLength() - sbPrevEnd, prevLane, exitPt) &&
                    PointOnEdge(nA, nB, v->getCurrentEdge(), L, sbStart, lane, entryPt))
                {
                    float s = std::clamp((sbPrevEnd + pos) / denom, 0.0f, 1.0f);
                    p.x = exitPt.x + s * (entryPt.x - exitPt.x);
                    p.y = exitPt.y + s * (entryPt.y - exitPt.y);
                    p.z = exitPt.z + s * (entryPt.z - exitPt.z);
                    p.yaw = LerpAngle(exitPt.yaw, entryPt.yaw, s);
                    p.pitch = exitPt.pitch + s * (entryPt.pitch - exitPt.pitch);
                    resolved = true;
                }
            }
        }

        // Normal case: on the visible span of the edge (or no adjacent edge to
        // blend with, e.g. first/last route segment) -- render along the edge.
        if (!resolved)
        {
            if (!PointOnEdge(nA, nB, v->getCurrentEdge(), L, pos, lane, p)) continue;

            // Angle the car toward the lane it is merging into, proportional
            // to its lateral speed. Lane 0's offset points 90deg left of the
            // heading, so positive lateral rate steers the yaw the same way.
            float lateralRate = v->getLaneChangeLateralRate();
            if (lateralRate != 0.0f)
            {
                float lateralSpeed = lateralRate * LANE_WIDTH;
                p.yaw += std::atan2(lateralSpeed, std::max(v->getSpeed(), 1.0f));
            }
        }

        VehicleRenderState state;
        state.x = static_cast<float>(p.x - originOffsetX);
        state.y = static_cast<float>(p.y - originOffsetY);
        state.z = static_cast<float>(p.z);
        state.yaw = p.yaw;
        state.pitch = p.pitch;
        state.id = v->getId();

        renderStates.push_back(state);
    }

    return renderStates;
}
void TrafficSimulation::AddRuntimeRoad(uint64_t startNodeId, uint64_t endNodeId, double destX, double destY, double lengthMeters, int lanes, float speedLimit) {
    if (!orlandoMap) return;

    // add new node from road editing
    if (!orlandoMap->getNode(endNodeId)) {
        orlandoMap->addNode(endNodeId, 0.0, 0.0, destX, destY);
    }

    // add directed edge, using the dynamic speed limit
    orlandoMap->addDirectedEdge(startNodeId, endNodeId, lengthMeters, speedLimit, lanes);

    // Adding to a node's outgoingEdges vector can reallocate it, which would
    // leave vehicles on that node's other edges holding dangling Road*.
    RefreshVehicleEdgePointers();
}

void TrafficSimulation::SplitRuntimeEdge(uint64_t u, uint64_t v, uint64_t newNodeId, double x, double y)
{
    if (!orlandoMap) return;

    const bool fwd = orlandoMap->splitDirectedEdge(u, v, newNodeId, x, y);
    // The opposite direction of a two-way street is an independent directed
    // edge; split it through the same node so the junction works both ways.
    const bool rev = orlandoMap->splitDirectedEdge(v, u, newNodeId, x, y);
    if (!fwd && !rev) return;

    // Patch active routes: every consecutive (u, v) or (v, u) hop now passes
    // through the new node. Vehicles currently on the split edge keep their
    // route index and position; the physics advance loop carries anyone past
    // the first half onto the second at the next step.
    if (controller)
    {
        for (VehicleState* veh : controller->getActiveVehicles())
        {
            if (!veh) continue;
            std::vector<uint64_t>& route = veh->currentRoute;
            for (size_t i = 0; i + 1 < route.size(); i++)
            {
                const bool hitFwd = fwd && route[i] == u && route[i + 1] == v;
                const bool hitRev = rev && route[i] == v && route[i + 1] == u;
                if (!hitFwd && !hitRev) continue;

                route.insert(route.begin() + i + 1, newNodeId);
                if (veh->currentRouteIndex >= i + 1) veh->currentRouteIndex++;
                i++; // skip over the node we just inserted
            }
        }
    }

    RefreshVehicleEdgePointers();
}

void TrafficSimulation::DeleteRuntimeEdge(uint64_t u, uint64_t v, bool bBothDirections)
{
    if (!orlandoMap) return;

    // Which directions actually exist right now.
    auto EdgeExists = [&](uint64_t a, uint64_t b) -> bool
    {
        Node* from = orlandoMap->getNode(a);
        if (!from) return false;
        for (const Road& e : from->outgoingEdges)
        {
            if (e.getDest() == b) return true;
        }
        return false;
    };
    const bool fwd = EdgeExists(u, v);
    const bool rev = bBothDirections && EdgeExists(v, u);
    if (!fwd && !rev) return;

    auto IsDeletedHop = [&](uint64_t a, uint64_t b) -> bool
    {
        return (fwd && a == u && b == v) || (rev && a == v && b == u);
    };

    // Sort affected vehicles BEFORE touching the graph, while their
    // currentEdge pointers are still valid (despawnVehicle balances the edge
    // volume through that pointer).
    std::vector<VehicleState*> toDespawn;
    std::vector<VehicleState*> toReroute;
    if (controller)
    {
        for (VehicleState* veh : controller->getActiveVehicles())
        {
            if (!veh || veh->currentRoute.size() < 2) continue;
            if (veh->currentRouteIndex + 1 >= veh->currentRoute.size()) continue;

            const std::vector<uint64_t>& route = veh->currentRoute;
            for (size_t i = veh->currentRouteIndex; i + 1 < route.size(); i++)
            {
                if (!IsDeletedHop(route[i], route[i + 1])) continue;

                if (i == veh->currentRouteIndex)
                {
                    // Physically on the road being deleted: no way to keep it
                    // driving on a graph edge that no longer exists.
                    toDespawn.push_back(veh);
                }
                else
                {
                    toReroute.push_back(veh);
                }
                break;
            }
        }

        for (VehicleState* veh : toDespawn)
        {
            controller->despawnVehicle(veh);
        }
    }

    if (fwd) orlandoMap->removeDirectedEdge(u, v);
    if (rev) orlandoMap->removeDirectedEdge(v, u);

    // Endpoints with nothing left connected disappear along with the road
    // (mirrors the visual network / JSONL cleanup on the frontend side).
    orlandoMap->removeNodeIfIsolated(u);
    orlandoMap->removeNodeIfIsolated(v);

    // Reroute survivors around the gap now (the edge is already gone, so D*
    // Lite cannot pick it). Deletion is rare enough that a synchronous search
    // per affected vehicle beats letting anyone drive into a missing edge.
    for (VehicleState* veh : toReroute)
    {
        std::vector<uint64_t>& route = veh->currentRoute;
        const uint64_t hereId = route[veh->currentRouteIndex];
        const uint64_t nextId = route[veh->currentRouteIndex + 1];
        const uint64_t destId = route.back();

        std::vector<uint64_t> fresh;
        Node* next = orlandoMap->getNode(nextId);
        Node* dest = orlandoMap->getNode(destId);
        if (next && dest && nextId != destId)
        {
            DStarLite router(orlandoMap, next, dest, Heuristics3D::Euclidean);
            router.ComputeShortestPath();
            fresh = router.ExtractRoute(*orlandoMap, next, dest);
        }

        if (fresh.size() >= 2 && fresh.front() == nextId)
        {
            std::vector<uint64_t> spliced;
            spliced.reserve(fresh.size() + 1);
            spliced.push_back(hereId);
            spliced.insert(spliced.end(), fresh.begin(), fresh.end());
            route = std::move(spliced);
            veh->currentRouteIndex = 0;
        }
        else
        {
            // No path around the gap: cut the route just before the deleted
            // hop. The car drives to that node, stops, and the destination
            // check despawns it there.
            for (size_t i = veh->currentRouteIndex; i + 1 < route.size(); i++)
            {
                if (IsDeletedHop(route[i], route[i + 1]))
                {
                    route.resize(i + 1);
                    break;
                }
            }
        }
    }

    // removeDirectedEdge shifts the surviving Roads inside outgoingEdges, so
    // every cached currentEdge pointer must be re-resolved.
    RefreshVehicleEdgePointers();
}

void TrafficSimulation::UpdateRuntimeRoad(uint64_t u, uint64_t v, int lanes, float speedMps, bool bBothDirections)
{
    if (!orlandoMap) return;

    const int safeLanes = std::max(1, lanes);
    const float safeSpeed = std::max(0.5f, speedMps);

    auto Apply = [&](uint64_t a, uint64_t b)
    {
        Node* from = orlandoMap->getNode(a);
        if (!from) return;
        for (Road& e : from->outgoingEdges)
        {
            if (e.getDest() == b)
            {
                e.setLanes(safeLanes);
                e.setSpeedLimit(safeSpeed);
                break;
            }
        }
    };

    Apply(u, v);
    if (bBothDirections) Apply(v, u);

    // Vehicles already driving the edited edge adopt the new speed limit and
    // get pulled out of lanes that no longer exist.
    if (!controller) return;
    for (VehicleState* veh : controller->getActiveVehicles())
    {
        if (!veh || veh->currentRoute.empty()) continue;
        if (veh->currentRouteIndex + 1 >= veh->currentRoute.size()) continue;

        const uint64_t a = veh->currentRoute[veh->currentRouteIndex];
        const uint64_t b = veh->currentRoute[veh->currentRouteIndex + 1];
        const bool onFwd = (a == u && b == v);
        const bool onRev = bBothDirections && (a == v && b == u);
        if (!onFwd && !onRev) continue;

        if (veh->getLane() >= safeLanes) veh->setLane(safeLanes - 1);
        veh->setDesiredSpeed(safeSpeed);
    }
}

void TrafficSimulation::QueueRouteReplansNear(double x, double y, double radiusMeters, int maxVehicles)
{
    if (!controller || !orlandoMap) return;

    const double r2 = radiusMeters * radiusMeters;
    int queued = 0;

    for (VehicleState* veh : controller->getActiveVehicles())
    {
        if (queued >= maxVehicles) break;
        if (!veh || veh->currentRoute.size() < 2) continue;
        if (veh->currentRouteIndex + 1 >= veh->currentRoute.size()) continue;

        // Only vehicles whose remaining route passes near the edit could
        // plausibly benefit from a detour through it.
        for (size_t i = veh->currentRouteIndex; i < veh->currentRoute.size(); i++)
        {
            Node* n = orlandoMap->getNode(veh->currentRoute[i]);
            if (!n) continue;
            const double dx = n->getX() - x;
            const double dy = n->getY() - y;
            if (dx * dx + dy * dy <= r2)
            {
                pendingReplanIds.push_back(veh->getId());
                queued++;
                break;
            }
        }
    }
}

void TrafficSimulation::ProcessPendingReplans(int maxCount)
{
    if (pendingReplanIds.empty()) return;
    if (!controller || !orlandoMap)
    {
        pendingReplanIds.clear();
        return;
    }

    int done = 0;
    while (done < maxCount && !pendingReplanIds.empty())
    {
        const int vid = pendingReplanIds.front();
        pendingReplanIds.pop_front();
        done++;

        VehicleState* veh = nullptr;
        for (VehicleState* c : controller->getActiveVehicles())
        {
            if (c && c->getId() == vid) { veh = c; break; }
        }
        if (!veh || veh->currentRoute.size() < 2) continue;
        if (veh->currentRouteIndex + 1 >= veh->currentRoute.size()) continue;

        // Replan from the node the car is heading toward: it cannot leave its
        // current edge mid-span, so the hop it is on must be preserved.
        const uint64_t hereId = veh->currentRoute[veh->currentRouteIndex];
        const uint64_t nextId = veh->currentRoute[veh->currentRouteIndex + 1];
        const uint64_t destId = veh->currentRoute.back();
        if (nextId == destId) continue; // already on the final hop

        Node* next = orlandoMap->getNode(nextId);
        Node* dest = orlandoMap->getNode(destId);
        if (!next || !dest) continue;

        DStarLite router(orlandoMap, next, dest, Heuristics3D::Euclidean);
        router.ComputeShortestPath();
        std::vector<uint64_t> fresh = router.ExtractRoute(*orlandoMap, next, dest);
        if (fresh.size() < 2 || fresh.front() != nextId) continue;

        std::vector<uint64_t> spliced;
        spliced.reserve(fresh.size() + 1);
        spliced.push_back(hereId);
        spliced.insert(spliced.end(), fresh.begin(), fresh.end());

        veh->currentRoute = std::move(spliced);
        veh->currentRouteIndex = 0;
    }

    RefreshVehicleEdgePointers();
}

void TrafficSimulation::RefreshVehicleEdgePointers()
{
    if (!controller || !orlandoMap) return;

    for (VehicleState* veh : controller->getActiveVehicles())
    {
        if (!veh || veh->currentRoute.empty()) continue;
        if (veh->currentRouteIndex >= veh->currentRoute.size() - 1) continue;

        Node* n = orlandoMap->getNode(veh->currentRoute[veh->currentRouteIndex]);
        if (!n) continue;

        for (Road& e : n->outgoingEdges)
        {
            if (e.getDest() == veh->currentRoute[veh->currentRouteIndex + 1])
            {
                veh->setCurrentEdge(&e);
                break;
            }
        }
    }
}