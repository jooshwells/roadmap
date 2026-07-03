#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "network.h"
#include "RoadNetworkVisualizer.generated.h"

class UProceduralMeshComponent;
class UMaterialInterface;

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

    // --- Intersection setbacks ------------------------------------------------
    // Stop road geometry at the edge of the junction box instead of running
    // every edge through the node center. The junction itself is then filled by
    // a procedural pavement polygon (JunctionMesh) whose sides meet each road's
    // end face, so mid-road geometry nodes no longer get a cap plopped on top
    // of the road and intersections read as asphalt instead of circles.
    UPROPERTY(EditAnywhere, Category = "Road Visuals|Intersections")
    bool bSetbackAtIntersections = true;

    // One procedural mesh holding every intersection's pavement polygon.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Road Network")
    UProceduralMeshComponent* JunctionMesh;

    // Material for the junction pavement (plain asphalt -- no lane markings).
    // Left unset, a dark-grey default is generated to roughly match roads.
    UPROPERTY(EditAnywhere, Category = "Road Visuals|Intersections")
    UMaterialInterface* JunctionMaterial = nullptr;

    // --- Lane-drop / lane-gain tapering --------------------------------------
    // Taper an edge to meet a narrower neighbour (a lane dropping at the end or
    // opening at the start) instead of an abrupt width cut. Only applies when
    // bScaleWidthByLanes is true.
    UPROPERTY(EditAnywhere, Category = "Road Visuals|Taper")
    bool bTaperLaneDrops = true;

    // Length (cm) of the taper zone over which the width ramps up/down.
    UPROPERTY(EditAnywhere, Category = "Road Visuals|Taper")
    float TaperLengthCm = 3000.0f; // 30 m

    // World-space length (cm) of each taper slice. Smaller = smoother when zoomed
    // in (more instances). The slice count per zone is TaperLength / this value.
    UPROPERTY(EditAnywhere, Category = "Road Visuals|Taper")
    float TaperStepLengthCm = 100.0f; // 1 m slices

    // Hard cap on slices per taper zone, to bound instance count on long tapers.
    UPROPERTY(EditAnywhere, Category = "Road Visuals|Taper")
    int32 TaperSteps = 32;

    // Minimum direction alignment (dot product) for a neighbour edge to count as
    // the through-continuation used for taper detection. 1 = perfectly straight.
    UPROPERTY(EditAnywhere, Category = "Road Visuals|Taper")
    float TaperAlignmentDot = 0.7f;

    // Largest lane-count difference that is treated as a taper. Real lane drops/
    // gains change by 1 (rarely 2) lanes; a bigger jump is a junction, not a taper,
    // and is left abrupt. Prevents e.g. a 4-lane road tapering to a 1-lane ramp.
    UPROPERTY(EditAnywhere, Category = "Road Visuals|Taper")
    int32 TaperMaxLaneDelta = 2;

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
    // Appends one intersection's pavement polygon (a fan around the node whose
    // outer edge meets each incident road's end face) to the junction mesh arrays.
    void AppendJunctionPolygon(Network* RoadNetwork, const Node& JunctionNode, const FVector& CenterLoc,
        TArray<FVector>& Verts, TArray<int32>& Tris, TArray<FVector>& Normals, TArray<FVector2D>& UVs) const;

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