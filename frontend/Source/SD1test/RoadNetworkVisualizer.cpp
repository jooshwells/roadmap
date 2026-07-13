#include "RoadNetworkVisualizer.h"
#include "road.h"
#include "node.h"
#include "intersection_geometry.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
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

    JunctionMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("JunctionMesh"));
    JunctionMesh->SetupAttachment(RootComponent);
    JunctionMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    JunctionMesh->SetCastShadow(false);

    // Default the junction material to the project's plain-asphalt asset via a
    // constructor-time hard reference. This is what gets the asset cooked into
    // packaged builds -- a runtime LoadObject on a string path is invisible to
    // the cooker, so without this the material would be missing in releases.
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> JunctionAsphaltFinder(
        TEXT("/Game/M_JunctionAsphalt.M_JunctionAsphalt"));
    if (JunctionAsphaltFinder.Succeeded())
    {
        JunctionMaterial = JunctionAsphaltFinder.Get();
    }
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

    // One mesh section shared by every intersection's pavement polygon.
    TArray<FVector> JunctionVerts;
    TArray<int32> JunctionTris;
    TArray<FVector> JunctionNormals;
    TArray<FVector2D> JunctionUVs;

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

        if (bSetbackAtIntersections)
        {
            // Fill the junction box the road setbacks carve out with a pavement
            // polygon that meets each incident road's end face.
            if (RoadIntersectionUtil::IsIntersectionNode(OriginNode))
            {
                // Just below the road surface (z=0) so short-edge overlap hides
                // under the roads, but above typical floor/ground actors.
                const FVector JunctionCenter(NodeLoc.X, NodeLoc.Y, -0.2f);
                AppendJunctionPolygon(RoadNetwork, OriginNode, JunctionCenter,
                    JunctionVerts, JunctionTris, JunctionNormals, JunctionUVs);
            }
        }
        else
        {
            FVector NodeScale3D(NodeScale, NodeScale, 0.05f);
            NodeTransforms.Add(FTransform(FRotator::ZeroRotator, NodeLoc, NodeScale3D));
        }
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

            // Ensure we always have at least 1 lane to prevent divide-by-zero in the shader
            int32 SafeLanes = FMath::Max(1, Edge.getLanes());

            // --- Real-world centerline (OSM geometry_xy) ------------------------
            // Curved edges render as a chain of straight pieces that follow the
            // OSM shape polyline instead of one chord instance node-to-node. Its
            // endpoints were snapped onto the node coordinates at network build,
            // so the chain stays flush at junctions. Shapeless edges (runtime
            // roads, missing data) fall back to the straight chord.
            TArray<FVector> Pts;
            if (Edge.hasCurveGeometry())
            {
                const std::vector<RoadGeomPoint>& Geom = Edge.getGeometry();
                Pts.Reserve(Geom.size());
                for (const RoadGeomPoint& P : Geom)
                {
                    Pts.Add(FVector((P.x - OriginOffsetX) * 100.0,
                                    (P.y - OriginOffsetY) * 100.0, 0.0));
                }
            }
            else
            {
                Pts.Add(StartLoc);
                Pts.Add(EndLoc);
            }

            // Cumulative arc length (cm) at each centerline vertex.
            TArray<float> Cum;
            Cum.Reserve(Pts.Num());
            Cum.Add(0.0f);
            for (int32 i = 1; i < Pts.Num(); i++)
            {
                Cum.Add(Cum[i - 1] + FVector::Dist2D(Pts[i - 1], Pts[i]));
            }
            const float TotalLenCm = Cum.Last();
            if (TotalLenCm < 1.0f) continue;

            // End tangents of the centerline. Taper detection compares these,
            // not the chord, so a curved edge measures the direction it
            // actually meets each neighbour at.
            const FVector StartTangent = (Pts[1] - Pts[0]).GetSafeNormal();
            const FVector EndTangent = (Pts.Last() - Pts[Pts.Num() - 2]).GetSafeNormal();

            // Position on the centerline at arc distance S, plus its segment index.
            auto PointAtArc = [&Pts, &Cum](float S, int32& OutSeg) -> FVector
            {
                int32 i = 0;
                while (i + 2 < Pts.Num() && Cum[i + 1] <= S) i++;
                OutSeg = i;
                const float SegLen = Cum[i + 1] - Cum[i];
                const float T = (SegLen > 0.0f) ? (S - Cum[i]) / SegLen : 0.0f;
                return FMath::Lerp(Pts[i], Pts[i + 1], T);
            };

            // Pull the drawn span back from intersection centers so crossing
            // roads meet the junction box instead of overlapping through the
            // node. Mid-road nodes get a 0 setback. Setbacks are measured as
            // arc length along the centerline.
            float SetbackStartCm = 0.0f;
            float SetbackEndCm = 0.0f;
            if (bSetbackAtIntersections)
            {
                SetbackStartCm = RoadIntersectionUtil::GetNodeSetbackMeters(
                    RoadNetwork, OriginNode, MedianGapCm / 100.0f) * 100.0f;
                SetbackEndCm = RoadIntersectionUtil::GetNodeSetbackMeters(
                    RoadNetwork, *DestNode, MedianGapCm / 100.0f) * 100.0f;
                RoadIntersectionUtil::ClampSetbacksToLength(TotalLenCm, SetbackStartCm, SetbackEndCm);
            }

            // The trimmed centerline actually drawn.
            TArray<FVector> TPts;
            if (SetbackStartCm > 0.0f || SetbackEndCm > 0.0f)
            {
                const float SpanStart = SetbackStartCm;
                const float SpanEnd = TotalLenCm - SetbackEndCm;
                if (SpanEnd - SpanStart < 1.0f) continue; // edge lies entirely inside the junction

                // Skip near-duplicate vertices (a cut landing on an existing
                // vertex) so no zero-length segment sneaks in.
                auto PushPt = [&TPts](const FVector& P)
                {
                    if (TPts.Num() == 0 || FVector::DistSquared2D(TPts.Last(), P) > 1.0f)
                    {
                        TPts.Add(P);
                    }
                };

                int32 SegA = 0, SegB = 0;
                const FVector PA = PointAtArc(SpanStart, SegA);
                const FVector PB = PointAtArc(SpanEnd, SegB);
                PushPt(PA);
                for (int32 i = SegA + 1; i <= SegB; i++) PushPt(Pts[i]);
                PushPt(PB);
                if (TPts.Num() < 2) continue;
            }
            else
            {
                TPts = Pts;
            }

            // Per-segment frame of the trimmed centerline.
            const int32 NumSegs = TPts.Num() - 1;
            TArray<float> TCum;
            TArray<FVector> SegDir;
            TArray<FVector> SegRight;
            TArray<FRotator> SegRot;
            TCum.Reserve(TPts.Num());
            SegDir.Reserve(NumSegs);
            SegRight.Reserve(NumSegs);
            SegRot.Reserve(NumSegs);
            TCum.Add(0.0f);
            for (int32 i = 0; i < NumSegs; i++)
            {
                const FVector D = TPts[i + 1] - TPts[i];
                TCum.Add(TCum[i] + D.Size2D());
                const FVector DN = D.GetSafeNormal();
                SegDir.Add(DN);
                SegRight.Add(FVector(-DN.Y, DN.X, 0.0));
                SegRot.Add(DN.Rotation());
            }
            const float DrawLenCm = TCum.Last();

            // Turn sharpness (sin of the heading change) at each interior
            // vertex, used to size the miter fills. Ends stay 0.
            TArray<float> JointSin;
            JointSin.Init(0.0f, TPts.Num());
            for (int32 i = 1; i < NumSegs; i++)
            {
                const float Cross = SegDir[i - 1].X * SegDir[i].Y - SegDir[i - 1].Y * SegDir[i].X;
                JointSin[i] = FMath::Min(FMath::Abs(Cross), 1.0f);
            }

            const float FullWidthCm = SafeLanes * 350.0f;

            // --- Lane-drop / lane-gain detection -------------------------------
            // Find the through-road at each end and taper to meet it. We only look
            // at roughly-aligned neighbours (dot > TaperAlignmentDot) and, among
            // those, pick the one whose lane count is CLOSEST to ours -- that is the
            // mainline continuation, not a minor merging ramp / off-ramp. Picking by
            // straightness alone would latch onto an aligned 1-lane ramp and taper
            // the whole road to it. We also ignore jumps larger than TaperMaxLaneDelta
            // (those are junctions, not lane drops) so a 4->1 change stays abrupt.
            int32 DownstreamLanes = SafeLanes; // lanes we taper DOWN to at the end
            int32 UpstreamLanes   = SafeLanes; // lanes we taper UP from at the start
            if (bTaperLaneDrops && bScaleWidthByLanes)
            {
                // Through-continuation out of DestNode (end of this edge).
                {
                    int32 BestDelta = TaperMaxLaneDelta + 1; // must be within cap to count
                    float BestDot   = -1.0f;
                    for (const Road& NextEdge : DestNode->outgoingEdges)
                    {
                        if (NextEdge.getDest() == OriginNode.getId()) continue; // ignore U-turn
                        Node* NextDest = RoadNetwork->getNode(NextEdge.getDest());
                        if (!NextDest) continue;

                        // Leaving direction of the neighbour: its first curve
                        // segment when shaped, node-to-node chord otherwise.
                        FVector NextDir;
                        if (NextEdge.hasCurveGeometry())
                        {
                            const std::vector<RoadGeomPoint>& G = NextEdge.getGeometry();
                            NextDir = FVector(G[1].x - G[0].x, G[1].y - G[0].y, 0.0);
                        }
                        else
                        {
                            NextDir = FVector((NextDest->getX() - DestNode->getX()),
                                              (NextDest->getY() - DestNode->getY()), 0.0);
                        }
                        const float Dot = FVector::DotProduct(EndTangent, NextDir.GetSafeNormal());
                        if (Dot < TaperAlignmentDot) continue;

                        const int32 L     = FMath::Max(1, NextEdge.getLanes());
                        const int32 Delta = FMath::Abs(L - SafeLanes);
                        if (Delta < BestDelta || (Delta == BestDelta && Dot > BestDot))
                        {
                            BestDelta = Delta; BestDot = Dot; DownstreamLanes = L;
                        }
                    }
                    if (BestDelta > TaperMaxLaneDelta) DownstreamLanes = SafeLanes; // junction, not a drop
                }

                // Through-predecessor into OriginNode (start of this edge).
                {
                    int32 BestDelta = TaperMaxLaneDelta + 1;
                    float BestDot   = -1.0f;
                    for (uint64_t PrevNodeId : OriginNode.incomingEdgeNodeIds)
                    {
                        if (PrevNodeId == Edge.getDest()) continue; // ignore U-turn pair
                        Node* PrevNode = RoadNetwork->getNode(PrevNodeId);
                        if (!PrevNode) continue;

                        // Find the predecessor edge feeding this node.
                        const Road* PrevEdge = nullptr;
                        for (const Road& Cand : PrevNode->outgoingEdges)
                        {
                            if (Cand.getDest() == OriginNode.getId())
                            {
                                PrevEdge = &Cand;
                                break;
                            }
                        }
                        if (!PrevEdge) continue;

                        // Arriving direction of the predecessor: its last curve
                        // segment when shaped, node-to-node chord otherwise.
                        FVector PrevDir;
                        if (PrevEdge->hasCurveGeometry())
                        {
                            const std::vector<RoadGeomPoint>& G = PrevEdge->getGeometry();
                            PrevDir = FVector(G[G.size() - 1].x - G[G.size() - 2].x,
                                              G[G.size() - 1].y - G[G.size() - 2].y, 0.0);
                        }
                        else
                        {
                            PrevDir = FVector((OriginNode.getX() - PrevNode->getX()),
                                              (OriginNode.getY() - PrevNode->getY()), 0.0);
                        }
                        const float Dot = FVector::DotProduct(StartTangent, PrevDir.GetSafeNormal());
                        if (Dot < TaperAlignmentDot) continue;

                        const int32 L = FMath::Max(1, PrevEdge->getLanes());
                        const int32 Delta = FMath::Abs(L - SafeLanes);
                        if (Delta < BestDelta || (Delta == BestDelta && Dot > BestDot))
                        {
                            BestDelta = Delta; BestDot = Dot; UpstreamLanes = L;
                        }
                    }
                    if (BestDelta > TaperMaxLaneDelta) UpstreamLanes = SafeLanes; // junction, not a gain
                }
            }

            // Emits the HISM pieces covering arc span [Along, Along+Len] of the
            // trimmed centerline at a constant width: one piece per underlying
            // polyline segment the span crosses. The inner (left) edge is
            // anchored at MedianGapCm, so a narrower slice tapers inward from
            // the right and lines up with the neighbouring edge. Pieces that
            // touch an interior vertex are extended by a miter so the wedge gap
            // on the outside of the turn is filled; a sub-mm Z stagger between
            // consecutive segments keeps those overlaps from z-fighting.
            auto AddSeg = [&](float Along, float Len, float WidthCm, float LaneData)
            {
                const float SpanA = Along;
                const float SpanB = Along + Len;
                for (int32 i = 0; i < NumSegs; i++)
                {
                    const float A = FMath::Max(SpanA, TCum[i]);
                    const float B = FMath::Min(SpanB, TCum[i + 1]);
                    if (B - A < 0.01f) continue;

                    // Full wedge cover needed at a joint is (gap+width)*sin(turn);
                    // the pieces on each side take half apiece.
                    const float Reach = MedianGapCm + WidthCm;
                    const float Ext0 = (A <= TCum[i] + 0.01f)     ? 0.5f * Reach * JointSin[i]     : 0.0f;
                    const float Ext1 = (B >= TCum[i + 1] - 0.01f) ? 0.5f * Reach * JointSin[i + 1] : 0.0f;

                    const float PieceLen = (B - A) + Ext0 + Ext1;
                    const float Local0 = (A - TCum[i]) - Ext0;
                    const float AlongRef = bPivotAtCenter ? (Local0 + PieceLen * 0.5f) : Local0;

                    FVector Loc = TPts[i] + SegDir[i] * AlongRef
                                + SegRight[i] * ((WidthCm * 0.5f) + MedianGapCm);
                    Loc.Z += (i % 3) * 0.03f;

                    const float SX = PieceLen / FMath::Max(1.0f, MeshBaseLengthCm);
                    const float SY = bScaleWidthByLanes ? (WidthCm / FMath::Max(1.0f, MeshBaseWidthCm)) : 1.0f;

                    Transforms.Add(FTransform(SegRot[i], Loc, FVector(SX, SY, 1.0f)));
                    TempEdgeIds.Add(Edge.getEdgeId());
                    TempScaleX.Add(SX);
                    TempLanes.Add(LaneData);
                }
            };

            // Emits a narrowing/widening zone as many thin slices. Slice count is
            // driven by TaperStepLengthCm (constant world-space step) so zooming in
            // stays smooth, capped by TaperSteps to bound instance count.
            auto AddTaperZone = [&](float ZoneStart, float ZoneLen, float WidthA, float WidthB)
            {
                if (ZoneLen <= 1.0f) return;
                const int32 K = FMath::Clamp(
                    FMath::CeilToInt(ZoneLen / FMath::Max(1.0f, TaperStepLengthCm)),
                    1, FMath::Max(1, TaperSteps));
                const float StepLen = ZoneLen / K;
                for (int32 k = 0; k < K; k++)
                {
                    const float aMid    = (k + 0.5f) / K;
                    const float WidthCm = FMath::Lerp(WidthA, WidthB, aMid);
                    // Pass effective lane count so shader divider spacing stays ~constant.
                    AddSeg(ZoneStart + k * StepLen, StepLen, WidthCm, WidthCm / 350.0f);
                }
            };

            // Taper lengths at each end, only where a neighbour has fewer lanes.
            float StartTaper = (UpstreamLanes   < SafeLanes) ? TaperLengthCm : 0.0f;
            float EndTaper   = (DownstreamLanes < SafeLanes) ? TaperLengthCm : 0.0f;

            // If both zones are present, shrink them proportionally so they fit.
            const float TotalTaper = StartTaper + EndTaper;
            if (TotalTaper > DrawLenCm && TotalTaper > 0.0f)
            {
                const float Scale = DrawLenCm / TotalTaper;
                StartTaper *= Scale;
                EndTaper   *= Scale;
            }

            if (StartTaper <= 0.0f && EndTaper <= 0.0f)
            {
                // No lane change: full width along the whole centerline.
                AddSeg(0.0f, DrawLenCm, FullWidthCm, static_cast<float>(SafeLanes));
            }
            else
            {
                const float UpWidthCm   = UpstreamLanes   * 350.0f;
                const float DownWidthCm = DownstreamLanes * 350.0f;

                // Widening taper at the start (a lane opens up).
                AddTaperZone(0.0f, StartTaper, UpWidthCm, FullWidthCm);

                // Full-width body between the two taper zones.
                const float BodyLen = DrawLenCm - StartTaper - EndTaper;
                if (BodyLen > 1.0f)
                {
                    AddSeg(StartTaper, BodyLen, FullWidthCm, static_cast<float>(SafeLanes));
                }

                // Narrowing taper at the end (a lane drops).
                AddTaperZone(DrawLenCm - EndTaper, EndTaper, FullWidthCm, DownWidthCm);
            }
        }
    }

    NodeHISM->ClearInstances();
    if (NodeTransforms.Num() > 0)
    {
        NodeHISM->AddInstances(NodeTransforms, false);
    }

    JunctionMesh->ClearAllMeshSections();
    if (JunctionTris.Num() > 0)
    {
        JunctionMesh->CreateMeshSection(0, JunctionVerts, JunctionTris, JunctionNormals, JunctionUVs,
            TArray<FColor>(), TArray<FProcMeshTangent>(), false);

        if (JunctionMaterial)
        {
            JunctionMesh->SetMaterial(0, JunctionMaterial);
        }
        else if (!JunctionMesh->GetMaterial(0))
        {
            // No material assigned: prefer the project's junction asphalt
            // (samples the same /Game/Asphalt texture as the roads, minus the
            // lane-divider logic, which needs per-instance custom data that
            // procedural meshes don't have). Fall back to a dark grey tint of
            // the engine's basic shape material if the asset is missing.
            if (UMaterialInterface* ProjectAsphalt = LoadObject<UMaterialInterface>(nullptr,
                TEXT("/Game/M_JunctionAsphalt.M_JunctionAsphalt")))
            {
                JunctionMesh->SetMaterial(0, ProjectAsphalt);
            }
            else if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr,
                TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")))
            {
                UMaterialInstanceDynamic* Asphalt = UMaterialInstanceDynamic::Create(Base, this);
                Asphalt->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.015f, 0.015f, 0.017f));
                JunctionMesh->SetMaterial(0, Asphalt);
            }
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
}

// Position + unit tangent at 'ArcM' meters along an edge's raw shape polyline
// (map coordinates). Unlike Road::samplePointAt this measures true polyline
// arc length -- the same measure the visualizer trims setbacks with -- so a
// junction face lands exactly on the trimmed road end.
static bool SampleGeometryAtArcMeters(const std::vector<RoadGeomPoint>& G, double ArcM,
    double& OutX, double& OutY, double& OutTanX, double& OutTanY)
{
    if (G.size() < 2) return false;
    const double Total = G.back().s;
    if (Total <= 0.0) return false;

    const double S = FMath::Clamp(ArcM, 0.0, Total);
    size_t i = 0;
    while (i + 2 < G.size() && G[i + 1].s <= S) i++;

    const double SegLen = G[i + 1].s - G[i].s;
    const double T = (SegLen > 0.0) ? (S - G[i].s) / SegLen : 0.0;
    OutX = G[i].x + (G[i + 1].x - G[i].x) * T;
    OutY = G[i].y + (G[i + 1].y - G[i].y) * T;
    OutTanX = (G[i + 1].x - G[i].x) / SegLen;
    OutTanY = (G[i + 1].y - G[i].y) / SegLen;
    return true;
}

void ARoadNetworkVisualizer::AppendJunctionPolygon(Network* RoadNetwork, const Node& JunctionNode, const FVector& CenterLoc,
    TArray<FVector>& Verts, TArray<int32>& Tris, TArray<FVector>& Normals, TArray<FVector2D>& UVs) const
{
    // Lane counts toward each distinct neighbour: X = outgoing, Y = incoming
    // (0 = that direction doesn't exist, i.e. a one-way approach).
    TMap<uint64_t, FIntPoint> NeighborLanes;
    for (const Road& E : JunctionNode.outgoingEdges)
    {
        FIntPoint& P = NeighborLanes.FindOrAdd(E.getDest(), FIntPoint::ZeroValue);
        P.X = FMath::Max(P.X, FMath::Max(1, E.getLanes()));
    }
    for (uint64_t InId : JunctionNode.incomingEdgeNodeIds)
    {
        Node* Prev = RoadNetwork->getNode(InId);
        if (!Prev) continue;
        for (const Road& E : Prev->outgoingEdges)
        {
            if (E.getDest() == JunctionNode.getId())
            {
                FIntPoint& P = NeighborLanes.FindOrAdd(InId, FIntPoint::ZeroValue);
                P.Y = FMath::Max(P.Y, FMath::Max(1, E.getLanes()));
                break;
            }
        }
    }

    const float SetbackCm = RoadIntersectionUtil::GetNodeSetbackMeters(
        RoadNetwork, JunctionNode, MedianGapCm / 100.0f) * 100.0f;

    // One approach = the combined two-way corridor toward one neighbour. Its end
    // face spans the outgoing pavement on the right of the outward direction and
    // the incoming pavement on the left; a missing direction falls back to just
    // the median gap so the median strip is still covered. The two face corners
    // are collected as independent ring points and sorted by their own angle
    // around the center, which always yields a simple (non-self-intersecting)
    // star-shaped ring even when a wide road meets a narrow one at a shallow angle.
    struct FRingPoint { float Angle; FVector Pos; };
    TArray<FRingPoint> RingPts;
    RingPts.Reserve(NeighborLanes.Num() * 2);

    auto AddCorner = [&](const FVector& P)
    {
        FRingPoint R;
        R.Angle = FMath::Atan2(P.Y - CenterLoc.Y, P.X - CenterLoc.X);
        R.Pos = P;
        RingPts.Add(R);
    };

    for (const auto& Pair : NeighborLanes)
    {
        Node* Neighbor = RoadNetwork->getNode(Pair.Key);
        if (!Neighbor) continue;

        // Where the approach toward this neighbour actually leaves the
        // junction. Curved edges are trimmed by arc length, so sample the
        // shape polyline at the setback distance for the true face position
        // and direction; shapeless edges use the straight node-to-node ray.
        FVector Dir(Neighbor->getX() - JunctionNode.getX(), Neighbor->getY() - JunctionNode.getY(), 0.0);
        FVector Base = FVector::ZeroVector;
        bool bSampled = false;

        double SX = 0.0, SY = 0.0, TX = 0.0, TY = 0.0;
        const Road* OutEdge = nullptr;
        for (const Road& E : JunctionNode.outgoingEdges)
        {
            if (E.getDest() == Pair.Key && E.hasCurveGeometry()) { OutEdge = &E; break; }
        }
        if (OutEdge && SampleGeometryAtArcMeters(OutEdge->getGeometry(), SetbackCm / 100.0, SX, SY, TX, TY))
        {
            Dir = FVector(TX, TY, 0.0);
            bSampled = true;
        }
        else
        {
            // One-way toward the junction: sample the incoming edge instead,
            // measuring the setback back from its far end.
            for (const Road& E : Neighbor->outgoingEdges)
            {
                if (E.getDest() != JunctionNode.getId() || !E.hasCurveGeometry()) continue;
                if (SampleGeometryAtArcMeters(E.getGeometry(), E.getGeometry().back().s - SetbackCm / 100.0, SX, SY, TX, TY))
                {
                    Dir = FVector(-TX, -TY, 0.0); // outward from the junction
                    bSampled = true;
                }
                break;
            }
        }

        if (!Dir.Normalize()) continue;
        if (bSampled)
        {
            Base = FVector((SX - OriginOffsetX) * 100.0, (SY - OriginOffsetY) * 100.0, CenterLoc.Z);
        }
        else
        {
            Base = CenterLoc + Dir * SetbackCm;
        }

        const FVector Right(-Dir.Y, Dir.X, 0.0);

        const float ExtentRight = MedianGapCm + Pair.Value.X * 350.0f;
        const float ExtentLeft  = MedianGapCm + Pair.Value.Y * 350.0f;

        AddCorner(Base + Right * ExtentRight);
        AddCorner(Base - Right * ExtentLeft);
    }

    if (RingPts.Num() < 3) return;

    RingPts.Sort([](const FRingPoint& A, const FRingPoint& B) { return A.Angle < B.Angle; });

    const float UVScale = 0.001f; // 1 UV unit per 10 m, for world-ish tiling
    auto AddVert = [&](const FVector& P) -> int32
    {
        Normals.Add(FVector::UpVector);
        UVs.Add(FVector2D(P.X, P.Y) * UVScale);
        return Verts.Add(P);
    };

    const int32 CenterIdx = AddVert(CenterLoc);
    TArray<int32> Ring;
    Ring.Reserve(RingPts.Num());
    for (const FRingPoint& R : RingPts)
    {
        Ring.Add(AddVert(R.Pos));
    }

    const int32 N = Ring.Num();
    for (int32 i = 0; i < N; i++)
    {
        // Ring is counter-clockwise (ascending angle); emitting each fan
        // triangle reversed makes it clockwise viewed from +Z, which is the
        // front face in Unreal, so the pavement points up.
        Tris.Add(CenterIdx);
        Tris.Add(Ring[(i + 1) % N]);
        Tris.Add(Ring[i]);
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