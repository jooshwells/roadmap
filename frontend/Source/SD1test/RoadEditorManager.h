#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "network.h" 
#include "RoadEditorManager.generated.h"

// Explicit declaration of the tool state tracking enum
UENUM(BlueprintType)
enum class ERoadToolType : uint8
{
	None,
	Straight,
	Turn90,
	Intersection4Way,
	Delete,
	BezierCurve
};

class USplineComponent;

UCLASS()
class SD1TEST_API ARoadEditorManager : public AActor
{
	GENERATED_BODY()

public:
	ARoadEditorManager();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Editor Settings")
	class ARoadNetworkVisualizer* VisualizerTarget;

	// Components & States for Modular Placement ===
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Road Editor")
	UStaticMeshComponent* GhostMeshComponent;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road Editor")
	ERoadToolType ActiveTool;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road Editor")
	bool bIsPlacingRoad;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road Editor")
	int32 SelectedLaneCount = 2;

protected:
	virtual void BeginPlay() override;

public:
	// Cleanly declared exactly once
	virtual void Tick(float DeltaTime) override;

	UFUNCTION(BlueprintCallable, Category = "Road Editor")
	void HandleMouseClick();

private:
	uint64 LastPlacedNodeID;
	FVector LastPlacedPhysicalLocation;
	Network* SimulationNetwork;

public:

	// Configurable lane width in Unreal units
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road Editor")
	float LaneWidth = 300.0f;

	// Call this to update the ghost material color based on the current tool
	UFUNCTION(BlueprintCallable, Category = "Road Editor")
	void EndCurrentRoadSegment(int32 LaneCount, FVector StartPoint, FVector EndPoint, UInstancedStaticMeshComponent* TargetISMComponent);

	UFUNCTION(BlueprintCallable, Category = "Road Editor")
	void ClearAllRoads();

	UFUNCTION(BlueprintCallable, Category = "Road Editor")
	void DeleteRoadSegmentUnderCursor();

	// Called once the file loader finishes reading the Central Florida map to set the active network for editing and visualization
	UFUNCTION(BlueprintCallable, Category = "Road Editor")
	void SetActiveNetwork(int64 ExternalNetworkPointerAddress);
private:
	// Cache physical world clicks for the current editing session.
	TArray<FVector> StagedClickLocations;

	void ClearStagedVisuals();

public:
	// Call this whenever the UI changes the ActiveTool
	UFUNCTION(BlueprintCallable, Category = "Road Editor")
	void UpdateGhostVisuals();

private:
	// Holds the dynamic material so we can tweak color parameters at runtime
	UPROPERTY()
	UMaterialInstanceDynamic* GhostDynamicMaterial;

};