#include "RoadNetworkVisualizer.h"
#include "road.h"
#include "node.h"
#include <limits> 

ARoadNetworkVisualizer::ARoadNetworkVisualizer()
{
    PrimaryActorTick.bCanEverTick = false;

    RoadHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("RoadHISM"));
    RootComponent = RoadHISM;

    // Turn off physics simulation, but keep raycasts enabled so you can still click them
    RoadHISM->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    RoadHISM->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
    RoadHISM->SetGenerateOverlapEvents(false);

    // We need 2 floats: Index 0 for Lane Count, Index 1 for X-Scale (for dash length)
    RoadHISM->NumCustomDataFloats = 2;

    // Disable shadows. 115k meshes casting shadows across a massive map will kill any GPU.
    RoadHISM->SetCastShadow(false);
}

void ARoadNetworkVisualizer::BuildVisualNetwork(Network* RoadNetwork)
{
    if (!RoadNetwork) return;

    // Only wipe out instances if we are doing a fresh file import sequence.
    // When editing interactively, we bypass this to let new clicks append safely.
    if (bIsLoadingFromFile)
    {
        RoadHISM->ClearInstances();
        InstanceIndexToEdgeId.Empty();
    }

    const auto& AllNodes = RoadNetwork->getNodes();
    if (AllNodes.empty()) return;

    // === FIXED STATIC CENTRAL FLORIDA ZONE OFFSETS ===
    // Forces a unified, static reference frame matching your editor placement inputs.
    OriginOffsetX = 4003563.0;
    OriginOffsetY = 2556901.0;

    TArray<FTransform> Transforms;
    TArray<uint64_t> TempEdgeIds;
    TArray<float> TempScaleX;
    TArray<float> TempLanes; // Holds the lane counts per instance

    Transforms.Reserve(120000);
    TempEdgeIds.Reserve(120000);
    TempScaleX.Reserve(120000);
    TempLanes.Reserve(120000);

    for (const auto& NodePair : AllNodes)
    {
        const Node& OriginNode = NodePair.second;

        for (const Road& Edge : OriginNode.outgoingEdges)
        {
            Node* DestNode = RoadNetwork->getNode(Edge.getDest());
            if (!DestNode) continue;

            // Force Z to 0.0. Subtracting the static Origin ensures coordinates match 1:1.
            FVector StartLoc((OriginNode.getX() - OriginOffsetX) * 100.0,
                (OriginNode.getY() - OriginOffsetY) * 100.0,
                0.0);

            FVector EndLoc((DestNode->getX() - OriginOffsetX) * 100.0,
                (DestNode->getY() - OriginOffsetY) * 100.0,
                0.0);

            FVector Direction = EndLoc - StartLoc;
            float DistanceCM = Direction.Size();
            FRotator Rotation = Direction.Rotation();

            FVector InstanceLocation = StartLoc;
            if (bPivotAtCenter)
            {
                InstanceLocation = StartLoc + (Direction * 0.5f);
            }

            float ScaleX = DistanceCM / FMath::Max(1.0f, MeshBaseLengthCm);

            // Ensure we always have at least 1 lane to prevent divide-by-zero in the shader
            int32 SafeLanes = FMath::Max(1, Edge.getLanes());

            float ScaleY = 1.0f;
            if (bScaleWidthByLanes)
            {
                float TargetWidthCm = SafeLanes * 350.0f; // Assuming 3.5m per lane
                ScaleY = TargetWidthCm / FMath::Max(1.0f, MeshBaseWidthCm);
            }

            float ScaleZ = 1.0f;

            Transforms.Add(FTransform(Rotation, InstanceLocation, FVector(ScaleX, ScaleY, ScaleZ)));
            TempEdgeIds.Add(Edge.getEdgeId());
            TempScaleX.Add(ScaleX);
            TempLanes.Add(static_cast<float>(SafeLanes)); // Save lane count to push to GPU
        }
    }

    TArray<int32> AddedIndices = RoadHISM->AddInstances(Transforms, true);

    for (int32 i = 0; i < AddedIndices.Num(); i++)
    {
        InstanceIndexToEdgeId.Add(AddedIndices[i], TempEdgeIds[i]);

        // Push the custom data to the GPU
        RoadHISM->SetCustomDataValue(AddedIndices[i], 0, TempLanes[i], false);
        RoadHISM->SetCustomDataValue(AddedIndices[i], 1, TempScaleX[i], false);
    }

    RoadHISM->UpdateBounds();
	RoadHISM->MarkRenderStateDirty(); // Force the HISM to refresh its render state after all custom data updates
}

int64 ARoadNetworkVisualizer::GetEdgeIdFromHitItem(int32 HitItemIndex)
{
    if (InstanceIndexToEdgeId.Contains(HitItemIndex))
    {
        return (int64)InstanceIndexToEdgeId[HitItemIndex];
    }
    return -1; // Edge not found
}