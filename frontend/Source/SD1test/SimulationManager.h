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
#include "Blueprint/UserWidget.h"

#include "SimulationManager.generated.h"

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
};


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
	
	UFUNCTION(BlueprintCallable, Category = "Simulation")
	bool GetVehicleStatsFromInstance(int32 InstanceIndex, FVehicleIDMStats& OutStats);

	//for the start sim button
	UPROPERTY(BlueprintReadWrite, Category = "Simulation")
	bool bSimulationRunning = false;
	//for the stop sim button
	
	UFUNCTION(BlueprintCallable, Category = "Simulation")
		void StartSimulation();
	UFUNCTION(BlueprintCallable, Category = "Simulation")
		void StopSimulation();

	UFUNCTION(BlueprintCallable, Category = "Heatmaps")
	void ShowHeatmapOverlay();

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

};