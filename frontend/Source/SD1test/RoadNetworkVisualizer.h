#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"

#include "network.h" 
#include "RoadNetworkVisualizer.generated.h"

UCLASS()
class SD1TEST_API ARoadNetworkVisualizer : public AActor
{
    GENERATED_BODY()

public:

    ARoadNetworkVisualizer();

    // The HISM component that renders all road segments efficiently
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Road Network")
    UHierarchicalInstancedStaticMeshComponent* RoadHISM;

    // The length (X-axis) of your custom road mesh in centimeters before any scaling
    UPROPERTY(EditAnywhere, Category = "Road Visuals")
    float MeshBaseLengthCm = 100.0f;

    // The width (Y-axis) of your custom road mesh in centimeters before any scaling
    UPROPERTY(EditAnywhere, Category = "Road Visuals")
    float MeshBaseWidthCm = 100.0f;

    // Is the pivot point in the middle of the road (true) or at the starting edge (false)?
    UPROPERTY(EditAnywhere, Category = "Road Visuals")
    bool bPivotAtCenter = true;

    // Do you want to scale the road width based on the simulator's lane count?
    UPROPERTY(EditAnywhere, Category = "Road Visuals")
    bool bScaleWidthByLanes = true;

    UPROPERTY(EditAnywhere, Category = "Road Visuals")
    double OriginOffsetX = 4003563.0;

    // The raw Y coordinate from your data that should become 0 in Unreal
    UPROPERTY(EditAnywhere, Category = "Road Visuals")
    double OriginOffsetY = 2556901.0;

    // Builds the visual instances from your simulator's network
    void BuildVisualNetwork(Network* RoadNetwork);

    // Helper function to get an Edge ID when clicking on a road instance
    // Returns int64 because Blueprints do not support uint64
    UFUNCTION(BlueprintCallable, Category = "Road Network")
    int64 GetEdgeIdFromHitItem(int32 HitItemIndex);

    //traffic light and stop sign stuff
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Road Network")
    UHierarchicalInstancedStaticMeshComponent* StopSignHISM;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Road Network")
    UHierarchicalInstancedStaticMeshComponent* TrafficLightHISM;
    TMap<uint64_t, TArray<int32>> IntersectionLightInstances;

private:
    // Maps HISM Instance ID (int32) to the simulator's Edge ID (uint64_t)
    TMap<int32, uint64_t> InstanceIndexToEdgeId;

protected:
    int32 CurrentlyHighlightedIndex = INDEX_NONE;
};