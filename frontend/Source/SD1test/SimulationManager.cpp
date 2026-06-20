// Fill out your copyright notice in the Description page of Project Settings.
#include "SimulationManager.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "network_builder.h"

// Sets default values
ASimulationManager::ASimulationManager()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	Accumulator = 0.0f;
	FixedDelta = 0.1f;
	TrafficSimEngine = nullptr;

	// Use standard ISM for moving objects!
	VehicleISM = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("VehicleISM"));
	RootComponent = VehicleISM;

	// CRITICAL FOR PERFORMANCE: Disable collision on the moving vehicles
	VehicleISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	VehicleISM->SetCollisionProfileName(TEXT("NoCollision"));
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

	if (!TrafficSimEngine || !VehicleISM) return;

	auto RenderStates = TrafficSimEngine->GetVehicleRenderStates();
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(2, 0.1f, FColor::Green, FString::Printf(TEXT("Backend Active Cars: %d"), (int32)RenderStates.size()));
	}
	TArray<FTransform> Transforms;
	Transforms.Reserve(RenderStates.size()); // Pre-allocate memory for speed!

	for (const auto& State : RenderStates)
	{
		FVector UnrealPosition(State.x * 100.0f, State.y * 100.0f, State.z * 100.0f);
		FRotator UnrealRotation(0.0f, FMath::RadiansToDegrees(State.yaw), 0.0f);
		Transforms.Add(FTransform(UnrealRotation, UnrealPosition));
	}

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
		VehicleISM->BatchUpdateInstancesTransforms(0, Transforms, false, true, true);
	}

	if (CurrentCount > TargetCount)
	{
		for (int32 i = TargetCount; i < CurrentCount; ++i)
		{
			// CRITICAL FIX: The 4th argument (bMarkRenderStateDirty) MUST BE TRUE
			// Otherwise the deleted cars stay permanently frozen on your screen!
			VehicleISM->UpdateInstanceTransform(i, FTransform(FRotator::ZeroRotator, FVector::ZeroVector, FVector::ZeroVector), false, true, true);
		}
	}
}