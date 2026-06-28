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

    // traffic light and stop sign stuff
    StopSignHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("StopSignHISM"));
    StopSignHISM->SetupAttachment(RootComponent);
    StopSignHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    StopSignHISM->SetCastShadow(false);
    TrafficLightHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("TrafficLightHISM"));
    TrafficLightHISM->SetupAttachment(RootComponent);
    TrafficLightHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    TrafficLightHISM->SetCastShadow(false);
    TrafficLightHISM->NumCustomDataFloats = 1; // Index 0 will hold the color phase (Red/Yellow/Green)
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
        // Index 0: The number of lanes (used to draw the Y-axis dividers)
        RoadHISM->SetCustomDataValue(AddedIndices[i], 0, TempLanes[i], false);

        // Index 1: The X-scale (used to keep dashed lines a standard length)
        RoadHISM->SetCustomDataValue(AddedIndices[i], 1, TempScaleX[i], false);
    }

    RoadHISM->MarkRenderStateDirty();
    IntersectionLightInstances.Empty();
StopSignHISM->ClearInstances();
TrafficLightHISM->ClearInstances();

for (const auto& NodePair : AllNodes)
{
    const Node& N = NodePair.second;
    
    // Only process nodes that are marked as stops or lights
    if (N.type == Node::FOUR_WAY_STOP || N.type == Node::TRAFFIC_LIGHT)
    {
        // Get the absolute position of the intersection center
        FVector IntersectionCenter((N.getX() - OriginOffsetX) * 100.0, (N.getY() - OriginOffsetY) * 100.0, 0.0);
        TArray<int32> LightIndicesForThisNode;

        for (uint64_t incomingId : N.incomingEdgeNodeIds)
        {
            Node* predNode = RoadNetwork->getNode(incomingId);
            if (!predNode) continue;

            // Find the physical road connecting the previous node to this intersection
            for (const Road& edge : predNode->outgoingEdges)
            {
                if (edge.getDest() == N.getId())
                {
                    FVector IncomingStart((predNode->getX() - OriginOffsetX) * 100.0, (predNode->getY() - OriginOffsetY) * 100.0, 0.0);
                    FVector Direction = IntersectionCenter - IncomingStart;
                    Direction.Normalize();

                    // Calculate the Right vector (Unreal uses X-Forward, Y-Right)
                    FVector RightVector = FVector(Direction.Y, -Direction.X, 0.0f);

                    // Pull back slightly from the center, and move to the right shoulder of the road
                    float PullbackDistance = 600.0f; // 6 meters back
                    float RightOffset = (edge.getLanes() * 350.0f / 2.0f) + 150.0f; // Edge of the road + 1.5m
                    
                    FVector PropLocation = IntersectionCenter - (Direction * PullbackDistance) + (RightVector * RightOffset);
                    FRotator PropRotation = Direction.Rotation(); // Face down the road

                    FTransform PropTransform(PropRotation, PropLocation);

                    if (N.type == Node::FOUR_WAY_STOP)
                    {
                        StopSignHISM->AddInstance(PropTransform);
                    }
                    else if (N.type == Node::TRAFFIC_LIGHT)
                    {
                        int32 newIndex = TrafficLightHISM->AddInstance(PropTransform);
                        LightIndicesForThisNode.Add(newIndex);
                    }
                    break; 
                }
            }
        }
        
        if (LightIndicesForThisNode.Num() > 0)
        {
            IntersectionLightInstances.Add(N.getId(), LightIndicesForThisNode);
        }
    }
}
}

int64 ARoadNetworkVisualizer::GetEdgeIdFromHitItem(int32 HitItemIndex)
{
    if (InstanceIndexToEdgeId.Contains(HitItemIndex))
    {
        return (int64)InstanceIndexToEdgeId[HitItemIndex];
    }
    return -1; // Edge not found
}