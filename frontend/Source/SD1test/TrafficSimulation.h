#pragma once
#include <deque>
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

class TrafficSimulation 
{
public:
    TrafficSimulation();
    ~TrafficSimulation();

    std::vector<VehicleRenderState> GetVehicleRenderStates();

    // Replaces your setup logic before the while loop. The caller decides
    // which roadmap JSONL pair to simulate (menu selection or default map).
    void Initialize(const std::string& nodesPath, const std::string& edgesPath);

    // The single deterministic step that replaces the while loop
    void Step(float dt);

    // Accessor for the Unreal frontend to grab data for rendering
    // Replace 'auto' or 'VehicleData' with your actual vehicle data struct
    const std::vector<VehicleState*>& GetActiveVehicles() const;

    // add new road when road editing
    void AddRuntimeRoad(uint64_t startNodeId, uint64_t endNodeId, double destX, double destY, double lengthMeters, int lanes, float speedLimit);

    // Split the live edge(s) between u and v at a new node placed at map
    // coords (x, y). Both directions of a two-way street are split when
    // present. Active vehicle routes crossing the split hop are patched to
    // pass through the new node, and cached edge pointers are re-resolved.
    void SplitRuntimeEdge(uint64_t u, uint64_t v, uint64_t newNodeId, double x, double y);

    // Delete the live edge u->v (and v->u when bBothDirections). Vehicles
    // currently driving the deleted edge are despawned; vehicles whose
    // remaining route uses it are rerouted around it immediately (or their
    // route is truncated at the gap when no alternative exists, so they park
    // and despawn there). Endpoint nodes left with no edges are removed too.
    void DeleteRuntimeEdge(uint64_t u, uint64_t v, bool bBothDirections);

    // Update the live edge(s) between u and v with new lane count / speed
    // limit. Vehicles already on the edge adopt the new speed and get their
    // lane clamped if lanes were removed.
    void UpdateRuntimeRoad(uint64_t u, uint64_t v, int lanes, float speedMps, bool bBothDirections);

    // Queue route replans for vehicles whose remaining route passes within
    // radiusMeters of map point (x, y). Routes are only computed at spawn, so
    // without this, cars already driving never discover an edited road. The
    // queue is drained a few vehicles per Step to avoid a frame hitch.
    void QueueRouteReplansNear(double x, double y, double radiusMeters, int maxVehicles = 100);

private:
    // Re-resolve every active vehicle's currentEdge pointer from its route.
    // Must run after any graph mutation: currentEdge points into a node's
    // outgoingEdges vector, which can reallocate when an edge is added.
    void RefreshVehicleEdgePointers();

    // Drain up to maxCount queued replans (one D* Lite search each).
    void ProcessPendingReplans(int maxCount);

    std::deque<int> pendingReplanIds;

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