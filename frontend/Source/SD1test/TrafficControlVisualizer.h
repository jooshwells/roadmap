#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include <vector>
#include "TrafficControlVisualizer.generated.h"

class Network;
class UProceduralMeshComponent;
class UMaterialInterface;
struct TrafficLightRenderState;

// Renders the physical traffic-control hardware at controlled intersection
// nodes: signal fixtures (pole + head + red/yellow/green lamps) at
// TRAFFIC_LIGHT nodes, and stop signs (post + octagon face) at FOUR_WAY_STOP
// nodes and on the minor approaches of YIELD_STOP nodes.
//
// Everything is built from engine basic shapes and one procedural mesh, so no
// project content is required. Lamp colors track the simulation's light phases
// via UpdateLightStates: only the lamp matching the current signal color is
// shown (the other two are zero-scaled), which reads clearly from the sim's
// usual camera distances.
UCLASS()
class SD1TEST_API ATrafficControlVisualizer : public AActor
{
    GENERATED_BODY()

public:
    ATrafficControlVisualizer();

    // Poles and sign posts (engine cylinder, per-instance scaled).
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic Controls")
    UHierarchicalInstancedStaticMeshComponent* PoleHISM;

    // Signal head housings (engine cube).
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic Controls")
    UHierarchicalInstancedStaticMeshComponent* SignalHeadHISM;

    // One HISM per lamp color so each can carry its own material. A fixture
    // owns one instance in each; visibility is toggled by zero-scaling.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic Controls")
    UHierarchicalInstancedStaticMeshComponent* LampRedHISM;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic Controls")
    UHierarchicalInstancedStaticMeshComponent* LampYellowHISM;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic Controls")
    UHierarchicalInstancedStaticMeshComponent* LampGreenHISM;

    // All stop-sign faces in one mesh: section 0 = red octagons, section 1 =
    // white inset octagons (same trick as the road visualizer's JunctionMesh).
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic Controls")
    UProceduralMeshComponent* SignFaceMesh;

    // Optional material overrides. Left unset, colored dynamic instances of
    // the engine's BasicShapeMaterial are used, so packaged builds work
    // without any project assets.
    UPROPERTY(EditAnywhere, Category = "Traffic Controls|Materials")
    UMaterialInterface* PoleMaterial = nullptr;

    UPROPERTY(EditAnywhere, Category = "Traffic Controls|Materials")
    UMaterialInterface* HeadMaterial = nullptr;

    UPROPERTY(EditAnywhere, Category = "Traffic Controls|Materials")
    UMaterialInterface* LampRedMaterial = nullptr;

    UPROPERTY(EditAnywhere, Category = "Traffic Controls|Materials")
    UMaterialInterface* LampYellowMaterial = nullptr;

    UPROPERTY(EditAnywhere, Category = "Traffic Controls|Materials")
    UMaterialInterface* LampGreenMaterial = nullptr;

    UPROPERTY(EditAnywhere, Category = "Traffic Controls|Materials")
    UMaterialInterface* SignRedMaterial = nullptr;

    UPROPERTY(EditAnywhere, Category = "Traffic Controls|Materials")
    UMaterialInterface* SignWhiteMaterial = nullptr;

    // --- Signal fixture dimensions (cm) ---------------------------------
    UPROPERTY(EditAnywhere, Category = "Traffic Controls|Dimensions")
    float PoleHeightCm = 550.0f;

    UPROPERTY(EditAnywhere, Category = "Traffic Controls|Dimensions")
    float PoleDiameterCm = 18.0f;

    UPROPERTY(EditAnywhere, Category = "Traffic Controls|Dimensions")
    float HeadWidthCm = 45.0f;

    UPROPERTY(EditAnywhere, Category = "Traffic Controls|Dimensions")
    float HeadDepthCm = 40.0f;

    UPROPERTY(EditAnywhere, Category = "Traffic Controls|Dimensions")
    float HeadHeightCm = 135.0f;

    UPROPERTY(EditAnywhere, Category = "Traffic Controls|Dimensions")
    float LampDiameterCm = 34.0f;

    UPROPERTY(EditAnywhere, Category = "Traffic Controls|Dimensions")
    float LampSpacingCm = 42.0f;

    // --- Stop sign dimensions (cm) ---------------------------------------
    UPROPERTY(EditAnywhere, Category = "Traffic Controls|Dimensions")
    float SignPostHeightCm = 260.0f;

    UPROPERTY(EditAnywhere, Category = "Traffic Controls|Dimensions")
    float SignPostDiameterCm = 8.0f;

    UPROPERTY(EditAnywhere, Category = "Traffic Controls|Dimensions")
    float SignFaceRadiusCm = 38.0f;

    // Gap between the outer lane edge and the fixture, in meters.
    UPROPERTY(EditAnywhere, Category = "Traffic Controls|Dimensions")
    float RoadsideMarginMeters = 1.2f;

    // Places every fixture from the network's node types. Origin offsets are
    // the same map-center offsets the road visualizer computed, so both
    // actors land in the same Unreal space.
    void BuildTrafficControls(Network* RoadNetwork, double InOriginOffsetX, double InOriginOffsetY);

    // Applies the sim's current signal colors. Cheap when nothing changed.
    void UpdateLightStates(const std::vector<TrafficLightRenderState>& LightStates);

private:
    // One signal fixture (one approach of one light node).
    struct FSignalLampSet
    {
        uint64 ApproachOriginId = 0;
        int32 LampIndex[3] = { INDEX_NONE, INDEX_NONE, INDEX_NONE }; // red, yellow, green
        FTransform LampTransform[3];                                 // full-size transforms
    };

    struct FSignalNodeVisual
    {
        TArray<FSignalLampSet> Approaches;
        uint8 LastColor[2] = { 255, 255 }; // per axis; 255 forces first update
    };

    TMap<uint64, FSignalNodeVisual> SignalVisuals;

    // Engine base material the colored dynamic instances derive from; a hard
    // reference so it survives cooking.
    UPROPERTY()
    UMaterialInterface* BasicShapeMaterialBase = nullptr;

    double OriginOffsetX = 0.0;
    double OriginOffsetY = 0.0;

    UHierarchicalInstancedStaticMeshComponent* LampHISM(int32 Color) const;

    void AddSignalFixture(uint64 NodeId, uint64 ApproachOriginId,
        const FVector& BaseLoc, const FVector& Forward);

    void AddStopSign(const FVector& BaseLoc, const FVector& Forward,
        TArray<FVector>& RedVerts, TArray<int32>& RedTris, TArray<FVector>& RedNormals, TArray<FVector2D>& RedUVs,
        TArray<FVector>& WhiteVerts, TArray<int32>& WhiteTris, TArray<FVector>& WhiteNormals, TArray<FVector2D>& WhiteUVs) const;

    // Appends a double-sided regular polygon facing +/-Forward.
    static void AppendPolygonFace(const FVector& Center, const FVector& Forward, float RadiusCm, int32 NumSides,
        float FirstVertexAngleDeg, TArray<FVector>& Verts, TArray<int32>& Tris,
        TArray<FVector>& Normals, TArray<FVector2D>& UVs);

    void ApplyDefaultMaterials();
};
