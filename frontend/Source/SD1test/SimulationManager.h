// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SimulationManager.generated.h"

UCLASS()
class ASimulationManager : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	ASimulationManager();

	virtual void Tick(float DeltaTime) override; // Called every frame

private:
	// Fixed time step of 60 hz (1/60 seconds)
	const double FixedDelta = 1.0 / 60.0;
	
	double Accumulator = 0.0;

	int StepCount = 0;

	void StepSimulation(double dt);

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;


};
