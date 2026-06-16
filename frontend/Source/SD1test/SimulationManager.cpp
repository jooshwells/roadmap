// Fill out your copyright notice in the Description page of Project Settings.


#include "SimulationManager.h"
#include "Engine/World.h"


// Sets default values
ASimulationManager::ASimulationManager()
{
 	// Set this actor to call Tick() every frame. Can be turned off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

}

// Called when the game starts or when spawned
void ASimulationManager::BeginPlay()
{
	Super::BeginPlay();

	Accumulator = 0.0;
	StepCount = 0;
	
}

// Called every frame
void ASimulationManager::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	DeltaTime = FMath::Min(DeltaTime, 0.25f); // Avoid spiral of death

	Accumulator += DeltaTime;

	int StepsThisFrame = 0;

	while (Accumulator >= FixedDelta)
	{
		StepSimulation(FixedDelta);
		Accumulator -= FixedDelta;
		StepsThisFrame++;
	}

	// Debug
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 0.0f, FColor::Green, FString::Printf(TEXT("Steps this frame: %d"), StepsThisFrame));
	}

}

void ASimulationManager::StepSimulation(double dt)
{
	StepCount++;
	// Debug
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 0.0f, FColor::Yellow, FString::Printf(TEXT("Step count: %d"), StepCount));
	}
}

