#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/SplineComponent.h"

// Your existing C++ classes — adjust include paths to match your project layout
#include "../../../sim/road_network/include/network.h"
#include "./sim/road_network/include/network_builder.h"
#include "./sim/common/road_state/include/node.h"
#include "./sim//common/road_state/include/road.h"

#include "RoadNetworkSpawner.generated.h"

UCLASS()
class ARoadNetworkSpawner : public AActor
{
    GENERATED_BODY()

public:
    ARoadNetworkSpawner();

    // ── Paths (relative to Content/) ──────────────────────────────────
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road Network|Data")
    FString NodesJsonLPath = "RoadData/Old_nodes_motorways_simplified.jsonl";

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road Network|Data")
    FString EdgesJsonLPath = "RoadData/Old_edges_motorways_simplified.jsonl";

    // ── Road actor ────────────────────────────────────────────────────
    // Assign your spline-based road Blueprint here in the Details panel
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road Network|Spawning")
    TSubclassOf<AActor> RoadActorClass;

    // ── Spline curve settings ─────────────────────────────────────────
    // Catmull-Rom tension: 0.5 = standard, lower = tighter curves
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road Network|Spline",
        meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float CatmullRomTension = 0.5;

    // Scale factor: GeographicLib outputs metres, UE5 uses cm by default
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road Network|Spawning")
    float MetresToCm = 100.0;

    // ── Editor actions ────────────────────────────────────────────────
    UFUNCTION(CallInEditor, BlueprintCallable, Category = "Road Network")
    void GenerateRoadNetwork();

    UFUNCTION(CallInEditor, BlueprintCallable, Category = "Road Network")
    void ClearGeneratedRoads();

private:
    // ── Helpers ───────────────────────────────────────────────────────

    // Convert a Node's projected (x, y) to a UE5 world-space FVector
    // GeographicLib LocalCartesian: x = East, y = North  →  UE5: X = forward (North), Y = right (East)
    FVector NodeToUEPosition(Node* InNode) const;

    // Compute a Catmull-Rom tangent at CurrPos given optional predecessor and successor
    FVector ComputeCatmullRomTangent(
        const FVector* PrevPos,
        const FVector& CurrPos,
        const FVector* NextPos,
        float Tension) const;

    // Clear the spline on a spawned road actor and set our two cubic points
    void SetupSpline(
        USplineComponent* Spline,
        const FVector& LocalStart,
        const FVector& LocalEnd,
        const FVector& StartTangent,
        const FVector& EndTangent) const;

    // All actors spawned by the last GenerateRoadNetwork() call
    UPROPERTY()
    TArray<AActor*> SpawnedRoads;
};