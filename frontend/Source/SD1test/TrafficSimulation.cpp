#include "TrafficSimulation.h"
#include <cmath>
#include <algorithm>
#include "physics_processor.h"
#include "traffic_manager.h"
#include "dstarlite.h"
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
    struct EdgePoint { double x, y, z; float yaw; };

    // Lane-offset point at 'dist' meters along the edge nA -> nB. Lane 0 is the
    // fast lane (closest to the median), matching the road HISM layout. Lane is
    // continuous so mid-lane-change vehicles render between lane centers.
    // Edges with OSM shape data follow their curved centerline -- the same
    // polyline the road visuals are built from -- so vehicles stay on the
    // pavement through bends; shapeless edges fall back to the straight chord.
    auto PointOnEdge = [&](Node* a, Node* b, const Road* edge, double edgeLen, double dist, double lane, EdgePoint& out) -> bool
    {
        if (edgeLen <= 0.0) return false;

        double px, py, tx, ty;
        if (!(edge && edge->samplePointAt(dist, px, py, tx, ty)))
        {
            // Straight fallback: lerp node to node.
            double dx = b->getX() - a->getX();
            double dy = b->getY() - a->getY();
            double len = std::sqrt(dx * dx + dy * dy);
            if (len < 0.0001) return false;
            double t = dist / edgeLen;
            px = a->getX() + t * dx;
            py = a->getY() + t * dy;
            tx = dx / len;
            ty = dy / len;
        }

        double t = std::clamp(dist / edgeLen, 0.0, 1.0);
        out.x = px;
        out.y = py;
        out.z = a->getZ() + t * (b->getZ() - a->getZ());
        out.yaw = static_cast<float>(std::atan2(ty, tx));

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
}