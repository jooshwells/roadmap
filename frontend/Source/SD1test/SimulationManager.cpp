// Fill out your copyright notice in the Description page of Project Settings.
#include "SimulationManager.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "network_builder.h"

// Sets default values
ASimulationManager::ASimulationManager()
{
	PrimaryActorTick.bCanEverTick = true;

	Accumulator = 0.0f;
	FixedDelta = 0.1f;
	TrafficSimEngine = nullptr;

	// 1. Initialize the single HISM component and make it the root
	VehicleHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("VehicleHISM"));
	RootComponent = VehicleHISM;
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
		delete TrafficSimEngine;
		TrafficSimEngine = nullptr;
	}
}

void ASimulationManager::GenerateRoadsInEditor()
{
	UE_LOG(LogTemp, Log, TEXT("Generate button clicked!"));
	// 1. Clean up old data if you click the button multiple times
	ClearRoadsInEditor();

	// 2. Build your simulator network. 
	// (If this crashes or fails to load the JSONs in the editor, change these to absolute paths like "C:/dev/roadmap/...")
	MyRoadNetwork = new Network(NetworkBuilder::buildNetworkFromJSONL(
		"E:/dev/roadmap/python_pipeline/sample_out/waterford_nodes_orange_allroads_offline_xy.jsonl",
		"E:/dev/roadmap/python_pipeline/sample_out/waterford_edges_orange_allroads_offline_xy.jsonl"
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
			NetworkVisualizer->BuildVisualNetwork(MyRoadNetwork);
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

	if (!TrafficSimEngine) return;

	DeltaTime = FMath::Min(DeltaTime, 0.25f); // Avoid spiral of death

	Accumulator += DeltaTime;

	int StepsThisFrame = 0;

	while (Accumulator >= FixedDelta)
	{
		TrafficSimEngine->Step(FixedDelta);
		Accumulator -= FixedDelta;
		StepsThisFrame++;
	}

	float Alpha = Accumulator / FixedDelta;
	UpdateVehicleVisuals(Alpha);

	// Debug
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 0.0f, FColor::Green, FString::Printf(TEXT("Steps this frame: %d"), StepsThisFrame));
	}
}

void ASimulationManager::UpdateVehicleVisuals(float Alpha)
{
	if (!TrafficSimEngine)
	{
		if (GEngine) GEngine->AddOnScreenDebugMessage(1, 0.1f, FColor::Red, TEXT("CRITICAL: Backend Engine is NULL!"));
		return;
	}

	if (!TrafficSimEngine || !VehicleHISM) return;

	// 1. Fetch the lightweight render structs from the backend
	auto RenderStates = TrafficSimEngine->GetVehicleRenderStates();
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(2, 0.1f, FColor::Green, FString::Printf(TEXT("Backend Active Cars: %d"), (int32)RenderStates.size()));
	}
	// 2. Convert backend positions to Unreal Transforms
	TArray<FTransform> Transforms;
	for (const auto& State : RenderStates)
	{
		// SCALE FIX: Multiply meters by 100 to get Unreal Centimeters
		FVector UnrealPosition(State.x * 100.0f, State.y * 100.0f, State.z * 100.0f);

		// Convert radians back to degrees for Unreal's rotation system
		FRotator UnrealRotation(0.0f, FMath::RadiansToDegrees(State.yaw), 0.0f);

		Transforms.Add(FTransform(UnrealRotation, UnrealPosition));
	}

	// 3. Safely manage instance counts
	int32 CurrentCount = VehicleHISM->GetInstanceCount();
	int32 TargetCount = Transforms.Num();

	if (CurrentCount < TargetCount)
	{
		for (int32 i = CurrentCount; i < TargetCount; ++i)
		{
			VehicleHISM->AddInstance(FTransform::Identity);
		}
	}

	// 4. Batch Update all active instances simultaneously on the GPU
	if (Transforms.Num() > 0)
	{
		VehicleHISM->BatchUpdateInstancesTransforms(0, Transforms, false, true, true);
	}

	// 5. Hide excess instances (if cars left the sim) by scaling to 0
	if (CurrentCount > TargetCount)
	{
		for (int32 i = TargetCount; i < CurrentCount; ++i)
		{
			VehicleHISM->UpdateInstanceTransform(i, FTransform(FRotator::ZeroRotator, FVector::ZeroVector, FVector::ZeroVector), false, false, false);
		}
	}
}