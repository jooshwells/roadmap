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
class UStaticMesh;

// Everything the road-edit UI needs about one directed edge. Lanes, speed and
// turn lanes are read-write: edit them and pass the struct back through
// AMapPlayerController::ApplyRoadEdit.
USTRUCT(BlueprintType)
struct FRoadEdgeInfo
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Road Edit") int64 EdgeId = -1;
    UPROPERTY(BlueprintReadOnly, Category = "Road Edit") int64 NodeU = -1;
    UPROPERTY(BlueprintReadOnly, Category = "Road Edit") int64 NodeV = -1;
    UPROPERTY(BlueprintReadOnly, Category = "Road Edit") float LengthMeters = 0.0f;
    UPROPERTY(BlueprintReadOnly, Category = "Road Edit") bool bTwoWay = false;

    UPROPERTY(BlueprintReadWrite, Category = "Road Edit") int32 Lanes = 1;
    UPROPERTY(BlueprintReadWrite, Category = "Road Edit") float SpeedLimitMps = 20.0f;
    UPROPERTY(BlueprintReadWrite, Category = "Road Edit") FString TurnLanes;

    // True when TurnLanes did not come from an explicit tag in the edge data
    // but from the network's inference over the movements available at the
    // destination node (see Network::assignInferredTurnLanes). Lets the UI
    // label the value as a suggestion rather than surveyed OSM data.
    UPROPERTY(BlueprintReadOnly, Category = "Road Edit") bool bTurnLanesInferred = false;

    // OSM vertical layer: 0 ground, +1 overpass, -1 underpass. Editing it
    // re-runs the elevation pass, so a ground road becomes a bridge in place.
    UPROPERTY(BlueprintReadWrite, Category = "Road Edit") int32 Layer = 0;
};

/** Configuration for an instanced foliage type scattered on the terrain */
USTRUCT(BlueprintType)
struct FFoliageTypeConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foliage Config")
    UHierarchicalInstancedStaticMeshComponent* InstancedMeshComponent = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foliage Config")
    bool bAlignToNormal = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foliage Config")
    float ScaleMin = 0.8f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foliage Config")
    float ScaleMax = 1.3f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foliage Config")
    float MaxSlopeAngle = 45.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foliage Config")
    float RoadClearanceBuffer = 300.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foliage Config")
    bool bUseGridCoverage = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foliage Config")
    float GridSpacingCm = 600.0f;
};

UCLASS()
class ROADMAP_API ARoadNetworkVisualizer : public AActor
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

    // --- Environment Components -----------------------------------------------
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Environment")
    UProceduralMeshComponent* GroundMesh;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Environment")
    UProceduralMeshComponent* WaterMesh;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Environment")
    UHierarchicalInstancedStaticMeshComponent* TreeHISM;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Environment")
    UHierarchicalInstancedStaticMeshComponent* GrassHISM;

    UPROPERTY(EditAnywhere, Category = "Environment")
    UMaterialInterface* GroundMaterial = nullptr;

    UPROPERTY(EditAnywhere, Category = "Environment")
    UMaterialInterface* WaterMaterial = nullptr;

    UPROPERTY(EditAnywhere, Category = "Environment")
    int32 NumLakes = 2;

    UPROPERTY(EditAnywhere, Category = "Environment")
    float LakeMinRadius = 1500.0f;

    UPROPERTY(EditAnywhere, Category = "Environment")
    float LakeMaxRadius = 3500.0f;

    // Extra buffer beyond a lake's own radius kept clear of roads.
    UPROPERTY(EditAnywhere, Category = "Environment")
    float LakeRoadClearance = 500.0f;

    // Returns one FVector4 per placed lake: XY = center, Z = radius (W unused).
    TArray<FVector4> GenerateLakes(const FBox& Bounds);

    // Add near your other protected foliage methods:
    void GenerateGroundMesh(const FBox& Bounds);

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
    UPROPERTY(EditAnywhere, Category = "Road Visuals|Intersections")
    bool bSetbackAtIntersections = true;

    // One procedural mesh holding every intersection's pavement polygon.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Road Network")
    UProceduralMeshComponent* JunctionMesh;

    // Material for the junction pavement (plain asphalt -- no lane markings).
    UPROPERTY(EditAnywhere, Category = "Road Visuals|Intersections")
    UMaterialInterface* JunctionMaterial = nullptr;

    // --- Lane-drop / lane-gain tapering --------------------------------------
    UPROPERTY(EditAnywhere, Category = "Road Visuals|Taper")
    bool bTaperLaneDrops = true;

    // Length (cm) of the taper zone over which the width ramps up/down.
    // Must match RoadIntersectionUtil::TaperLenMeters -- the vehicle renderer
    // uses that constant to keep cars on the tapered pavement.
    UPROPERTY(EditAnywhere, Category = "Road Visuals|Taper")
    float TaperLengthCm = 3000.0f; // 30 m

    UPROPERTY(EditAnywhere, Category = "Road Visuals|Taper")
    float TaperStepLengthCm = 100.0f; // 1 m slices

    UPROPERTY(EditAnywhere, Category = "Road Visuals|Taper")
    int32 TaperSteps = 32;

    UPROPERTY(EditAnywhere, Category = "Road Visuals|Taper")
    float TaperAlignmentDot = 0.7f;

    // Largest lane-count difference that is treated as a taper. Real lane drops/
    // gains change by a single lane; a bigger jump is a junction, not a taper,
    // and is left abrupt. Prevents e.g. a 4-lane road tapering to a 1-lane ramp.
    // Keep in sync with RoadIntersectionUtil::TaperMaxLaneDeltaDefault so the
    // drawn pavement and the vehicle clamp taper by the same amount.
    UPROPERTY(EditAnywhere, Category = "Road Visuals|Taper")
    int32 TaperMaxLaneDelta = 1;

    // --- Elevated road dressing ----------------------------------------------
    UPROPERTY(EditAnywhere, Category = "Road Visuals|Elevated")
    bool bElevatedRoadDecor = true;

    UPROPERTY(EditAnywhere, Category = "Road Visuals|Elevated")
    float DeckThicknessCm = 60.0f;

    UPROPERTY(EditAnywhere, Category = "Road Visuals|Elevated")
    float DeckMinHeightCm = 80.0f;

    UPROPERTY(EditAnywhere, Category = "Road Visuals|Elevated")
    float PillarSpacingCm = 2500.0f;

    UPROPERTY(EditAnywhere, Category = "Road Visuals|Elevated")
    float PillarMinHeightCm = 300.0f;

    UPROPERTY(EditAnywhere, Category = "Road Visuals|Elevated")
    float PillarClearanceCm = 150.0f;

    UPROPERTY(EditAnywhere, Category = "Road Visuals|Elevated")
    UMaterialInterface* ElevatedConcreteMaterial = nullptr;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Road Network")
    UHierarchicalInstancedStaticMeshComponent* DeckHISM;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Road Network")
    UHierarchicalInstancedStaticMeshComponent* PillarHISM;

    // Builds the visual instances from your simulator's network
    void BuildVisualNetwork(Network* RoadNetwork, FString InNodesPath, FString InEdgesPath);

    // Rebuilds every road/junction visual from the cached network.
    void RefreshRoadVisuals();

    // Reserves a fresh node id above everything in the network and the files.
    int64 AllocateNodeId() { return static_cast<int64>(++CurrentMaxNodeId); }

    int64 ExportNewRoadSegment(int64 StartNodeId, int64 EndNodeId, FVector EndNodeUnrealLoc, int32 Lanes, float SpeedLimit, FString TurnLanes, int32 Layer = 0);

    bool FindClosestEdge(FVector SearchLocation, float SnapRadiusCM, FVector& OutPointOnEdge, int64& OutU, int64& OutV);

    bool FindFirstCrossing(const FVector& SegStart, const FVector& SegEnd, const TArray<int64>& IgnoreNodes,
        FVector& OutPoint, int64& OutU, int64& OutV, int64& OutExistingNodeId);

    bool SplitEdgeForNewNode(int64 U, int64 V, int64 NewNodeId, FVector SplitUnrealLoc);

    bool GetNodeLocation(int64 NodeId, FVector& OutLocation) const;

    UFUNCTION(BlueprintCallable, Category = "Road Network")
    bool GetEdgeInfo(int64 EdgeId, FRoadEdgeInfo& OutInfo);

    bool UpdateRoadProperties(int64 U, int64 V, int32 Lanes, float SpeedMps, const FString& TurnLanes, int32 Layer, bool bBothDirections);

    bool DeleteRoad(int64 U, int64 V, bool bBothDirections);

    UFUNCTION(BlueprintCallable, Category = "Road Network")
    int64 GetEdgeIdFromHitItem(int32 HitItemIndex);

    UFUNCTION(BlueprintCallable, Category = "Road Network")
    FString GetRoadNameFromHitItem(int32 HitItemIndex);

    UFUNCTION(BlueprintCallable, Category = "Road Network")
    FString GetRoadNameFromEdgeId(int64 EdgeId);

    FVector2D ConvertUnrealToJSONCoords(FVector UnrealLocation);

    UFUNCTION(BlueprintCallable, Category = "Road Network")
    bool FindClosestNode(FVector SearchLocation, float SnapRadiusCM, FVector& OutNodeLocation, int64& OutNodeId);

    // Nearest node worth inspecting: junction nodes (3+ roadways) and
    // controlled nodes win over plain shape/pass-through nodes inside the
    // radius, because a big junction's pavement spans many meters and the
    // geometrically nearest node to a click on it is often a curve point on
    // an approach road. Falls back to the plain nearest node when nothing
    // junction-like is in range.
    UFUNCTION(BlueprintCallable, Category = "Road Network")
    bool FindClosestInspectableNode(FVector SearchLocation, float SnapRadiusCM, FVector& OutNodeLocation, int64& OutNodeId);

    // Controls the meshes and spacing used for grass and trees.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foliage")
    UStaticMesh* GrassClumpMesh = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foliage")
    float FoliageDrawDistance = 20000.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foliage")
    float FoliageSpacing = 300.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foliage")
    float RoadClearanceDistance = 400.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foliage")
    float TreeScaleMultiplier = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foliage")
    float GrassScaleMultiplier = 1.0f;

protected:
    int32 CurrentlyHighlightedIndex = INDEX_NONE;

    // Clears and rebuilds the grass and tree instances.
    UFUNCTION(BlueprintCallable, Category = "Road Network|Foliage")
    void ScatterFoliage();

    // Removes the current grass and tree instances.
    UFUNCTION(BlueprintCallable, Category = "Road Network|Foliage")
    void ClearFoliage();

    // Checks whether a location is too close to a road for foliage.
    UFUNCTION(BlueprintCallable, Category = "Road Network|Foliage")
    bool IsLocationOnRoad(const FVector& Location, float Buffer) const;

    // Returns the area used when placing the ground and foliage.
    FBox GetNetworkOrTerrainBounds() const;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road Network|Foliage")
    TArray<FFoliageTypeConfig> FoliageTypes;

    // Number of random positions checked when placing foliage.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road Network|Foliage")
    int32 TotalScatterAttempts = 1000;

    // Keeps the random foliage layout the same between rebuilds.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road Network|Foliage")
    int32 FoliageSeed = 1337;

private:
    void AppendJunctionPolygon(Network* RoadNetwork, const Node& JunctionNode, const FVector& CenterLoc,
        TArray<FVector>& Verts, TArray<int32>& Tris, TArray<FVector>& Normals, TArray<FVector2D>& UVs) const;

    void AppendNodeRecord(int64 NodeId, FVector UnrealLoc);

    bool SplitEdgeInFile(int64 U, int64 V, int64 NewNodeId, FVector2D SplitJsonCoords);

    // One pending JSONL edge rewrite. Lanes / speed / layer are shared by
    // both directions of an edit; turn:lanes is per-direction (the reverse
    // edge carries the mirrored string).
    struct FEdgeFileUpdate
    {
        int64 U = 0;
        int64 V = 0;
        FString TurnLanes;
    };

    // Rewrites the lines for all Updates' lanes / speed_mps / turn:lanes /
    // layer in ONE file load + ONE save (a two-way edit used to rewrite the
    // whole file twice). The layer is always written explicitly so it
    // overrides any legacy bridge/tunnel tag fallback when a road is
    // grounded again. Returns true if every requested edge was found.
    bool UpdateEdgesInFile(const TArray<FEdgeFileUpdate>& Updates, int32 Lanes, float SpeedMps, int32 Layer);

    bool RemoveEdgeInFile(int64 U, int64 V);

    bool RemoveNodeInFile(int64 NodeId);

    TSharedPtr<FJsonObject> FindEdgeJson(int64 U, int64 V) const;

    void GetEdgePolylineUnreal(const Node& FromNode, const Road& Edge, const Node& DestNode, TArray<FVector>& OutPts) const;

    Network* CachedNetwork = nullptr;

    bool bOriginLocked = false;

    // Road-instance count of the previous RefreshRoadVisuals build. Sizes the
    // next build's scratch reserves near-exactly (edits shift the count only
    // slightly), so rebuilds neither spike a fixed worst-case allocation nor
    // pay repeated growth.
    int32 LastRoadInstanceCount = 0;

    // Maps HISM Instance ID (int32) to the simulator's Edge ID (uint64_t)
    TMap<int32, uint64_t> InstanceIndexToEdgeId;
    // Maps Edge ID back to its road name string
    TMap<uint64_t, FString> EdgeIdToName;
    // Maps Edge ID back to its (origin, dest) node pair for property edits.
    TMap<uint64_t, TPair<uint64_t, uint64_t>> EdgeIdToNodes;
    TMap<uint64_t, FVector> CachedNodeLocations;
    uint64_t CurrentMaxNodeId = 0;
    uint64_t CurrentMaxEdgeId = 0;

    FString NodesFilePath;
    FString EdgesFilePath;
};
