// Fill out your copyright notice in the Description page of Project Settings.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RoadEditorManager.generated.h"

UCLASS()
class SD1TEST_API ARoadEditorManager : public AActor
{
	GENERATED_BODY()

public:
	ARoadEditorManager();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Editor Settings")
	TSubclassOf<class ARoadActor> RoadSegmentClass;

protected:
	virtual void BeginPlay() override;

public:
	virtual void Tick(float DeltaTime) override;

	void HandleMouseClick();

private:
	UPROPERTY()
	class ARoadNodeActor* LastPlacedNode;

public:
	// Clear the tracking reference so the next click is a new road.
	UFUNCTION(BlueprintCallable, Category="Road Editor")
	void EndCurrentRoadSegment();

public:
	// Utility function to clear all existing road segments and nodes from the level. Can be called from the editor or via a keybind.
	UFUNCTION(BlueprintCallable, Category="Road Editor")
	void ClearAllRoads();

public:
	// Utility function to delete a road segment and its nodes by clicking on it while holding a modifier key (e.g. Shift). Can be called from the editor or via a keybind.
	UFUNCTION(BlueprintCallable, Category = "Road Editor")
	void DeleteRoadSegmentUnderCursor();
};