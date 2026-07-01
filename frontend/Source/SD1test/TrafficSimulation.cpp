#include "TrafficSimulation.h"
#include <cmath>
#include "physics_processor.h"
#include "traffic_manager.h"
#include "dstarlite.h"
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

void TrafficSimulation::Initialize() {
    currentTime = 0.0f;
    
    FString ContentDir = FPaths::ProjectContentDir();

    FString NodesPath = FPaths::Combine(ContentDir, TEXT("ThirdParty/MapData/waterford_nodes_orange_allroads_offline_xy.jsonl"));
    FString EdgesPath = FPaths::Combine(ContentDir, TEXT("ThirdParty/MapData/waterford_edges_orange_allroads_offline_xy.jsonl"));

    FPaths::CollapseRelativeDirectories(NodesPath);
    FPaths::CollapseRelativeDirectories(EdgesPath);
    // 1. Instantiate the network map on the heap
    orlandoMap = new Network(NetworkBuilder::buildNetworkFromJSONL(
        TCHAR_TO_UTF8(*NodesPath),
        TCHAR_TO_UTF8(*EdgesPath)
    ));

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

        float t = v->getPos() / v->getCurrentEdge()->getLength();

        // Calculate absolute map position
        double rawX = nA->getX() + t * (nB->getX() - nA->getX());
        double rawY = nA->getY() + t * (nB->getY() - nA->getY());
        double rawZ = nA->getZ() + t * (nB->getZ() - nA->getZ());

        VehicleRenderState state;

        // Apply the map centering offset
        state.x = static_cast<float>(rawX - originOffsetX);
        state.y = static_cast<float>(rawY - originOffsetY);
        state.z = static_cast<float>(rawZ);

        // Heading direction
        float dx = nB->getX() - nA->getX();
        float dy = nB->getY() - nA->getY();
        state.yaw = std::atan2(dy, dx); 

        float len = std::sqrt(dx * dx + dy * dy);
        if (len > 0.0001f)
        {
            // ---> FIX: Correct Unreal Engine Right Vector <---
            float rightVecX = -dy / len;
            float rightVecY = dx / len;

            int totalLanes = v->getCurrentEdge()->getLanes();
            const float LANE_WIDTH = 3.5f;
            const float MEDIAN_GAP_METERS = 1.0f;

            // Lane 0 is the fast lane, so it gets the smallest offset (closest to median)
            float laneOffsetMeters = MEDIAN_GAP_METERS + (LANE_WIDTH / 2.0f) + (v->getLane() * LANE_WIDTH);

            // Apply the offset
            state.x += rightVecX * laneOffsetMeters;
            state.y += rightVecY * laneOffsetMeters;

            state.id = v->getId();
        }
        // ---------------------------------------------------------

        renderStates.push_back(state);
    }

    return renderStates;
}
void TrafficSimulation::AddRuntimeRoad(uint64_t startNodeId, uint64_t endNodeId, double destX, double destY, double lengthMeters, int lanes) {
    if (!orlandoMap) return;

    // add new node from road editing
    if (!orlandoMap->getNode(endNodeId)) {
        orlandoMap->addNode(endNodeId, 0.0, 0.0, destX, destY);
    }

    // add directed edge, adjust speed limit later
    orlandoMap->addDirectedEdge(startNodeId, endNodeId, lengthMeters, 15.646, lanes);
}