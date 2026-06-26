#include "TrafficSimulation.h"
#include <cmath>
#include "physics_processor.h"
#include "traffic_manager.h"
#include "dstarlite.h"
#include <filesystem>
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

void TrafficSimulation::Initialize() 
{
    currentTime = 0.0f;

    FString ProjectDir = FPaths::ProjectDir();

    // 2. Build the path to the python_pipeline folder
    // Since python_pipeline is next to frontend, we go up one level from the project root
    FString NodesPath = FPaths::Combine(ProjectDir, TEXT("../python_pipeline/sample_out/waterford_nodes_orange_allroads_offline_xy.jsonl"));
    FString EdgesPath = FPaths::Combine(ProjectDir, TEXT("../python_pipeline/sample_out/waterford_edges_orange_allroads_offline_xy.jsonl"));

    // 3. (Optional but recommended) Convert it to a clean, absolute path
    FPaths::CollapseRelativeDirectories(NodesPath);
    FPaths::CollapseRelativeDirectories(EdgesPath);
    // 1. Instantiate the network map on the heap
    orlandoMap = new Network(NetworkBuilder::buildNetworkFromJSONL(
        TCHAR_TO_UTF8(*NodesPath),
        TCHAR_TO_UTF8(*EdgesPath)
    ));

    if (orlandoMap)
    {
        double MinX = std::numeric_limits<double>::max();
        double MinY = std::numeric_limits<double>::max();
        double MaxX = std::numeric_limits<double>::lowest();
        double MaxY = std::numeric_limits<double>::lowest();

        // Assuming orlandoMap->getNodes() returns a map/unordered_map of ID to Node
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
    }

    // 2. Instantiate the rest of the simulation components
    logger = new TelemetryLogger("simulation_output.csv");
    spatialHash = new VehicleSpatialHash();
    
    // Pass pointers to the dependent components
    controller = new PhysicsProcessor(orlandoMap, spatialHash);
    spawner = new TrafficManager(orlandoMap, controller);
}

void TrafficSimulation::Step(float dt) 
{
    // Safety check to ensure things were initialized
    if (!spawner || !controller || !logger) return; 

    // The core of your previous while-loop lives here now.
    // Notice we use the arrow operator (->) because they are now pointers.
    spawner->update(dt);
    controller->update(dt);
    
    logger->logFrame(currentTime, controller->getActiveVehicles());

    currentTime += dt;
}

// Example getter implementation:
const std::vector<VehicleState*>& TrafficSimulation::GetActiveVehicles() const 
{
    return controller->getActiveVehicles();
}

std::vector<VehicleRenderState> TrafficSimulation::GetVehicleRenderStates() 
{
    std::vector<VehicleRenderState> renderStates;
    
    if (!controller || !orlandoMap) return renderStates;

    for (VehicleState* v : controller->getActiveVehicles()) 
    {
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

        // ---------------------------------------------------------
        // THE NEW LANE OFFSET LOGIC GOES HERE
        // ---------------------------------------------------------
        float len = std::sqrt(dx*dx + dy*dy);
        if (len > 0.0001f) 
        {
            // 1. Correct Right Vector for Unreal Engine (X-Forward, Y-Right)
            float rightVecX = -dy / len; 
            float rightVecY = dx / len;

            // 2. Centered Lane Offset Logic
            int totalLanes = v->getCurrentEdge()->getLanes();
            const float LANE_WIDTH = 3.5f;

            // Calculate the mathematical edges of the visual road box
            float roadRightEdge = (totalLanes * LANE_WIDTH) / 2.0f;

            // Option A: Lane 0 is the RIGHT-MOST lane 
            // Start at the right edge, move left (negative) for each lane index, minus half a lane to hit the center.
            float laneOffsetMeters = roadRightEdge - (v->getLane() * LANE_WIDTH) - (LANE_WIDTH / 2.0f);

            // Apply the offset
            state.x += rightVecX * laneOffsetMeters;
            state.y += rightVecY * laneOffsetMeters;
        }
        // ---------------------------------------------------------

        renderStates.push_back(state);
    }

    return renderStates;
}