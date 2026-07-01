#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "network.h" 
#include "RoadNetworkVisualizer.generated.h"

UCLASS()
class SD1TEST_API ARoadNetworkVisualizer : public AActor
{
    GENERATED_BODY()

public:

    ARoadNetworkVisualizer();

    // HISM for rendering Intersections/Nodes
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Road Network")
    UHierarchicalInstancedStaticMeshComponent* NodeHISM;

    // How big the intersection caps should be
    UPROPERTY(EditAnywhere, Category = "Road Visuals")
    float NodeScale = 15.0f;

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

    // The empty space in centimeters between opposing lanes of traffic
    UPROPERTY(EditAnywhere, Category = "Road Visuals")
    float MedianGapCm = 100.0f; // 1 meter gap

    // Builds the visual instances from your simulator's network
    void BuildVisualNetwork(Network* RoadNetwork, FString InNodesPath, FString InEdgesPath);

    int64 ExportNewRoadSegment(int64 StartNodeId, int64 EndNodeId, FVector EndNodeUnrealLoc, int32 Lanes);

    // Helper function to get an Edge ID when clicking on a road instance
    // Returns int64 because Blueprints do not support uint64
    UFUNCTION(BlueprintCallable, Category = "Road Network")
    int64 GetEdgeIdFromHitItem(int32 HitItemIndex);

    void AddSingleRoadVisually(FVector StartUnrealLoc, FVector EndUnrealLoc, int32 Lanes);

    FVector2D ConvertUnrealToJSONCoords(FVector UnrealLocation);

    // Snaps a clicked location to the nearest node if within the radius
    UFUNCTION(BlueprintCallable, Category = "Road Network")
    bool FindClosestNode(FVector SearchLocation, float SnapRadiusCM, FVector& OutNodeLocation, int64& OutNodeId);

private:
    // Maps HISM Instance ID (int32) to the simulator's Edge ID (uint64_t)
    TMap<int32, uint64_t> InstanceIndexToEdgeId;
    TMap<uint64_t, FVector> CachedNodeLocations;
    uint64_t CurrentMaxNodeId = 0;
    uint64_t CurrentMaxEdgeId = 0;

    FString NodesFilePath;
    FString EdgesFilePath;
protected:
    int32 CurrentlyHighlightedIndex = INDEX_NONE;
};