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

#include "TrafficSimulation.h"

#include "SimulationManager.generated.h"


UCLASS()
class SD1TEST_API ASimulationManager : public AActor
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	ASimulationManager();
	// This allows you to select your Blueprint in the Unreal Editor
	UPROPERTY(EditAnywhere, Category = "Simulation Setup")
	TSubclassOf<class ARoadNetworkVisualizer> VisualizerBlueprint;

	//for the start sim button
	UPROPERTY(BlueprintReadWrite, Category = "Simulation")
	bool bSimulationRunning = false;
	
	UFUNCTION(BlueprintCallable, Category = "Simulation")
	void StartSimulation();

	// Creates a button in the Unreal Editor to generate the map
	UFUNCTION(CallInEditor, Category = "Simulation Setup")

	void GenerateRoadsInEditor();

	// Creates a button to clear the map
	UFUNCTION(CallInEditor, Category = "Simulation Setup")
	void ClearRoadsInEditor();

	virtual void Tick(float DeltaTime) override; // Called every frame

private:
	TrafficSimulation* TrafficSimEngine;

	double Accumulator = 0.0;

	int StepCount = 0;

	void StepSimulation(double dt);

	void UpdateVehicleVisuals(float Alpha);

	Network* MyRoadNetwork;
	ARoadNetworkVisualizer* NetworkVisualizer;

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// Single HISM for the minimal MVP
	UPROPERTY(EditDefaultsOnly, Category = "Traffic Visuals")
	UInstancedStaticMeshComponent* VehicleISM;

	// Time step configuration (e.g., 0.1f for 10 updates/second)
	UPROPERTY(EditAnywhere, Category = "Simulation Settings")
	float FixedDelta;

};