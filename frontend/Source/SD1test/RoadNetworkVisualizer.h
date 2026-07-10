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

    // OSM vertical layer: 0 ground, +1 overpass, -1 underpass. Editing it
    // re-runs the elevation pass, so a ground road becomes a bridge in place.
    UPROPERTY(BlueprintReadWrite, Category = "Road Edit") int32 Layer = 0;
};

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

    // --- Elevated road dressing ----------------------------------------------
    // Elevated spans are flat HISM ribbons; without extra geometry they read as
    // floating paper strips. These add a concrete deck slab under every raised
    // road piece and support pillars down to the ground at regular intervals,
    // plus sides and piers for elevated junction pavements.

    // Master switch for deck slabs, pillars, and junction sides.
    UPROPERTY(EditAnywhere, Category = "Road Visuals|Elevated")
    bool bElevatedRoadDecor = true;

    // Vertical thickness (cm) of the deck slab under an elevated road piece
    // (also the depth of an elevated junction's side skirt).
    UPROPERTY(EditAnywhere, Category = "Road Visuals|Elevated")
    float DeckThicknessCm = 60.0f;

    // A road piece only gets a deck slab once its surface is at least this
    // high (cm), so ramp bottoms fade into the ground instead of clipping it.
    UPROPERTY(EditAnywhere, Category = "Road Visuals|Elevated")
    float DeckMinHeightCm = 80.0f;

    // Arc-length spacing (cm) between support pillars along an elevated span.
    UPROPERTY(EditAnywhere, Category = "Road Visuals|Elevated")
    float PillarSpacingCm = 2500.0f;

    // Minimum clear height (cm) under the deck for a pillar to be placed.
    UPROPERTY(EditAnywhere, Category = "Road Visuals|Elevated")
    float PillarMinHeightCm = 300.0f;

    // Minimum horizontal daylight (cm) between a pier and the pavement edge of
    // any road passing below it. A pier that would land closer than this slides
    // along its span to a clear spot, or is dropped if none exists nearby.
    UPROPERTY(EditAnywhere, Category = "Road Visuals|Elevated")
    float PillarClearanceCm = 150.0f;

    // Material for deck slabs and pillars. Left unset, a concrete-grey tint of
    // the engine's basic shape material is generated at first build.
    UPROPERTY(EditAnywhere, Category = "Road Visuals|Elevated")
    UMaterialInterface* ElevatedConcreteMaterial = nullptr;

    // Deck slabs (engine cube) under elevated road pieces.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Road Network")
    UHierarchicalInstancedStaticMeshComponent* DeckHISM;

    // Support pillars (engine cylinder) under elevated spans and junctions.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Road Network")
    UHierarchicalInstancedStaticMeshComponent* PillarHISM;

    // Builds the visual instances from your simulator's network
    void BuildVisualNetwork(Network* RoadNetwork, FString InNodesPath, FString InEdgesPath);

    // Rebuilds every road/junction visual from the cached network. Call after
    // runtime edits so new roads get the full geometry treatment (setbacks,
    // junction pavement, tapering) instead of a bare rectangle. The world
    // origin stays locked to the first build so nothing shifts.
    void RefreshRoadVisuals();

    // Reserves a fresh node id above everything in the network and the files.
    int64 AllocateNodeId() { return static_cast<int64>(++CurrentMaxNodeId); }

    // Exports the new segment to the JSONL files AND adds it to the cached
    // visual network (so RefreshRoadVisuals shows it). Returns the end node
    // id, allocating a new node when EndNodeId is -1. Layer is the OSM
    // vertical layer (0 ground, +1 overpass, ...); the elevation pass in the
    // next rebuild turns it into an actual bridge/underpass profile.
    int64 ExportNewRoadSegment(int64 StartNodeId, int64 EndNodeId, FVector EndNodeUnrealLoc, int32 Lanes, float SpeedLimit, FString TurnLanes, int32 Layer = 0);

    // Snaps a clicked location to the nearest point on an edge centerline
    // within the radius. The returned point is pulled away from the edge's
    // endpoints so a split there never degenerates. U/V are the edge's nodes.
    bool FindClosestEdge(FVector SearchLocation, float SnapRadiusCM, FVector& OutPointOnEdge, int64& OutU, int64& OutV);

    // First place the segment A->B crosses an existing edge, measured from A.
    // Edges touching a node in IgnoreNodes are skipped (the pieces already
    // chained at A, and the destination). When the crossing lands close to an
    // existing node, OutExistingNodeId reports it (weld there instead of
    // splitting) and OutU/OutV are the crossed edge's nodes otherwise.
    bool FindFirstCrossing(const FVector& SegStart, const FVector& SegEnd, const TArray<int64>& IgnoreNodes,
        FVector& OutPoint, int64& OutU, int64& OutV, int64& OutExistingNodeId);

    // Splits edge U->V (and V->U when present) at the new node in the cached
    // network AND in the edges JSONL file, and appends the node record. Call
    // SimulationManager::SplitBackendEdge separately for the live sim.
    bool SplitEdgeForNewNode(int64 U, int64 V, int64 NewNodeId, FVector SplitUnrealLoc);

    // Cached Unreal-space location of a node, if known.
    bool GetNodeLocation(int64 NodeId, FVector& OutLocation) const;

    // Fills the road-edit UI struct for a clicked edge (see GetEdgeIdFromHitItem).
    UFUNCTION(BlueprintCallable, Category = "Road Network")
    bool GetEdgeInfo(int64 EdgeId, FRoadEdgeInfo& OutInfo);

    // Applies new lane count / speed / turn lanes / vertical layer to edge
    // U->V (and V->U when bBothDirections) in the visual network and the
    // edges JSONL, then rebuilds the visuals (which re-runs the elevation
    // pass, so layer changes take effect immediately). Push the same change
    // to the live sim through SimulationManager::UpdateBackendRoad.
    bool UpdateRoadProperties(int64 U, int64 V, int32 Lanes, float SpeedMps, const FString& TurnLanes, int32 Layer, bool bBothDirections);

    // Deletes edge U->V (and V->U when bBothDirections) from the visual
    // network and the edges JSONL, then rebuilds the visuals. Endpoint nodes
    // left with no edges at all are removed from the network, the location
    // cache, and the nodes JSONL, so no orphan intersections linger. Push the
    // same deletion to the live sim through SimulationManager::
    // DeleteBackendRoad.
    bool DeleteRoad(int64 U, int64 V, bool bBothDirections);

    // Helper function to get an Edge ID when clicking on a road instance
    // Returns int64 because Blueprints do not support uint64
    UFUNCTION(BlueprintCallable, Category = "Road Network")
    int64 GetEdgeIdFromHitItem(int32 HitItemIndex);

    FVector2D ConvertUnrealToJSONCoords(FVector UnrealLocation);

    // Snaps a clicked location to the nearest node if within the radius
    UFUNCTION(BlueprintCallable, Category = "Road Network")
    bool FindClosestNode(FVector SearchLocation, float SnapRadiusCM, FVector& OutNodeLocation, int64& OutNodeId);

private:
    // Appends one intersection's pavement polygon (a fan around the node whose
    // outer edge meets each incident road's end face) to the junction mesh arrays.
    void AppendJunctionPolygon(Network* RoadNetwork, const Node& JunctionNode, const FVector& CenterLoc,
        TArray<FVector>& Verts, TArray<int32>& Tris, TArray<FVector>& Normals, TArray<FVector2D>& UVs) const;

    // Appends one node record to the nodes JSONL file and caches its location.
    void AppendNodeRecord(int64 NodeId, FVector UnrealLoc);

    // Rewrites the edges JSONL: the line for U->V becomes two lines meeting at
    // NewNodeId, with length_m split proportionally and geometry_xy divided at
    // the split point. All other fields (highway, turn:lanes, ...) are kept.
    bool SplitEdgeInFile(int64 U, int64 V, int64 NewNodeId, FVector2D SplitJsonCoords);

    // Rewrites the U->V line's lanes / speed_mps / turn:lanes / layer in
    // place. The layer is always written explicitly so it overrides any
    // legacy bridge/tunnel tag fallback when a road is grounded again.
    bool UpdateEdgeInFile(int64 U, int64 V, int32 Lanes, float SpeedMps, const FString& TurnLanes, int32 Layer);

    // Drops the U->V line from the edges JSONL.
    bool RemoveEdgeInFile(int64 U, int64 V);

    // Drops the node's line from the nodes JSONL.
    bool RemoveNodeInFile(int64 NodeId);

    // Parses the JSONL line for edge U->V, if present.
    TSharedPtr<FJsonObject> FindEdgeJson(int64 U, int64 V) const;

    // The edge's centerline in Unreal coordinates (shape polyline or chord).
    void GetEdgePolylineUnreal(const Node& FromNode, const Road& Edge, const Node& DestNode, TArray<FVector>& OutPts) const;

    // The network the visuals are built from (SimulationManager's visual
    // network, NOT the live sim's). Runtime edits mutate it so rebuilds and
    // edge queries see them. Node*/Road* into it are never stored.
    Network* CachedNetwork = nullptr;

    // The world origin is computed from the first build's bounds and then
    // locked, so runtime rebuilds never shift existing geometry.
    bool bOriginLocked = false;

    // Maps HISM Instance ID (int32) to the simulator's Edge ID (uint64_t)
    TMap<int32, uint64_t> InstanceIndexToEdgeId;
    // Maps Edge ID back to its (origin, dest) node pair for property edits.
    TMap<uint64_t, TPair<uint64_t, uint64_t>> EdgeIdToNodes;
    TMap<uint64_t, FVector> CachedNodeLocations;
    uint64_t CurrentMaxNodeId = 0;
    uint64_t CurrentMaxEdgeId = 0;

    FString NodesFilePath;
    FString EdgesFilePath;
protected:
    int32 CurrentlyHighlightedIndex = INDEX_NONE;
};