// Fill out your copyright notice in the Description page of Project Settings.
#include "SimulationManager.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "network_builder.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "PythonBridge.h"
#include "RoadmapGameInstance.h"
#include "MapPlayerController.h"
#include "Kismet/GameplayStatics.h"

// Sets default values
ASimulationManager::ASimulationManager()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	Accumulator = 0.0f;
	// 30 Hz sim: IDM/MOBIL are stable well below this, and UpdateVehicleVisuals
	// interpolates between steps, so rendering stays smooth at any frame rate.
	FixedDelta = 0.0333f;
	MaxStepsPerFrame = 4;
	TrafficSimEngine = nullptr;

	// Use standard ISM for moving objects!
	VehicleISM = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("VehicleISM"));
	RootComponent = VehicleISM;

	// CRITICAL FOR PERFORMANCE: Disable collision on the moving vehicles
	VehicleISM->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	VehicleISM->SetCollisionResponseToAllChannels(ECR_Block); // Block raycasts
	VehicleISM->SetCollisionProfileName(TEXT("QueriesOnly"));
	VehicleISM->SetGenerateOverlapEvents(false);

	// Disable shadows for the MVP to guarantee maximum GPU performance
	VehicleISM->SetCastShadow(false);
}

void ASimulationManager::BeginPlay()
{
	Super::BeginPlay();


	// If the network hasn't been generated in the editor yet, build it when the game starts
	if (!MyRoadNetwork)
	{
		GenerateRoadsInEditor();
	}

	if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Yellow, TEXT("Initializing Traffic Simulation Backend..."));

	// 1. Allocate the memory for the backend
	TrafficSimEngine = new TrafficSimulation();

	// 2. Load the active roadmap and initialize physics
	FString NodesPath, EdgesPath;
	ResolveActiveMapPaths(NodesPath, EdgesPath);
	TrafficSimEngine->Initialize(TCHAR_TO_UTF8(*NodesPath), TCHAR_TO_UTF8(*EdgesPath));

	if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Green, TEXT("Traffic Simulation Initialized successfully!"));
}

void ASimulationManager::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	if (TrafficSimEngine)
	{
		// Destroy the simulation first so the telemetry logger finishes writing the CSV.
		delete TrafficSimEngine;
		TrafficSimEngine = nullptr;
	}
}

void ASimulationManager::ResolveActiveMapPaths(FString& OutNodesPath, FString& OutEdgesPath) const
{
	// Prefer the roadmap the player picked in the main menu. The GameInstance
	// is null when this runs from the editor's CallInEditor button.
	if (const UWorld* World = GetWorld())
	{
		if (const URoadmapGameInstance* GameInstance = World->GetGameInstance<URoadmapGameInstance>())
		{
			if (GameInstance->HasActiveRoadmap())
			{
				OutNodesPath = GameInstance->GetActiveNodesPath();
				OutEdgesPath = GameInstance->GetActiveEdgesPath();
				UE_LOG(LogTemp, Log, TEXT("Simulating roadmap '%s'"), *GameInstance->GetActiveRoadmapName());
				return;
			}
		}
	}

	// Fallback: the bundled default map (editor tools, or PIE straight into MainLevel).
	FString ProjectDir = FPaths::ProjectContentDir();
	OutNodesPath = FPaths::Combine(ProjectDir, TEXT("ThirdParty/MapData/waterford_nodes_orange_allroads_offline_xy.jsonl"));
	OutEdgesPath = FPaths::Combine(ProjectDir, TEXT("ThirdParty/MapData/waterford_edges_orange_allroads_offline_xy.jsonl"));
	FPaths::CollapseRelativeDirectories(OutNodesPath);
	FPaths::CollapseRelativeDirectories(OutEdgesPath);
}

bool ASimulationManager::ExportActiveNetworkGraph(const FString& NodesPath, const FString& EdgesPath, const FString& OutCsvPath) const
{
	// Make sure the destination folder exists before the sim tries to open the file.
	const FString OutDir = FPaths::GetPath(OutCsvPath);
	if (!OutDir.IsEmpty())
	{
		IFileManager::Get().MakeDirectory(*OutDir, /*Tree=*/true);
	}

	// Build a throwaway network purely to emit the graph CSV. Using the same
	// NetworkBuilder as the simulation guarantees the exported edge_ids line up
	// with the EdgeIDs recorded in the telemetry output.
	Network GraphNetwork = NetworkBuilder::buildNetworkFromJSONL(
		TCHAR_TO_UTF8(*NodesPath),
		TCHAR_TO_UTF8(*EdgesPath)
	);

	GraphNetwork.visualizeNetworkForPython(TCHAR_TO_UTF8(*OutCsvPath));

	if (!FPaths::FileExists(OutCsvPath))
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to export network graph CSV to: %s"), *OutCsvPath);
		return false;
	}

	UE_LOG(LogTemp, Log, TEXT("Exported network graph CSV for the active roadmap to: %s"), *OutCsvPath);
	return true;
}

void ASimulationManager::GenerateRoadsInEditor()
{
	UE_LOG(LogTemp, Log, TEXT("Generate button clicked!"));
	// 1. Clean up old data if you click the button multiple times
	ClearRoadsInEditor();

	// 2. Resolve which roadmap to build (menu selection or bundled default)
	FString NodesPath, EdgesPath;
	ResolveActiveMapPaths(NodesPath, EdgesPath);

	// 3. Build your simulator network.
	// (If this crashes or fails to load the JSONs in the editor, change these to absolute paths like "C:/dev/roadmap/...")
	MyRoadNetwork = new Network(NetworkBuilder::buildNetworkFromJSONL(
		TCHAR_TO_UTF8(*NodesPath),
		TCHAR_TO_UTF8(*EdgesPath)
	));

	if (MyRoadNetwork)
	{
		UE_LOG(LogTemp, Warning, TEXT("Network pointer created."));
	}

	// 3. Spawn the Visualizer using the assigned Blueprint
	if (VisualizerBlueprint)
	{
		UE_LOG(LogTemp, Log, TEXT("Blueprint is assigned, attempting to spawn..."));
		FActorSpawnParameters SpawnParams;
		NetworkVisualizer = GetWorld()->SpawnActor<ARoadNetworkVisualizer>(
			VisualizerBlueprint,
			FVector::ZeroVector,
			FRotator::ZeroRotator,
			SpawnParams
		);

		// 4. Command the visualizer to render the instances
		if (NetworkVisualizer)
		{
			NetworkVisualizer->BuildVisualNetwork(MyRoadNetwork, NodesPath, EdgesPath);
			if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Green, TEXT("Roads Generated Successfully!"));
		}
	}
	else
	{
		if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, TEXT("WARNING: Visualizer Blueprint not assigned in SimulationManager!"));
	}
}

void ASimulationManager::ClearRoadsInEditor()
{
	// Destroy the visualizer actor if it exists
	if (NetworkVisualizer)
	{
		NetworkVisualizer->Destroy();
		NetworkVisualizer = nullptr;
	}

	// Free the C++ memory
	if (MyRoadNetwork)
	{
		delete MyRoadNetwork;
		MyRoadNetwork = nullptr;
	}
}

// Called every frame
void ASimulationManager::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (bWaitingForTelemetry && FPaths::FileExists(TelemetryDoneFilePath))
	{
		bWaitingForTelemetry = false;

		// The run is ready for the user to open from the telemetry panel.
		UE_LOG(LogTemp, Log, TEXT("Telemetry run saved and ready in the telemetry panel."));
	}

	if (!TrafficSimEngine || !bSimulationRunning || bSimulationPaused) return;

	DeltaTime = FMath::Min(DeltaTime, 0.25f);

	Accumulator += DeltaTime * SimSpeedMultiplier;
	int StepsThisFrame = 0;

	// Fast-forward needs proportionally more steps per frame or the backlog
	// drop below would cancel the speed-up; keep the plain cap at 1x and below.
	const int32 StepCap = FMath::CeilToInt(MaxStepsPerFrame * FMath::Max(1.0f, SimSpeedMultiplier));

	while (Accumulator >= FixedDelta && StepsThisFrame < StepCap)
	{
		TrafficSimEngine->Step(FixedDelta);
		Accumulator -= FixedDelta;
		StepsThisFrame++;
	}

	// If the machine couldn't keep up this frame, drop the whole-step backlog
	// (sim runs briefly in slow motion) instead of demanding even more steps
	// next frame. Keep the sub-step remainder so Alpha stays in [0, 1).
	if (Accumulator >= FixedDelta)
	{
		Accumulator = FMath::Fmod(Accumulator, static_cast<double>(FixedDelta));
	}

	float Alpha = Accumulator / FixedDelta;
	UpdateVehicleVisuals(Alpha, StepsThisFrame > 0);

	// Debug
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 0.0f, FColor::Green, FString::Printf(TEXT("Steps this frame: %d"), StepsThisFrame));
	}
}

void ASimulationManager::StartSimulation()
{
    if (!TrafficSimEngine)
    {
        TrafficSimEngine = new TrafficSimulation();
        FString NodesPath, EdgesPath;
        ResolveActiveMapPaths(NodesPath, EdgesPath);
        TrafficSimEngine->Initialize(TCHAR_TO_UTF8(*NodesPath), TCHAR_TO_UTF8(*EdgesPath));
    }

    bSimulationRunning = true;
    bSimulationPaused = false;

    // Road editing is pre-run only: kick the controller out of draw mode and
    // close the edit panel so the map turns view-only while cars are moving.
    if (AMapPlayerController* PC = Cast<AMapPlayerController>(UGameplayStatics::GetPlayerController(GetWorld(), 0)))
    {
        PC->NotifySimulationStarted();
    }

    if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Green, TEXT("Simulation Started!"));
}
void ASimulationManager::SetSimulationPaused(bool bPaused)
{
	bSimulationPaused = bPaused;
}

void ASimulationManager::SetSimulationSpeed(float Multiplier)
{
	SimSpeedMultiplier = FMath::Clamp(Multiplier, 0.25f, 8.0f);
}

void ASimulationManager::StopSimulation()
{
	bSimulationRunning = false;
	bSimulationPaused = false;
	// Clear all vehicle visuals
	if (VehicleISM)
	{
		VehicleISM->ClearInstances();
	}

	// Reset the accumulator
	Accumulator = 0.0f;

	// Destroy and recreate the simulation engine to reset state
	if (TrafficSimEngine)
	{
		delete TrafficSimEngine;
		TrafficSimEngine = nullptr;
	}

	FString ProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	FString ProjectContentDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir());	

	FString SourceTelemetryDir = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(ProjectDir, TEXT("../python_pipeline/telemetry"))
	);

	FString PackagedTelemetryDir = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(ProjectContentDir, TEXT("ThirdParty/python_pipeline/telemetry"))
	);

	FString PipelineExePath = FPaths::Combine(PackagedTelemetryDir, TEXT("run_pipeline.exe"));

	if (!FPaths::FileExists(PipelineExePath))
	{
		PipelineExePath = FPaths::Combine(SourceTelemetryDir, TEXT("run_pipeline.exe"));
	}

	if (!FPaths::FileExists(PipelineExePath))
	{
		UE_LOG(LogTemp, Error, TEXT("Telemetry executable not found in packaged or source telemetry folder."));
		UE_LOG(LogTemp, Error, TEXT("Checked packaged path: %s"), *FPaths::Combine(PackagedTelemetryDir, TEXT("run_pipeline.exe")));
		UE_LOG(LogTemp, Error, TEXT("Checked source path: %s"), *FPaths::Combine(SourceTelemetryDir, TEXT("run_pipeline.exe")));
		return;
	}

	FString TelemetryDir = FPaths::GetPath(PipelineExePath);

	FString SimulationCsvPath = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(ProjectDir, TEXT("simulation_output.csv"))
	);

	// Generate the network-graph CSV for whichever roadmap was simulated so the
	// heatmaps draw on the active map instead of the bundled Waterford default.
	FString NodesPath, EdgesPath;
	ResolveActiveMapPaths(NodesPath, EdgesPath);

	FString NetworkGraphCsvPath = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(TelemetryDir, TEXT("data/network/network_graph_active.csv"))
	);

	ExportActiveNetworkGraph(NodesPath, EdgesPath, NetworkGraphCsvPath);

	FString TelemetryDonePath = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(TelemetryDir, TEXT("telemetry_done.txt"))
	);

	TelemetryDoneFilePath = TelemetryDonePath;
	bWaitingForTelemetry = true;

	// Remove the old done file so this run has to create a fresh one.
	if (FPaths::FileExists(TelemetryDonePath))
	{
		IFileManager::Get().Delete(*TelemetryDonePath);
	}

	// Run telemetry from the active roadmap JSONL files. The shared active graph
	// above remains available, while Python stores its own graph with this run.
	PythonBridge::RunTelemetryAnalysis(
		PipelineExePath,
		SimulationCsvPath,
		NodesPath,
		EdgesPath
	);

	//TrafficSimEngine = new TrafficSimulation();
	//TrafficSimEngine->Initialize();
	InterpolationData.Empty();
	// TrafficSimEngine = new TrafficSimulation();
	// TrafficSimEngine->Initialize();

	if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Red, TEXT("Simulation Stopped & Reset!"));
}

void ASimulationManager::UpdateVehicleVisuals(float Alpha, bool bDidPhysicsStep)
{
	if (!TrafficSimEngine)
	{
		if (GEngine) GEngine->AddOnScreenDebugMessage(1, 0.1f, FColor::Red, TEXT("CRITICAL: Backend Engine is NULL!"));
		return;
	}

	if (!TrafficSimEngine || !VehicleISM) return;

	if (bDidPhysicsStep)
	{
		auto RenderStates = TrafficSimEngine->GetVehicleRenderStates();
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(2, 0.1f, FColor::Green, FString::Printf(TEXT("Backend Active Cars: %d"), (int32)RenderStates.size()));
		}
		TSet<int32> ActiveVehicleIDs;

		for (const auto& State : RenderStates)
		{
			ActiveVehicleIDs.Add(State.id);
			FVector UnrealPosition(State.x * 100.0f, State.y * 100.0f, State.z * 100.0f);
			// Pitch follows the road grade so cars sit flush on bridge ramps
			// instead of staying horizontal; positive pitch is nose-up, same
			// convention as the sim's climbing-positive grade.
			FRotator UnrealRotation(FMath::RadiansToDegrees(State.pitch), FMath::RadiansToDegrees(State.yaw), 0.0f);
			FTransform NewTransform(UnrealRotation, UnrealPosition);

			if (InterpolationData.Contains(State.id))
			{
				// Car already exists: Push the old target to history, and set the new target
				InterpolationData[State.id].Previous = InterpolationData[State.id].Target;
				InterpolationData[State.id].Target = NewTransform;
			}
			else
			{
				// Brand new car: Set both to the new transform so it doesn't fly in from 0,0,0
				InterpolationData.Add(State.id, { NewTransform, NewTransform });
			}
		}

		// Clean up cars that finished their routes and despawned
		for (auto It = InterpolationData.CreateIterator(); It; ++It)
		{
			if (!ActiveVehicleIDs.Contains(It.Key()))
			{
				It.RemoveCurrent();
			}
		}
	}

	// 2. GLIDE THE CARS (LERP)
	TArray<FTransform> Transforms;
	Transforms.Reserve(InterpolationData.Num());
	InstanceIndexToVehicleId.Reset(); // Keep memory reserved, but clear the map

	int32 Index = 0;
	for (const auto& Pair : InterpolationData)
	{
		int32 VehID = Pair.Key;
		const FVehicleTransformState& State = Pair.Value;

		// Linear interpolation for Location
		FVector LerpedLoc = FMath::Lerp(State.Previous.GetLocation(), State.Target.GetLocation(), Alpha);

		// Spherical interpolation (Slerp) for Rotation to ensure cars take the shortest rotational path!
		FQuat LerpedRot = FQuat::Slerp(State.Previous.GetRotation(), State.Target.GetRotation(), Alpha);

		Transforms.Add(FTransform(LerpedRot, LerpedLoc));
		InstanceIndexToVehicleId.Add(Index, VehID);
		Index++;
	}

	// 3. PUSH TO THE GPU (Your existing logic)
	int32 CurrentCount = VehicleISM->GetInstanceCount();
	int32 TargetCount = Transforms.Num();

	if (CurrentCount < TargetCount)
	{
		for (int32 i = CurrentCount; i < TargetCount; ++i)
		{
			VehicleISM->AddInstance(FTransform::Identity);
		}
	}

	if (Transforms.Num() > 0)
	{
		// Notice bTeleport is set to false here (the last parameter) to prevent TAA smearing!
		VehicleISM->BatchUpdateInstancesTransforms(0, Transforms, false, true, false);
	}

	if (CurrentCount > TargetCount)
	{
		for (int32 i = TargetCount; i < CurrentCount; ++i)
		{
			VehicleISM->UpdateInstanceTransform(i, FTransform(FRotator::ZeroRotator, FVector::ZeroVector, FVector::ZeroVector), false, true, false);
		}
	}
}

bool ASimulationManager::GetVehicleStatsFromInstance(int32 InstanceIndex, FVehicleIDMStats& OutStats)
{
	if (!TrafficSimEngine || !InstanceIndexToVehicleId.Contains(InstanceIndex)) return false;

	return GetVehicleStatsByID(InstanceIndexToVehicleId[InstanceIndex], OutStats);
}

bool ASimulationManager::GetVehicleStatsByID(int32 VehicleID, FVehicleIDMStats& OutStats)
{
	if (!TrafficSimEngine) return false;

	// Find the vehicle in the backend
	for (VehicleState* v : TrafficSimEngine->GetActiveVehicles())
	{
		if (!v) continue; // If the pointer is null, skip it!
		if (v->getId() == VehicleID)
		{
			OutStats.VehicleID = VehicleID;
			OutStats.CurrentSpeed = v->getSpeed();
			OutStats.DesiredSpeed = v->getDesiredSpeed();
			OutStats.MaxAcceleration = v->getMaxAccel();
			OutStats.AccelerationExponent = v->getAccelExp();
			OutStats.MinGap = v->getMinGap();
			OutStats.SafeBrakePower = v->getSafeBrakePower();
			OutStats.SafeTimeHeadway = v->getSafeTimeHeadway();

			OutStats.CurrentAcceleration = v->getAcceleration();
			OutStats.WaitTime = v->getWaitTime();
			OutStats.Lane = v->getLane();
			OutStats.Politeness = v->getPoliteness();
			OutStats.RouteIndex = static_cast<int32>(v->currentRouteIndex);
			OutStats.RouteLength = static_cast<int32>(v->currentRoute.size());
			if (const Road* Edge = v->getCurrentEdge())
			{
				OutStats.RoadSpeedLimit = static_cast<float>(Edge->getSpeedLimit());
			}
			return true;
		}
	}
	return false;
}

int32 ASimulationManager::GetInstanceIndexFromVehicleID(int32 VehicleID)
{
	// Search the map for the VehicleID. If found, return its current HISM Index.
	const int32* FoundIndex = InstanceIndexToVehicleId.FindKey(VehicleID);

	if (FoundIndex)
	{
		return *FoundIndex;
	}

	// Return -1 if the car is no longer in the simulation
	return -1;
}

void ASimulationManager::NotifyBackendOfNewRoad(int64 StartNodeId, int64 EndNodeId, FVector EndNodeUnrealLoc, float LengthMeters, int32 Lanes, float SpeedLimit) {
    if (TrafficSimEngine && NetworkVisualizer) {
        // Convert Unreal units back to Map Coordinates using the Visualizer's offsets
        double BackendX = (EndNodeUnrealLoc.X / 100.0) + NetworkVisualizer->OriginOffsetX;
        double BackendY = (EndNodeUnrealLoc.Y / 100.0) + NetworkVisualizer->OriginOffsetY;

        // Push to the live simulation with the new SpeedLimit
        TrafficSimEngine->AddRuntimeRoad(StartNodeId, EndNodeId, BackendX, BackendY, LengthMeters, Lanes, SpeedLimit);
        
        if (GEngine) {
            GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Cyan, TEXT("Live Graph Updated!"));
        }
    }
}

void ASimulationManager::SplitBackendEdge(int64 U, int64 V, int64 NewNodeId, FVector SplitUnrealLoc)
{
    if (TrafficSimEngine && NetworkVisualizer)
    {
        // Convert Unreal units back to Map Coordinates using the Visualizer's offsets
        double BackendX = (SplitUnrealLoc.X / 100.0) + NetworkVisualizer->OriginOffsetX;
        double BackendY = (SplitUnrealLoc.Y / 100.0) + NetworkVisualizer->OriginOffsetY;

        TrafficSimEngine->SplitRuntimeEdge(U, V, NewNodeId, BackendX, BackendY);
    }
}

void ASimulationManager::UpdateBackendRoad(int64 U, int64 V, int32 Lanes, float SpeedMps, bool bBothDirections)
{
    if (TrafficSimEngine)
    {
        TrafficSimEngine->UpdateRuntimeRoad(U, V, Lanes, SpeedMps, bBothDirections);
    }
}

void ASimulationManager::DeleteBackendRoad(int64 U, int64 V, bool bBothDirections)
{
    if (TrafficSimEngine)
    {
        TrafficSimEngine->DeleteRuntimeEdge(U, V, bBothDirections);
    }
}

void ASimulationManager::RequestBackendReroutes(FVector CenterUnrealLoc, float RadiusMeters)
{
    if (TrafficSimEngine && NetworkVisualizer)
    {
        // Convert Unreal units back to Map Coordinates using the Visualizer's offsets
        double BackendX = (CenterUnrealLoc.X / 100.0) + NetworkVisualizer->OriginOffsetX;
        double BackendY = (CenterUnrealLoc.Y / 100.0) + NetworkVisualizer->OriginOffsetY;

        TrafficSimEngine->QueueRouteReplansNear(BackendX, BackendY, RadiusMeters);
    }
}
