#pragma once
#include "Blueprint/UserWidget.h" 
#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "SimulationManager.h"
#include "MapPlayerController.generated.h"
UCLASS()
class SD1TEST_API AMapPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	// Expose a slot in the editor so you can select your WBP_MapEditorUI
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UI")
	TSubclassOf<UUserWidget> EditorUIClass;

	// Pointer to hold the created widget in memory
	UPROPERTY()
	UUserWidget* EditorUIWidget;

	// updated for turn lanes and speed limit
	UFUNCTION(BlueprintCallable, Category = "Map Editor")
    void SetDrawMode(bool bEnable, int32 InLanes, bool bTwoWay, float InSpeedLimit, FString InTurnLanes);

	UFUNCTION(BlueprintImplementableEvent, Category = "UI")
	void OnVehicleClickedUI(FVehicleIDMStats VehicleStats);

protected:
	// Runs when the game starts
	virtual void BeginPlay() override;

	// Where we bind our input keys
	virtual void SetupInputComponent() override;

	// The function that fires when we click
	void OnLeftMouseClick();

private:
	bool bIsDrawingMode = false;
	int32 CurrentDrawLanes = 2; // Default from UI

	bool bIsTwoWayStreet = false;

	bool bHasStartNode = false;
	int64 StartNodeId = -1;
	FVector StartNodeLocation;

	// Helper to find exactly where the mouse is on the Z=0 plane
	bool GetMouseIntersectionOnZPlane(FVector& OutIntersection);

	// How close the mouse needs to be to an intersection to snap (e.g., 2000 = 20 meters)
	float SnapRadius = 2000.0f;

	// Cached reference to your visualizer
	class ARoadNetworkVisualizer* CachedVisualizer;

	float CurrentDrawSpeedLimit = 20f; // default around 45 mph
    FString CurrentDrawTurnLanes;   // string for turn lanes
};