// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

THIRD_PARTY_INCLUDES_START
#include "network.h"
#include "network_builder.h"
#include "RoadNetworkVisualizer.h"
THIRD_PARTY_INCLUDES_END

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

	// Creates a button in the Unreal Editor to generate the map
	UFUNCTION(CallInEditor, Category = "Simulation Setup")
	void GenerateRoadsInEditor();

	// Creates a button to clear the map
	UFUNCTION(CallInEditor, Category = "Simulation Setup")
	void ClearRoadsInEditor();

	virtual void Tick(float DeltaTime) override; // Called every frame

private:
	// Fixed time step of 60 hz (1/60 seconds)
	const double FixedDelta = 1.0 / 60.0;
	
	double Accumulator = 0.0;

	int StepCount = 0;

	void StepSimulation(double dt);

	Network* MyRoadNetwork;
    ARoadNetworkVisualizer* NetworkVisualizer;

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;


};
