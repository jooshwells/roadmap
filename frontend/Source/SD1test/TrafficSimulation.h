#pragma once
#include <string>
#include <vector>
#include "network_builder.h"
#include "network.h"
#include "traffic_manager.h"
#include "tm_logger.h"
#include "spatial_hash.h"
#include "vehicle_state.h"
#include "physics_processor.h"
#include "dstarlite.h"

// Forward declarations or placeholder includes for your decoupled classes
// #include "TrafficSpawner.h"
// #include "TrafficController.h"
// #include "TrafficLogger.h"
// #include "VehicleData.h"

// Note: Ensure this class has NO dependencies on Unreal Engine (#include "Engine.h" etc.)
// This keeps it completely decoupled and portable.

struct VehicleRenderState
{
    float x, y, z;
    float yaw; // Heading direction in radians
    int id; // vhcl id in sim backend
};

// Per-frame signal color of one traffic-light node, per axis. Approaches are
// identified by the origin node id of their incoming edge, which is how the
// visualizer maps its own network's fixtures onto the sim's axis grouping.
struct TrafficLightRenderState
{
    enum Color : uint8_t { RED = 0, YELLOW = 1, GREEN = 2 };

    uint64_t nodeId;
    uint8_t axisColor[2];                  // [0] = main axis, [1] = cross axis
    std::vector<uint64_t> axisOrigins[2];  // incoming-edge origin node ids per axis
};

class TrafficSimulation 
{
public:
    TrafficSimulation();
    ~TrafficSimulation();

    std::vector<VehicleRenderState> GetVehicleRenderStates();

    // Current signal colors for every traffic-light intersection, for the
    // frontend's traffic control visuals. Cheap: one entry per light node.
    std::vector<TrafficLightRenderState> GetTrafficLightRenderStates();

    // Replaces your setup logic before the while loop. The caller decides
    // which roadmap JSONL pair to simulate (menu selection or default map).
    void Initialize(const std::string& nodesPath, const std::string& edgesPath);

    // The single deterministic step that replaces the while loop
    void Step(float dt);

    // Accessor for the Unreal frontend to grab data for rendering
    // Replace 'auto' or 'VehicleData' with your actual vehicle data struct
    const std::vector<VehicleState*>& GetActiveVehicles() const;

    // add new road when road editing
    void AddRuntimeRoad(uint64_t startNodeId, uint64_t endNodeId, double destX, double destY, double lengthMeters, int lanes);

private:
    double originOffsetX;
    double originOffsetY;

    // backend components
    Network* orlandoMap;
    TelemetryLogger* logger;
    VehicleSpatialHash* spatialHash;
    PhysicsProcessor* controller;
    TrafficManager* spawner;

    float currentTime;
};