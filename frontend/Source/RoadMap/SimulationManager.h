// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/InstancedStaticMeshComponent.h"

THIRD_PARTY_INCLUDES_START
#include "network.h"
#include "network_builder.h"
#include "RoadNetworkVisualizer.h"
THIRD_PARTY_INCLUDES_END

#include "TrafficControlVisualizer.h"

#include "TrafficSimulation.h"
#include "Blueprint/UserWidget.h"

#include "SimulationManager.generated.h"

struct FVehicleTransformState
{
	FTransform Previous;
	FTransform Target;
};

USTRUCT(BlueprintType)
struct FVehicleIDMStats
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Vehicle Stats") int32 VehicleID = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle Stats") float CurrentSpeed = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle Stats") float DesiredSpeed = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle Stats") float MaxAcceleration = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle Stats") float AccelerationExponent = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle Stats") float MinGap = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle Stats") float SafeBrakePower = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle Stats") float SafeTimeHeadway = 0.0f;

	// Driver personality: archetype name ("Aggressive" / "Average" /
	// "Cautious"), cruise speed as a multiple of the road limit, and seconds
	// of lag before launching from a stop. Fixed for the vehicle's lifetime.
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle Stats") FString ProfileName;
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle Stats") float SpeedFactor = 1.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle Stats") float ReactionTime = 0.0f;

	// Live telemetry -- changes every sim step, so the stats panel re-polls
	// these while open (GetVehicleStatsByID) instead of showing a snapshot.
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle Stats") float CurrentAcceleration = 0.0f; // m/s^2, negative while braking
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle Stats") float WaitTime = 0.0f;            // seconds spent (nearly) stopped
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle Stats") int32 Lane = 0;                   // 0-based lane index
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle Stats") float Politeness = 0.0f;          // MOBIL p: 0 selfish .. 1 selfless
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle Stats") float RoadSpeedLimit = 0.0f;      // m/s on the current edge (0 = unknown)
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle Stats") int32 RouteIndex = 0;             // node reached along the route
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle Stats") int32 RouteLength = 0;            // total nodes in the route
};

// One incoming approach of an inspected intersection node: which road feeds
// it, where its stop line (and any sign/signal fixture) sits, and the
// per-lane turn permissions the physics steers by. Everything the "why is
// this fixture here / why do cars stop there" question needs.
USTRUCT(BlueprintType)
struct FIntersectionApproachInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Intersection") int64 FromNodeId = -1;
	// Edge id of the approach road, matching the road editor's numbering.
	UPROPERTY(BlueprintReadOnly, Category = "Intersection") int64 RoadId = -1;
	UPROPERTY(BlueprintReadOnly, Category = "Intersection") int32 Lanes = 1;
	UPROPERTY(BlueprintReadOnly, Category = "Intersection") float LengthMeters = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Intersection") float SpeedLimitMps = 0.0f;

	// OSM-syntax per-lane turn map ("left|through|through;right"); empty when
	// the edge has no data (every movement allowed from every lane).
	UPROPERTY(BlueprintReadOnly, Category = "Intersection") FString TurnLanes;
	UPROPERTY(BlueprintReadOnly, Category = "Intersection") bool bTurnLanesInferred = false;

	// How far before the node center the stop line (and fixture) sits, and
	// whether GetStopLineArcPos had to clamp it because the approach edge is
	// shorter than the junction setback -- the usual culprit when a sign
	// appears to float mid-road or cars halt in odd places.
	UPROPERTY(BlueprintReadOnly, Category = "Intersection") float StopLineFromNodeM = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Intersection") bool bStopLineClamped = false;

	// Interior edge of one physical multi-node junction: the sim skips the
	// control here and no fixture or stop line exists on this approach.
	UPROPERTY(BlueprintReadOnly, Category = "Intersection") bool bInternalLeg = false;

	// This approach yields (minor road of a YIELD_STOP node).
	UPROPERTY(BlueprintReadOnly, Category = "Intersection") bool bMinorApproach = false;

	// A sign/signal fixture is rendered for this approach.
	UPROPERTY(BlueprintReadOnly, Category = "Intersection") bool bHasFixture = false;
};

// Everything the intersection inspector panel shows about one node, mirroring
// FRoadEdgeInfo for roads. Topology only -- it comes from the visual network,
// which road edits keep current (the panel closes on edits anyway).
USTRUCT(BlueprintType)
struct FIntersectionNodeInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Intersection") int64 NodeId = -1;
	UPROPERTY(BlueprintReadOnly, Category = "Intersection") FString ControlType;   // Traffic light / Four-way stop / Yield / Uncontrolled
	UPROPERTY(BlueprintReadOnly, Category = "Intersection") FVector WorldLocation = FVector::ZeroVector;

	// 3+ distinct roadways meet here (RoadIntersectionUtil::IsIntersectionNode).
	// Controlled nodes that are NOT geometric intersections get a setback of 0,
	// so their stop line lands on the node itself -- flagged by the panel.
	UPROPERTY(BlueprintReadOnly, Category = "Intersection") bool bIsIntersection = false;
	UPROPERTY(BlueprintReadOnly, Category = "Intersection") int32 IncomingCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Intersection") int32 OutgoingCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Intersection") int32 MaxLanesAtNode = 1;
	UPROPERTY(BlueprintReadOnly, Category = "Intersection") float SetbackMeters = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Intersection") TArray<FIntersectionApproachInfo> Approaches;
};


UCLASS()
class ROADMAP_API ASimulationManager : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	ASimulationManager();
	// This allows you to select your Blueprint in the Unreal Editor
	UPROPERTY(EditAnywhere, Category = "Simulation Setup")
	TSubclassOf<class ARoadNetworkVisualizer> VisualizerBlueprint;

	// Optional Blueprint override for the traffic light / stop sign actor.
	// Left unset, the base C++ class is spawned with its built-in shapes.
	UPROPERTY(EditAnywhere, Category = "Simulation Setup")
	TSubclassOf<class ATrafficControlVisualizer> TrafficControlVisualizerClass;
	
	UFUNCTION(BlueprintCallable, Category = "Simulation")
	bool GetVehicleStatsFromInstance(int32 InstanceIndex, FVehicleIDMStats& OutStats);

	// Live re-poll for an already-identified vehicle (the stats panel calls
	// this every UI tick). Returns false once the vehicle has despawned or
	// the simulation stopped.
	UFUNCTION(BlueprintCallable, Category = "Simulation")
	bool GetVehicleStatsByID(int32 VehicleID, FVehicleIDMStats& OutStats);

	UFUNCTION(BlueprintCallable, Category = "Simulation")
	int32 GetInstanceIndexFromVehicleID(int32 VehicleID);

	// Fills the intersection inspector struct for a node: static topology,
	// stop lines, and fixture placement from the visual network, live phase /
	// queue state from the running sim when there is one. Returns false when
	// the node id is unknown. The inspector panel re-polls this while open.
	UFUNCTION(BlueprintCallable, Category = "Simulation")
	bool GetIntersectionInfo(int64 NodeId, FIntersectionNodeInfo& OutInfo);

	// One-frame debug overlay for an inspected node: junction setback circle
	// around the node center plus a bar across every approach at its stop
	// line (orange when the stop line had to be clamped onto a short edge).
	// The inspector panel calls this every tick while open.
	UFUNCTION(BlueprintCallable, Category = "Simulation")
	void DrawIntersectionDebug(int64 NodeId);

	//for the start sim button
	UPROPERTY(BlueprintReadWrite, Category = "Simulation")
	bool bSimulationRunning = false;
	//for the stop sim button

	UFUNCTION(BlueprintCallable, Category = "Simulation")
		void StartSimulation();
	UFUNCTION(BlueprintCallable, Category = "Simulation")
		void StopSimulation();

	// Transport state for the HUD sim control bar. Pause freezes stepping in
	// place but keeps the engine and vehicles alive (unlike StopSimulation,
	// which resets everything and kicks off the telemetry pipeline).
	UPROPERTY(BlueprintReadOnly, Category = "Simulation")
	bool bSimulationPaused = false;

	// Playback rate: scales how much sim time accumulates per real second. The
	// fixed step is unchanged, so physics behave identically at every speed.
	UPROPERTY(BlueprintReadOnly, Category = "Simulation")
	float SimSpeedMultiplier = 1.0f;

	UFUNCTION(BlueprintCallable, Category = "Simulation")
	void SetSimulationPaused(bool bPaused);

	UFUNCTION(BlueprintCallable, Category = "Simulation")
	void SetSimulationSpeed(float Multiplier);

	// Creates a button in the Unreal Editor to generate the map
	UFUNCTION(CallInEditor, Category = "Simulation Setup")

	void GenerateRoadsInEditor();

	// Creates a button to clear the map
	UFUNCTION(CallInEditor, Category = "Simulation Setup")
	void ClearRoadsInEditor();

	virtual void Tick(float DeltaTime) override; // Called every frame

	UFUNCTION(BlueprintCallable, Category = "Simulation")
	void NotifyBackendOfNewRoad(int64 StartNodeId, int64 EndNodeId, FVector EndNodeUnrealLoc, float LengthMeters, int32 Lanes, float SpeedLimit);

	// Splits the live sim's edge U->V (and V->U when present) at a new node so
	// roads drawn from / crossing existing streets connect for traffic too.
	UFUNCTION(BlueprintCallable, Category = "Simulation")
	void SplitBackendEdge(int64 U, int64 V, int64 NewNodeId, FVector SplitUnrealLoc);

	// Pushes edited lane count / speed limit / turn lanes to the live sim's
	// edge(s). TurnLanes is OSM turn:lanes syntax for the U->V direction (the
	// V->U edge gets the mirrored string); empty means no explicit data and
	// the sim re-infers a per-lane map from the intersection layout.
	UFUNCTION(BlueprintCallable, Category = "Simulation")
	void UpdateBackendRoad(int64 U, int64 V, int32 Lanes, float SpeedMps, const FString& TurnLanes, bool bBothDirections);

	// Re-plants the traffic light / stop sign fixtures from the visual
	// network's current node controls. Call after any road edit that can
	// create, remove, or re-prioritize an intersection (draw, split, delete,
	// lane/speed change); cheap enough to run once per committed edit.
	UFUNCTION(BlueprintCallable, Category = "Simulation")
	void RebuildTrafficControls();

	// Deletes edge U->V (and V->U when bBothDirections) from the live sim.
	// Vehicles on the road are despawned; routes through it are repaired.
	UFUNCTION(BlueprintCallable, Category = "Simulation")
	void DeleteBackendRoad(int64 U, int64 V, bool bBothDirections);

	// Asks the sim to replan routes for vehicles passing near a road edit, so
	// existing traffic discovers new connections instead of only new spawns.
	UFUNCTION(BlueprintCallable, Category = "Simulation")
	void RequestBackendReroutes(FVector CenterUnrealLoc, float RadiusMeters);

private:
	// The JSONL pair to simulate: the active roadmap chosen in the main menu,
	// or the bundled default map when launched straight into this level.
	void ResolveActiveMapPaths(FString& OutNodesPath, FString& OutEdgesPath) const;

	// Builds the active roadmap's network and writes the graph-geometry CSV the
	// Python heatmap pipeline draws on. Returns false (and logs) on failure.
	bool ExportActiveNetworkGraph(const FString& NodesPath, const FString& EdgesPath, const FString& OutCsvPath) const;

	TrafficSimulation* TrafficSimEngine;

	double Accumulator = 0.0;

	int StepCount = 0;

	void StepSimulation(double dt);

	void UpdateVehicleVisuals(float Alpha, bool bDidPhysicsStep);
	Network* MyRoadNetwork;
    ARoadNetworkVisualizer* NetworkVisualizer;
	ATrafficControlVisualizer* TrafficControlVisualizer = nullptr;
	TMap<int32, FVehicleTransformState> InterpolationData;
	TMap<int32, int32> InstanceIndexToVehicleId;

	// Scratch buffers reused across frames so the render push is allocation-free
	// once warm. Reset() (not Empty()) keeps their capacity.
	std::vector<VehicleRenderState> RenderStateBuffer;
	TArray<FTransform> VehicleTransforms;
	TSet<int32> ActiveVehicleIDScratch;

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	bool bWaitingForTelemetry = false;

	// Throttles the telemetry done-file existence check to ~2 Hz; a per-frame
	// filesystem stat on the game thread is wasted work.
	float TelemetryPollAccumulator = 0.0f;

	FString TelemetryDoneFilePath;
	
	// Single HISM for the minimal MVP
	UPROPERTY(EditDefaultsOnly, Category = "Traffic Visuals")
	UInstancedStaticMeshComponent* VehicleISM;

	// Time step configuration (e.g., 0.1f for 10 updates/second)
	UPROPERTY(EditAnywhere, Category = "Simulation Settings")
	float FixedDelta;

	// Max physics steps per rendered frame. When a slow machine falls behind,
	// the excess sim time is dropped rather than queued, so one long frame
	// can't snowball into ever-more steps per frame.
	UPROPERTY(EditAnywhere, Category = "Simulation Settings")
	int32 MaxStepsPerFrame;

	// Real-time ceiling (ms) the fixed-step loop may spend per frame when
	// fast-forwarding (>1x). Keeps a heavy frame from grinding through all its
	// sub-steps and hitching; the sim slow-mos toward the target speed instead.
	// Disabled at <=1x, so normal playback is unaffected.
	UPROPERTY(EditAnywhere, Category = "Simulation Settings")
	float StepTimeBudgetMs = 8.0f;

	// On-screen per-frame sim stats (steps/frame, backend car count). Off by
	// default: the Printf + AddOnScreenDebugMessage every frame is pure waste
	// outside of debugging sessions.
	UPROPERTY(EditAnywhere, Category = "Simulation Settings")
	bool bShowDebugStats = false;

};
