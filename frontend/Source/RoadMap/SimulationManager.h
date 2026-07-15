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

	UFUNCTION(BlueprintCallable, Category = "Heatmaps")
	void ShowHeatmapOverlay();

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

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// Widget to display telemetry heatmaps.
	UPROPERTY(EditAnywhere, Category = "Heatmaps")
	TSubclassOf<UUserWidget> HeatmapOverlayClass;

	UPROPERTY(EditAnywhere, Category = "Heatmaps")
	TSubclassOf<UUserWidget> TelemetryStatusClass;

	UPROPERTY()
	UUserWidget* TelemetryStatusWidget;

	bool bWaitingForTelemetry = false;

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

};