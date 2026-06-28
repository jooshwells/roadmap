#pragma once
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
};

struct TrafficLightRenderState {
    uint64_t nodeId;
    int phase;
};

class TrafficSimulation 
{
public:
    TrafficSimulation();
    ~TrafficSimulation();

    std::vector<VehicleRenderState> GetVehicleRenderStates();

    // Replaces your setup logic before the while loop
    void Initialize();

    // The single deterministic step that replaces the while loop
    void Step(float dt);

    // Accessor for the Unreal frontend to grab data for rendering
    // Replace 'auto' or 'VehicleData' with your actual vehicle data struct
    const std::vector<VehicleState*>& GetActiveVehicles() const;

    std::vector<TrafficLightRenderState> GetTrafficLightStates();
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