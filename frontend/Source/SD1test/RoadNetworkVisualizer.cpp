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

void ARoadNetworkVisualizer::BuildVisualNetwork(Network* RoadNetwork, FString InNodesPath, FString InEdgesPath)
{
    if (!RoadNetwork) return;

    // 1. Store the paths for later exporting
    NodesFilePath = InNodesPath;
    EdgesFilePath = InEdgesPath;

    // Reset max IDs
    CurrentMaxNodeId = 0;
    CurrentMaxEdgeId = 0;

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
        CachedNodeLocations.Add(OriginNode.getId(), NodeLoc);

        if (OriginNode.getId() > CurrentMaxNodeId) CurrentMaxNodeId = OriginNode.getId();

        for (const Road& Edge : OriginNode.outgoingEdges)
        {
            if (Edge.getEdgeId() > CurrentMaxEdgeId) CurrentMaxEdgeId = Edge.getEdgeId();

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

void ARoadNetworkVisualizer::AddSingleRoadVisually(FVector StartUnrealLoc, FVector EndUnrealLoc, int32 Lanes)
{
    FVector Direction = EndUnrealLoc - StartUnrealLoc;
    float DistanceCM = Direction.Size();
    FRotator Rotation = Direction.Rotation();

    int32 SafeLanes = FMath::Max(1, Lanes);
    FVector InstanceLocation = StartUnrealLoc;

    if (bPivotAtCenter)
    {
        InstanceLocation = StartUnrealLoc + (Direction * 0.5f);
    }

    // Reuse your exact Right Vector math for right-side traffic alignment
    FVector RightVec(-Direction.Y, Direction.X, 0.0);
    RightVec.Normalize();
    float TargetWidthCm = SafeLanes * 350.0f;
    InstanceLocation += RightVec * ((TargetWidthCm * 0.5f) + MedianGapCm);

    float ScaleX = DistanceCM / FMath::Max(1.0f, MeshBaseLengthCm);
    float ScaleY = bScaleWidthByLanes ? (TargetWidthCm / FMath::Max(1.0f, MeshBaseWidthCm)) : 1.0f;

    FTransform NewTransform(Rotation, InstanceLocation, FVector(ScaleX, ScaleY, 1.0f));

    // Add the instance dynamically
    int32 NewIndex = RoadHISM->AddInstance(NewTransform, true);

    // Push Custom Data to GPU (Index 0: Lanes, Index 1: ScaleX)
    RoadHISM->SetCustomDataValue(NewIndex, 0, static_cast<float>(SafeLanes), false);
    RoadHISM->SetCustomDataValue(NewIndex, 1, ScaleX, false);

    // Add Node visual at the end point
    FTransform NodeTransform(FRotator::ZeroRotator, EndUnrealLoc, FVector(NodeScale, NodeScale, 0.05f));
    NodeHISM->AddInstance(NodeTransform, true);

    RoadHISM->MarkRenderStateDirty();
}

FVector2D ARoadNetworkVisualizer::ConvertUnrealToJSONCoords(FVector UnrealLocation)
{
    // 1. Convert Unreal units (cm) back to Map units (meters) and add the origin offset back.
    // This puts the coordinates back into your backend's memory space.
    double BackendX = (UnrealLocation.X / 100.0) + OriginOffsetX;
    double BackendY = (UnrealLocation.Y / 100.0) + OriginOffsetY;

    // 2. Undo the sign swap (-j["y"]) to match the raw JSON schema
    double JsonX = BackendX;
    double JsonY = -BackendY;

    return FVector2D(JsonX, JsonY);
}

bool ARoadNetworkVisualizer::FindClosestNode(FVector SearchLocation, float SnapRadiusCM, FVector& OutNodeLocation, int64& OutNodeId)
{
    float ClosestDistSq = SnapRadiusCM * SnapRadiusCM;
    bool bFound = false;

    // Iterate through cached nodes to find the closest one within the radius
    for (const auto& Pair : CachedNodeLocations)
    {
        // Calculate squared distance (much faster than true distance because it avoids square roots)
        float DistSq = FVector::DistSquaredXY(SearchLocation, Pair.Value);

        if (DistSq < ClosestDistSq)
        {
            ClosestDistSq = DistSq;
            OutNodeLocation = Pair.Value; // Snap perfectly to the center of the node
            OutNodeLocation.Z = 0.0f;          // Keep everything perfectly flat on the Z plane
            OutNodeId = Pair.Key;
            bFound = true;
        }
    }

    return bFound;
}

int64 ARoadNetworkVisualizer::ExportNewRoadSegment(int64 StartNodeId, int64 EndNodeId, FVector EndNodeUnrealLoc, int32 Lanes)
{
    // 1. Handle Node Generation (If the user clicked in empty space)
    int64 FinalEndNodeId = EndNodeId;
    FVector2D EndJsonCoords = ConvertUnrealToJSONCoords(EndNodeUnrealLoc);

    if (FinalEndNodeId == -1)
    {
        CurrentMaxNodeId++;
        FinalEndNodeId = CurrentMaxNodeId;

        // Build Node JSON Object
        TSharedPtr<FJsonObject> NodeObj = MakeShareable(new FJsonObject);
        NodeObj->SetNumberField(TEXT("id"), FinalEndNodeId);
        NodeObj->SetNumberField(TEXT("lon"), 0.0); // Or reverse Mercator projection if needed
        NodeObj->SetNumberField(TEXT("lat"), 0.0);
        NodeObj->SetNumberField(TEXT("x"), EndJsonCoords.X);
        NodeObj->SetNumberField(TEXT("y"), EndJsonCoords.Y);
        // traffic_control is null

        FString NodeString;
        TSharedRef<TJsonWriter<>> NodeWriter = TJsonWriterFactory<>::Create(&NodeString, 0);
        FJsonSerializer::Serialize(NodeObj.ToSharedRef(), NodeWriter);

        NodeString.ReplaceInline(TEXT("\n"), TEXT(""));
        NodeString.ReplaceInline(TEXT("\r"), TEXT(""));
        NodeString += TEXT("\n"); // Make it JSONL compliant

        // Append to Nodes file
        FFileHelper::SaveStringToFile(NodeString, *NodesFilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), EFileWrite::FILEWRITE_Append);

        // Cache it so the user can immediately snap to this newly created node!
        CachedNodeLocations.Add(FinalEndNodeId, EndNodeUnrealLoc);
    }

    // 2. Handle Edge Generation
    CurrentMaxEdgeId++;
    FVector StartNodeUnrealLoc = CachedNodeLocations[StartNodeId]; // Retrieve from cache
    FVector2D StartJsonCoords = ConvertUnrealToJSONCoords(StartNodeUnrealLoc);

    // Calculate length in meters
    double LengthMeters = FVector::Distance(StartNodeUnrealLoc, EndNodeUnrealLoc) / 100.0;

    TSharedPtr<FJsonObject> EdgeObj = MakeShareable(new FJsonObject);
    // You might want to assign an ID to the edge JSON if your schema requires it, but based on your example, u and v are the primary keys
    EdgeObj->SetNumberField(TEXT("u"), StartNodeId);
    EdgeObj->SetNumberField(TEXT("v"), FinalEndNodeId);
    EdgeObj->SetNumberField(TEXT("length_m"), LengthMeters);
    EdgeObj->SetNumberField(TEXT("speed_mps"), 15.646); // Default or passed from UI
    EdgeObj->SetNumberField(TEXT("lanes"), Lanes);
    EdgeObj->SetBoolField(TEXT("oneway"), true);
    EdgeObj->SetStringField(TEXT("highway"), TEXT("residential")); // Default type

    // Create geometry_xy array representing the straight line
    TArray<TSharedPtr<FJsonValue>> GeometryArray;

    TSharedPtr<FJsonObject> GeomStart = MakeShareable(new FJsonObject);
    GeomStart->SetNumberField(TEXT("x"), StartJsonCoords.X);
    GeomStart->SetNumberField(TEXT("y"), StartJsonCoords.Y);
    GeometryArray.Add(MakeShareable(new FJsonValueObject(GeomStart)));

    TSharedPtr<FJsonObject> GeomEnd = MakeShareable(new FJsonObject);
    GeomEnd->SetNumberField(TEXT("x"), EndJsonCoords.X);
    GeomEnd->SetNumberField(TEXT("y"), EndJsonCoords.Y);
    GeometryArray.Add(MakeShareable(new FJsonValueObject(GeomEnd)));

    EdgeObj->SetArrayField(TEXT("geometry_xy"), GeometryArray);

    FString EdgeString;
    TSharedRef<TJsonWriter<>> EdgeWriter = TJsonWriterFactory<>::Create(&EdgeString, 0);
    FJsonSerializer::Serialize(EdgeObj.ToSharedRef(), EdgeWriter);

    EdgeString.ReplaceInline(TEXT("\n"), TEXT(""));
    EdgeString.ReplaceInline(TEXT("\r"), TEXT(""));
    EdgeString += TEXT("\n");

    // Append to Edges file
    FFileHelper::SaveStringToFile(EdgeString, *EdgesFilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), EFileWrite::FILEWRITE_Append);

    return FinalEndNodeId;
}