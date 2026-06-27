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

    NodeHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("NodeHISM"));
    NodeHISM->SetupAttachment(RootComponent);
    NodeHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    NodeHISM->SetCastShadow(false);
}

void ARoadNetworkVisualizer::BuildVisualNetwork(Network* RoadNetwork)
{
    if (!RoadNetwork) return;

    RoadHISM->ClearInstances();
    InstanceIndexToEdgeId.Empty();

    const auto& AllNodes = RoadNetwork->getNodes();
    if (AllNodes.empty()) return;

    // Calculate the center of the road network
    double MinX = std::numeric_limits<double>::max();
    double MinY = std::numeric_limits<double>::max();
    double MaxX = std::numeric_limits<double>::lowest();
    double MaxY = std::numeric_limits<double>::lowest();

    for (const auto& NodePair : AllNodes)
    {
        const Node& N = NodePair.second;
        if (N.getX() < MinX) MinX = N.getX();
        if (N.getX() > MaxX) MaxX = N.getX();
        if (N.getY() < MinY) MinY = N.getY();
        if (N.getY() > MaxY) MaxY = N.getY();
    }

    // Set the offset to the exact center of the bounding box
    OriginOffsetX = (MinX + MaxX) / 2.0;
    OriginOffsetY = (MinY + MaxY) / 2.0;

    TArray<FTransform> NodeTransforms;
    NodeTransforms.Reserve(AllNodes.size());

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

        FVector NodeLoc((OriginNode.getX() - OriginOffsetX) * 100.0,
            (OriginNode.getY() - OriginOffsetY) * 100.0,
            -2.0f);
        FVector NodeScale3D(NodeScale, NodeScale, 0.05f);
        NodeTransforms.Add(FTransform(FRotator::ZeroRotator, NodeLoc, NodeScale3D));

        for (const Road& Edge : OriginNode.outgoingEdges)
        {
            Node* DestNode = RoadNetwork->getNode(Edge.getDest());
            if (!DestNode) continue;

            // Force Z to 0.0. Subtracting Origin forces the geographic center to 0,0.
            FVector StartLoc((OriginNode.getX() - OriginOffsetX) * 100.0,
                (OriginNode.getY() - OriginOffsetY) * 100.0,
                0.0);

            FVector EndLoc((DestNode->getX() - OriginOffsetX) * 100.0,
                (DestNode->getY() - OriginOffsetY) * 100.0,
                0.0);

            FVector Direction = EndLoc - StartLoc;
            float DistanceCM = Direction.Size();
            FRotator Rotation = Direction.Rotation();
            // Ensure we always have at least 1 lane to prevent divide-by-zero in the shader
            int32 SafeLanes = FMath::Max(1, Edge.getLanes());
            FVector InstanceLocation = StartLoc;
            if (bPivotAtCenter)
            {
                InstanceLocation = StartLoc + (Direction * 0.5f);
            }

            // ---> FIX: Correct Unreal Engine Right Vector <---
            FVector RightVec(-Direction.Y, Direction.X, 0.0);
            RightVec.Normalize();
            float TargetWidthCm = SafeLanes * 350.0f;

            // Move the road mesh so it perfectly aligns with the right-side traffic
            InstanceLocation += RightVec * ((TargetWidthCm * 0.5f) + MedianGapCm);

            float ScaleX = DistanceCM / FMath::Max(1.0f, MeshBaseLengthCm);

            

            float ScaleY = 1.0f;
            if (bScaleWidthByLanes)
            {
                ScaleY = TargetWidthCm / FMath::Max(1.0f, MeshBaseWidthCm);
            }

            float ScaleZ = 1.0f;

            Transforms.Add(FTransform(Rotation, InstanceLocation, FVector(ScaleX, ScaleY, ScaleZ)));
            TempEdgeIds.Add(Edge.getEdgeId());
            TempScaleX.Add(ScaleX);
            TempLanes.Add(static_cast<float>(SafeLanes)); // Save lane count to push to GPU
        }
    }

    NodeHISM->ClearInstances();
    if (NodeTransforms.Num() > 0)
    {
        NodeHISM->AddInstances(NodeTransforms, false);
    }

    TArray<int32> AddedIndices = RoadHISM->AddInstances(Transforms, true);

    for (int32 i = 0; i < AddedIndices.Num(); i++)
    {
        InstanceIndexToEdgeId.Add(AddedIndices[i], TempEdgeIds[i]);

        // Push the custom data to the GPU
        // Index 0: The number of lanes (used to draw the Y-axis dividers)
        RoadHISM->SetCustomDataValue(AddedIndices[i], 0, TempLanes[i], false);

        // Index 1: The X-scale (used to keep dashed lines a standard length)
        RoadHISM->SetCustomDataValue(AddedIndices[i], 1, TempScaleX[i], false);
    }

    RoadHISM->MarkRenderStateDirty();
}

int64 ARoadNetworkVisualizer::GetEdgeIdFromHitItem(int32 HitItemIndex)
{
    if (InstanceIndexToEdgeId.Contains(HitItemIndex))
    {
        return (int64)InstanceIndexToEdgeId[HitItemIndex];
    }
    return -1; // Edge not found
}