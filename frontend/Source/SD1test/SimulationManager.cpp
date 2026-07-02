// Fill out your copyright notice in the Description page of Project Settings.
#include "SimulationManager.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "network_builder.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "PythonBridge.h"

// Sets default values
ASimulationManager::ASimulationManager()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	Accumulator = 0.0f;
	FixedDelta = 0.016f;
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

	// 2. Load the map and initialize physics (this is where your JSON paths get called)
	TrafficSimEngine->Initialize();

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

void ASimulationManager::GenerateRoadsInEditor()
{
	UE_LOG(LogTemp, Log, TEXT("Generate button clicked!"));
	// 1. Clean up old data if you click the button multiple times
	ClearRoadsInEditor();
	FString ProjectDir = FPaths::ProjectContentDir();

	// 2. Build the path to the python_pipeline folder
	// Since python_pipeline is next to frontend, we go up one level from the project root
	FString NodesPath = FPaths::Combine(ProjectDir, TEXT("ThirdParty/MapData/waterford_nodes_orange_allroads_offline_xy.jsonl"));
	FString EdgesPath = FPaths::Combine(ProjectDir, TEXT("ThirdParty/MapData/waterford_edges_orange_allroads_offline_xy.jsonl"));

	// 3. (Optional but recommended) Convert it to a clean, absolute path
	FPaths::CollapseRelativeDirectories(NodesPath);
	FPaths::CollapseRelativeDirectories(EdgesPath);
	// 2. Build your simulator network. 
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

		if (TelemetryStatusWidget)
		{
			TelemetryStatusWidget->RemoveFromParent();
			TelemetryStatusWidget = nullptr;
		}

		ShowHeatmapOverlay();
	}

	if (!TrafficSimEngine || !bSimulationRunning) return;

	DeltaTime = FMath::Min(DeltaTime, 0.25f);

	Accumulator += DeltaTime;
	int StepsThisFrame = 0;

	while (Accumulator >= FixedDelta)
	{
		TrafficSimEngine->Step(FixedDelta);
		Accumulator -= FixedDelta;
		StepsThisFrame++;
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
        TrafficSimEngine->Initialize();
    }

    bSimulationRunning = true;
    if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Green, TEXT("Simulation Started!"));
}
void ASimulationManager::StopSimulation()
{
	bSimulationRunning = false;
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
	// Prefer the project's virtual environment if it exists.
	// Otherwise fall back to the system Python installation.
	FString VenvPython = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(ProjectContentDir, TEXT("ThirdParty/python_pipeline/telemetry/.venv/Scripts/python.exe"))
	);

	FString PythonExePath;

	if (FPaths::FileExists(VenvPython))
	{
		PythonExePath = VenvPython;
	}
	else
	{
		PythonExePath = TEXT("python");
	}

	if (FPaths::FileExists(VenvPython))
	{
		PythonExePath = VenvPython;
		UE_LOG(LogTemp, Warning, TEXT("Using project virtual environment."));
	}
	else
	{
		PythonExePath = TEXT("python");
		UE_LOG(LogTemp, Warning, TEXT("Using system Python from PATH."));
	}

	FString ScriptPath = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(ProjectContentDir, TEXT("ThirdParty/python_pipeline/telemetry/run_pipeline.py"))
	);

	FString SimulationCsvPath = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(ProjectDir, TEXT("simulation_output.csv"))
	);

	FString TelemetryDonePath = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(ProjectContentDir, TEXT("ThirdParty/python_pipeline/telemetry/telemetry_done.txt"))
	);
	TelemetryDoneFilePath = TelemetryDonePath;
	bWaitingForTelemetry = true;

	// Remove the old done file so this run has to create a fresh one.
	if (FPaths::FileExists(TelemetryDonePath))
	{
		IFileManager::Get().Delete(*TelemetryDonePath);
	}

	// Show a small status widget while Python generates the telemetry outputs.
	if (TelemetryStatusClass)
	{
		TelemetryStatusWidget = CreateWidget<UUserWidget>(GetWorld(), TelemetryStatusClass);

		if (TelemetryStatusWidget)
		{
			TelemetryStatusWidget->AddToViewport(100);
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("TelemetryStatusClass is not assigned."));
	}

	// Run the Python telemetry pipeline after the simulation has finished.
	PythonBridge::RunTelemetryAnalysis(
		PythonExePath,
		ScriptPath,
		SimulationCsvPath
	);

	//ShowHeatmapOverlay();
	
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
			FRotator UnrealRotation(0.0f, FMath::RadiansToDegrees(State.yaw), 0.0f);
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

void ASimulationManager::ShowHeatmapOverlay()
{
	if (!HeatmapOverlayClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("HeatmapOverlayClass is not assigned."));
		return;
	}

	UUserWidget* HeatmapWidget = CreateWidget<UUserWidget>(GetWorld(), HeatmapOverlayClass);

	if (HeatmapWidget)
	{
		HeatmapWidget->AddToViewport(100);
	}
}

bool ASimulationManager::GetVehicleStatsFromInstance(int32 InstanceIndex, FVehicleIDMStats& OutStats)
{
	if (!TrafficSimEngine || !InstanceIndexToVehicleId.Contains(InstanceIndex)) return false;

	int32 TargetVehId = InstanceIndexToVehicleId[InstanceIndex];

	// Find the vehicle in the backend
	for (VehicleState* v : TrafficSimEngine->GetActiveVehicles())
	{
		if (!v) continue; // If the pointer is null, skip it!
		if (v->getId() == TargetVehId)
		{
			OutStats.VehicleID = TargetVehId;
			OutStats.CurrentSpeed = v->getSpeed();
			OutStats.DesiredSpeed = v->getDesiredSpeed();
			OutStats.MaxAcceleration = v->getMaxAccel();
			OutStats.AccelerationExponent = v->getAccelExp();
			OutStats.MinGap = v->getMinGap();
			OutStats.SafeBrakePower = v->getSafeBrakePower();
			OutStats.SafeTimeHeadway = v->getSafeTimeHeadway();
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

void ASimulationManager::NotifyBackendOfNewRoad(int64 StartNodeId, int64 EndNodeId, FVector EndNodeUnrealLoc, float LengthMeters, int32 Lanes) {
    if (TrafficSimEngine && NetworkVisualizer) {
        // Convert Unreal units back to Map Coordinates using the Visualizer's offsets
        double BackendX = (EndNodeUnrealLoc.X / 100.0) + NetworkVisualizer->OriginOffsetX;
        double BackendY = (EndNodeUnrealLoc.Y / 100.0) + NetworkVisualizer->OriginOffsetY;

        // Push to the live simulation
        TrafficSimEngine->AddRuntimeRoad(StartNodeId, EndNodeId, BackendX, BackendY, LengthMeters, Lanes);
        
        if (GEngine) {
            GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Cyan, TEXT("Live Graph Updated!"));
        }
    }
}