#include "RoadNetworkVisualizer.h"
#include "road.h"
#include "node.h"
#include "IntersectionGeometry.h"
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

    GroundMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("GroundMesh"));
    WaterMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("WaterMesh"));
    TreeHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("TreeHISM"));
    GrassHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("GrassHISM"));

    GroundMesh->SetupAttachment(RootComponent);
    WaterMesh->SetupAttachment(RootComponent);
    TreeHISM->SetupAttachment(RootComponent);
    GrassHISM->SetupAttachment(RootComponent);

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
            23.0f);

        if (bSetbackAtIntersections)
        {
            // Fill the junction box the road setbacks carve out with a pavement
            // polygon that meets each incident road's end face.
            if (RoadIntersectionUtil::IsIntersectionNode(OriginNode))
            {
                // Just below the road surface (z=0) so short-edge overlap hides
                // under the roads, but above typical floor/ground actors.
                const FVector JunctionCenter(NodeLoc.X, NodeLoc.Y, 24.0f); 
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
                25.0);

            FVector EndLoc((DestNode->getX() - OriginOffsetX) * 100.0,
                (DestNode->getY() - OriginOffsetY) * 100.0,
                25.0);

            // Ensure we always have at least 1 lane to prevent divide-by-zero in the shader
            int32 SafeLanes = FMath::Max(1, Edge.getLanes());

            EdgeIdToName.Add(Edge.getEdgeId(), FString(Edge.getName().c_str()));

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
                                    (P.y - OriginOffsetY) * 100.0, 25.0));
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

            const float UpstreamWidth = UpstreamLanes * 350.0f;
            const float DownstreamWidth = DownstreamLanes * 350.0f;

            float CurrentAlong = 0.0f;
            float RemainingLen = DrawLenCm;

            // Start taper zone
            if (UpstreamLanes != SafeLanes && RemainingLen > TaperLengthCm)
            {
                AddTaperZone(CurrentAlong, TaperLengthCm, UpstreamWidth, FullWidthCm);
                CurrentAlong += TaperLengthCm;
                RemainingLen -= TaperLengthCm;
            }

            // Determine if there is space for an end taper zone
            float MidZoneLen = RemainingLen;
            bool bHasEndTaper = (DownstreamLanes != SafeLanes && RemainingLen > TaperLengthCm);
            if (bHasEndTaper)
            {
                MidZoneLen -= TaperLengthCm;
            }

            // Middle constant zone
            if (MidZoneLen > 0.01f)
            {
                AddSeg(CurrentAlong, MidZoneLen, FullWidthCm, (float)SafeLanes);
                CurrentAlong += MidZoneLen;
            }

            // End taper zone
            if (bHasEndTaper)
            {
                AddTaperZone(CurrentAlong, TaperLengthCm, FullWidthCm, DownstreamWidth);
            }
        }
    }

    // Node rendering fallback (if setbacks are disabled)
    if (!bSetbackAtIntersections && NodeHISM && NodeTransforms.Num() > 0)
    {
        NodeHISM->AddInstances(NodeTransforms, false);
    }

    // Batch add the accumulated road transforms to the HISM
    for (int32 i = 0; i < Transforms.Num(); i++)
    {
        int32 NewIndex = RoadHISM->AddInstance(Transforms[i]);
        InstanceIndexToEdgeId.Add(NewIndex, TempEdgeIds[i]);
        RoadHISM->SetCustomDataValue(NewIndex, 0, TempLanes[i], false);
        RoadHISM->SetCustomDataValue(NewIndex, 1, TempScaleX[i], false);
    }
    RoadHISM->MarkRenderStateDirty();

    // Create Junction procedural mesh if setback is enabled
    if (bSetbackAtIntersections && JunctionMesh && JunctionVerts.Num() > 0)
    {
        TArray<FProcMeshTangent> Tangents;
        JunctionMesh->CreateMeshSection(0, JunctionVerts, JunctionTris, JunctionNormals, JunctionUVs, TArray<FColor>(), Tangents, false);
        if (JunctionMaterial)
        {
            JunctionMesh->SetMaterial(0, JunctionMaterial);
        }
    }

    // --- TRIGGER ENVIRONMENT GENERATION ---
    // Generate Ground, Lakes, and Foliage based on the finalized network
    GenerateEnvironment(RoadNetwork);
}

// Position + unit tangent at 'ArcM' meters along an edge's raw shape polyline
// (map coordinates). Unlike Road::samplePointAt this measures true polyline
// arc length -- the same measure the visualizer trims setbacks with -- so a
// junction face lands exactly on the trimmed road end.

#pragma optimize("", off)

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

    const double DeltaX = G[i + 1].x - G[i].x;
    const double DeltaY = G[i + 1].y - G[i].y;

    OutX = G[i].x + DeltaX * T;
    OutY = G[i].y + DeltaY * T;

    OutTanX = DeltaX / SegLen;
    OutTanY = DeltaY / SegLen;

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
        const uint64_t DestId = E.getDest();
        const int32 LaneCount = E.getLanes();
        const int32 ClampedLanes = FMath::Max(1, LaneCount);

        FIntPoint& P = NeighborLanes.FindOrAdd(DestId, FIntPoint::ZeroValue);
        const int32 NewX = FMath::Max(P.X, ClampedLanes);
        P.X = NewX;
    }
    for (uint64_t InId : JunctionNode.incomingEdgeNodeIds)
    {
        Node* Prev = RoadNetwork->getNode(InId);
        if (!Prev) continue;

        for (const Road& E : Prev->outgoingEdges)
        {
            if (E.getDest() == JunctionNode.getId())
            {
                const int32 LaneCount = E.getLanes();
                const int32 ClampedLanes = FMath::Max(1, LaneCount);

                FIntPoint& P = NeighborLanes.FindOrAdd(InId, FIntPoint::ZeroValue);
                const int32 NewY = FMath::Max(P.Y, ClampedLanes);
                P.Y = NewY;
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

#pragma optimize("", on)

int64 ARoadNetworkVisualizer::GetEdgeIdFromHitItem(int32 HitItemIndex)
{
    if (InstanceIndexToEdgeId.Contains(HitItemIndex))
    {
        return (int64)InstanceIndexToEdgeId[HitItemIndex];
    }
    return -1; // Edge not found
}

FString ARoadNetworkVisualizer::GetRoadNameFromHitItem(int32 HitItemIndex)
{
    if (InstanceIndexToEdgeId.Contains(HitItemIndex))
    {
        uint64_t EdgeId = InstanceIndexToEdgeId[HitItemIndex];
        if (const FString* Name = EdgeIdToName.Find(EdgeId))
        {
            return Name->IsEmpty() ? TEXT("Unnamed Road") : *Name;
        }
    }
    return TEXT("Unknown Road");
}

void ARoadNetworkVisualizer::AddSingleRoadVisually(FVector StartUnrealLoc, FVector EndUnrealLoc, int32 Lanes)
{

    StartUnrealLoc.Z = 25.0f;
    EndUnrealLoc.Z = 25.0f;

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

    // Update the Node visual at the end point to sit on the Node plane
    FVector VisualNodeLoc = EndUnrealLoc;
    VisualNodeLoc.Z = 23.0f;

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
            OutNodeLocation.Z = 25.0f;          // Keep everything perfectly flat on the Z plane
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

// ============================================================================
// Master entry point
// ============================================================================
void ARoadNetworkVisualizer::GenerateEnvironment(Network* RoadNetwork)
{
    if (!RoadNetwork) return;

    // Calculate map bounds with padding
    FBox2D Bounds = ComputeRoadNetworkBounds(RoadNetwork);
    if (!Bounds.bIsValid) return;

    // Build the green terrain ground plane
    GenerateGroundMesh(Bounds);

    // Carve out lakes (now aware of road placement)
    TArray<FVector4> LakeFootprints = GenerateLakes(RoadNetwork, Bounds);

    // Scatter foliage (trees automatically avoid both roads and LakeFootprints)
    ScatterFoliage(RoadNetwork, Bounds, LakeFootprints);
}

FBox2D ARoadNetworkVisualizer::ComputeRoadNetworkBounds(Network* RoadNetwork) const
{
    FBox2D Bounds(ForceInit);
    if (!RoadNetwork) return Bounds;

    for (const auto& NodePair : RoadNetwork->getNodes())
    {
        const Node& N = NodePair.second;
        FVector2D UnrealLoc((N.getX() - OriginOffsetX) * 100.0, (N.getY() - OriginOffsetY) * 100.0);
        Bounds += UnrealLoc;
    }

    // Apply environment padding
    Bounds.Min -= FVector2D(EnvironmentPadding, EnvironmentPadding);
    Bounds.Max += FVector2D(EnvironmentPadding, EnvironmentPadding);

    return Bounds;
}

void ARoadNetworkVisualizer::GenerateGroundMesh(const FBox2D& Bounds)
{
    BuildFlatMeshSection(GroundMesh, Bounds, 0.0f, 1);
    if (GroundMesh && GroundMaterial)
    {
        GroundMesh->SetMaterial(0, GroundMaterial);
    }
}

TArray<FVector4> ARoadNetworkVisualizer::GenerateLakes(Network* RoadNetwork, const FBox2D& Bounds)
{
    TArray<FVector4> Footprints;
    if (!WaterMesh || NumLakes <= 0) return Footprints;

    WaterMesh->ClearAllMeshSections();

    const int32 MaxAttemptsPerLake = 150;
    const float UVScale = 0.001f;

    for (int32 LakeIdx = 0; LakeIdx < NumLakes; ++LakeIdx)
    {
        bool bPlaced = false;

        for (int32 Attempt = 0; Attempt < MaxAttemptsPerLake; ++Attempt)
        {
            float CandidateRadius = FMath::FRandRange(LakeMinRadius, LakeMaxRadius);

            float CenterX = FMath::FRandRange(Bounds.Min.X + CandidateRadius, Bounds.Max.X - CandidateRadius);
            float CenterY = FMath::FRandRange(Bounds.Min.Y + CandidateRadius, Bounds.Max.Y - CandidateRadius);
            FVector2D CandidateCenter(CenterX, CenterY);

            // 1. Check distance to roads (Candidate Radius + Clearance Buffer)
            if (IsNearAnyRoad(RoadNetwork, CandidateCenter, CandidateRadius + RoadClearanceDistance))
            {
                continue;
            }

            // 2. Check separation from other lakes
            bool bOverlapsOtherLake = false;
            for (const FVector4& Existing : Footprints)
            {
                FVector2D ExistingCenter(Existing.X, Existing.Y);
                float ExistingRadius = Existing.Z;
                float MinDist = CandidateRadius + ExistingRadius + 1000.0f;

                if (FVector2D::DistSquared(CandidateCenter, ExistingCenter) < FMath::Square(MinDist))
                {
                    bOverlapsOtherLake = true;
                    break;
                }
            }

            if (bOverlapsOtherLake) continue;

            // Valid location found!
            Footprints.Add(FVector4(CenterX, CenterY, CandidateRadius, 0.0f));
            bPlaced = true;

            TArray<FVector> Vertices;
            TArray<int32> Triangles;
            TArray<FVector> Normals;
            TArray<FVector2D> UVs;
            TArray<FProcMeshTangent> Tangents;

            const int32 NumRadialVerts = 16;
            const float AngleStep = UE_TWO_PI / NumRadialVerts;

            // Center vertex elevated to Z = 2.0f so it renders ABOVE the grass plane (Z = 0.0f)
            Vertices.Add(FVector(CenterX, CenterY, 2.0f));
            Normals.Add(FVector::UpVector);
            UVs.Add(FVector2D(CenterX, CenterY) * UVScale);

            for (int32 i = 0; i < NumRadialVerts; ++i)
            {
                float Angle = i * AngleStep;
                float ShorelineJitter = FMath::FRandRange(0.85f, 1.15f);
                float VertexRadius = CandidateRadius * ShorelineJitter;

                float Vx = CenterX + FMath::Cos(Angle) * VertexRadius;
                float Vy = CenterY + FMath::Sin(Angle) * VertexRadius;

                Vertices.Add(FVector(Vx, Vy, 2.0f));
                Normals.Add(FVector::UpVector);
                UVs.Add(FVector2D(Vx, Vy) * UVScale);
            }

            for (int32 i = 1; i <= NumRadialVerts; ++i)
            {
                int32 NextIdx = (i % NumRadialVerts) + 1;
                Triangles.Add(0);
                Triangles.Add(NextIdx);
                Triangles.Add(i);
            }

            WaterMesh->CreateMeshSection(LakeIdx, Vertices, Triangles, Normals, UVs, TArray<FColor>(), Tangents, false);
            if (WaterMaterial)
            {
                WaterMesh->SetMaterial(LakeIdx, WaterMaterial);
            }

            break;
        }

        if (!bPlaced)
        {
            UE_LOG(LogTemp, Warning, TEXT("GenerateLakes: Could not find valid spot for lake %d (road network too dense)."), LakeIdx);
        }
    }

    return Footprints;
}

void ARoadNetworkVisualizer::ScatterFoliage(Network* RoadNetwork, const FBox2D& Bounds, const TArray<FVector4>& LakeFootprints)
{
    if (!RoadNetwork) return;

    // Clear old instances
    if (TreeHISM) TreeHISM->ClearInstances();
    for (auto* Variant : TreeHISMVariants)
    {
        if (Variant) Variant->ClearInstances();
    }
    if (GrassHISM) GrassHISM->ClearInstances();

    // Assign meshes and apply render distances for performance
    auto SetupHISM = [this](UHierarchicalInstancedStaticMeshComponent* HISM, UStaticMesh* Mesh) {
        if (HISM && Mesh)
        {
            HISM->SetStaticMesh(Mesh);
            HISM->SetCullDistances(0, FoliageDrawDistance);
            HISM->SetCastShadow(false);
        }
        };

    SetupHISM(TreeHISM, TreeMesh);
    SetupHISM(GrassHISM, GrassClumpMesh);
    for (auto* Variant : TreeHISMVariants)
    {
        if (Variant)
        {
            Variant->SetCullDistances(0, FoliageDrawDistance);
            Variant->SetCastShadow(false);
        }
    }

    // --- Fast occupancy grid setup -------------------------------------------
    const int32 MaxGridDimension = 1024;
    const float RawSpacingX = (Bounds.Max.X - Bounds.Min.X) / MaxGridDimension;
    const float RawSpacingY = (Bounds.Max.Y - Bounds.Min.Y) / MaxGridDimension;
    const float Step = FMath::Max3(FMath::Max(50.0f, FoliageSpacing), RawSpacingX, RawSpacingY);

    int32 NumCols = FMath::CeilToInt((Bounds.Max.X - Bounds.Min.X) / Step) + 1;
    int32 NumRows = FMath::CeilToInt((Bounds.Max.Y - Bounds.Min.Y) / Step) + 1;
    if (NumCols <= 0 || NumRows <= 0) return;

    UE_LOG(LogTemp, Warning, TEXT("ScatterFoliage grid: %d x %d = %lld cells, Step=%.1f"),
        NumCols, NumRows, (int64)NumCols * (int64)NumRows, Step);

    TArray<bool> RoadOccupancyGrid;
    RoadOccupancyGrid.Init(false, NumCols * NumRows);

    // Stamp roads/intersections onto the grid ONCE, instead of scanning the
    // whole network per candidate point. Clearance radius is lane-aware so it
    // matches the actual pavement footprint (offset by MedianGap + FullWidth
    // in AddSeg), not just a flat distance from the centerline.
    auto StampCircle = [&](const FVector2D& Center, float Radius)
        {
            const float RadiusSq = Radius * Radius;
            int32 MinCol = FMath::Clamp(FMath::FloorToInt((Center.X - Radius - Bounds.Min.X) / Step), 0, NumCols - 1);
            int32 MaxCol = FMath::Clamp(FMath::FloorToInt((Center.X + Radius - Bounds.Min.X) / Step), 0, NumCols - 1);
            int32 MinRow = FMath::Clamp(FMath::FloorToInt((Center.Y - Radius - Bounds.Min.Y) / Step), 0, NumRows - 1);
            int32 MaxRow = FMath::Clamp(FMath::FloorToInt((Center.Y + Radius - Bounds.Min.Y) / Step), 0, NumRows - 1);

            for (int32 r = MinRow; r <= MaxRow; ++r)
                for (int32 c = MinCol; c <= MaxCol; ++c)
                {
                    FVector2D CellPos(Bounds.Min.X + c * Step, Bounds.Min.Y + r * Step);
                    if (FVector2D::DistSquared(CellPos, Center) <= RadiusSq)
                        RoadOccupancyGrid[r * NumCols + c] = true;
                }
        };

    auto EdgeClearanceRadius = [&](int32 Lanes) -> float
        {
            const int32 SafeLanes = FMath::Max(1, Lanes);
            return MedianGapCm + (SafeLanes * 350.0f) + RoadClearanceDistance;
        };

    auto StampSegment = [&](const FVector2D& SegStart, const FVector2D& SegEnd, float Radius)
        {
            const float RadiusSq = Radius * Radius;
            float MinX = FMath::Min(SegStart.X, SegEnd.X) - Radius;
            float MaxX = FMath::Max(SegStart.X, SegEnd.X) + Radius;
            float MinY = FMath::Min(SegStart.Y, SegEnd.Y) - Radius;
            float MaxY = FMath::Max(SegStart.Y, SegEnd.Y) + Radius;

            int32 MinCol = FMath::Clamp(FMath::FloorToInt((MinX - Bounds.Min.X) / Step), 0, NumCols - 1);
            int32 MaxCol = FMath::Clamp(FMath::FloorToInt((MaxX - Bounds.Min.X) / Step), 0, NumCols - 1);
            int32 MinRow = FMath::Clamp(FMath::FloorToInt((MinY - Bounds.Min.Y) / Step), 0, NumRows - 1);
            int32 MaxRow = FMath::Clamp(FMath::FloorToInt((MaxY - Bounds.Min.Y) / Step), 0, NumRows - 1);

            for (int32 r = MinRow; r <= MaxRow; ++r)
                for (int32 c = MinCol; c <= MaxCol; ++c)
                {
                    FVector2D CellPos(Bounds.Min.X + c * Step, Bounds.Min.Y + r * Step);
                    float DistSq = FMath::PointDistToSegmentSquared(
                        FVector(CellPos, 0.0f), FVector(SegStart, 0.0f), FVector(SegEnd, 0.0f));
                    if (DistSq <= RadiusSq)
                        RoadOccupancyGrid[r * NumCols + c] = true;
                }
        };

    for (const auto& Pair : RoadNetwork->getNodes())
    {
        const Node& N = Pair.second;
        const FVector* StartLocPtr = CachedNodeLocations.Find(N.getId());
        if (!StartLocPtr) continue;
        FVector2D Start2D(StartLocPtr->X, StartLocPtr->Y);

        int32 MaxLanesAtNode = 1;
        for (const Road& E : N.outgoingEdges)
        {
            MaxLanesAtNode = FMath::Max(MaxLanesAtNode, FMath::Max(1, E.getLanes()));
        }

        if (RoadIntersectionUtil::IsIntersectionNode(N))
        {
            StampCircle(Start2D, EdgeClearanceRadius(MaxLanesAtNode));
        }

        for (const Road& E : N.outgoingEdges)
        {
            const FVector* EndLocPtr = CachedNodeLocations.Find(E.getDest());
            if (!EndLocPtr) continue;
            FVector2D End2D(EndLocPtr->X, EndLocPtr->Y);

            const float Radius = EdgeClearanceRadius(E.getLanes());

            if (E.hasCurveGeometry())
            {
                const std::vector<RoadGeomPoint>& Geom = E.getGeometry();
                for (size_t i = 0; i < Geom.size() - 1; i++)
                {
                    FVector2D SegStart((Geom[i].x - OriginOffsetX) * 100.0, (Geom[i].y - OriginOffsetY) * 100.0);
                    FVector2D SegEnd((Geom[i + 1].x - OriginOffsetX) * 100.0, (Geom[i + 1].y - OriginOffsetY) * 100.0);
                    StampSegment(SegStart, SegEnd, Radius);
                }
            }
            else
            {
                StampSegment(Start2D, End2D, Radius);
            }
        }
    }

    // --- Populate using the grid (O(1) road check per cell) -----------------
    TArray<FTransform> TreeTransforms;
    TArray<FTransform> GrassTransforms;
    TMap<UHierarchicalInstancedStaticMeshComponent*, TArray<FTransform>> VariantTransforms;

    for (int32 c = 0; c < NumCols; ++c)
    {
        float X = Bounds.Min.X + c * Step;
        if (X >= Bounds.Max.X) break;

        for (int32 r = 0; r < NumRows; ++r)
        {
            float Y = Bounds.Min.Y + r * Step;
            if (Y >= Bounds.Max.Y) break;

            float JitterX = FMath::FRandRange(-Step * 0.4f, Step * 0.4f);
            float JitterY = FMath::FRandRange(-Step * 0.4f, Step * 0.4f);
            FVector2D CandidatePoint(X + JitterX, Y + JitterY);

            // Re-check occupancy at the JITTERED position, not the pre-jitter
            // cell center -- otherwise a point that passed the check can still
            // drift back onto the road.
            int32 CheckCol = FMath::Clamp(FMath::FloorToInt((CandidatePoint.X - Bounds.Min.X) / Step), 0, NumCols - 1);
            int32 CheckRow = FMath::Clamp(FMath::FloorToInt((CandidatePoint.Y - Bounds.Min.Y) / Step), 0, NumRows - 1);
            if (RoadOccupancyGrid[CheckRow * NumCols + CheckCol]) continue;

            bool bInLake = false;
            for (const FVector4& Lake : LakeFootprints)
            {
                FVector2D LakeCenter(Lake.X, Lake.Y);
                float LakeRadius = Lake.Z;
                if (FVector2D::DistSquared(CandidatePoint, LakeCenter) < FMath::Square(LakeRadius + 150.0f))
                {
                    bInLake = true;
                    break;
                }
            }
            if (bInLake) continue;

            float SpawnRoll = FMath::FRand();
            FVector SpawnLoc(CandidatePoint.X, CandidatePoint.Y, 5.0f);

            if (SpawnRoll < 0.25f) // Tree Spawn Rate
            {
                UHierarchicalInstancedStaticMeshComponent* SelectedTreeHISM = TreeHISM;
                if (TreeHISMVariants.Num() > 0)
                {
                    int32 Index = FMath::RandRange(-1, TreeHISMVariants.Num() - 1);
                    if (Index >= 0 && TreeHISMVariants[Index] != nullptr)
                    {
                        SelectedTreeHISM = TreeHISMVariants[Index];
                    }
                }

                if (SelectedTreeHISM && SelectedTreeHISM->GetStaticMesh())
                {
                    float Scale = FMath::FRandRange(0.7f, 1.3f) * TreeScaleMultiplier;
                    float Yaw = FMath::FRandRange(0.0f, 360.0f);
                    FTransform Transform(FRotator(0.0f, Yaw, 0.0f), SpawnLoc, FVector(Scale));

                    if (SelectedTreeHISM == TreeHISM)
                    {
                        TreeTransforms.Add(Transform);
                    }
                    else
                    {
                        VariantTransforms.FindOrAdd(SelectedTreeHISM).Add(Transform);
                    }
                }
            }
            else if (SpawnRoll < 0.40f) // Grass Spawn Rate
            {
                if (GrassHISM && GrassHISM->GetStaticMesh())
                {
                    float Scale = FMath::FRandRange(0.5f, 1.2f) * GrassScaleMultiplier;
                    float Yaw = FMath::FRandRange(0.0f, 360.0f);
                    FTransform Transform(FRotator(0.0f, Yaw, 0.0f), SpawnLoc, FVector(Scale));
                    GrassTransforms.Add(Transform);
                }
            }
            // remaining chance = empty clearing
        }
    }

    UE_LOG(LogTemp, Warning, TEXT("ScatterFoliage: adding %d trees, %d grass instances in bulk..."),
        TreeTransforms.Num(), GrassTransforms.Num());

    if (TreeHISM && TreeTransforms.Num() > 0)
    {
        TreeHISM->AddInstances(TreeTransforms, false);
    }
    if (GrassHISM && GrassTransforms.Num() > 0)
    {
        GrassHISM->AddInstances(GrassTransforms, false);
    }
    for (auto& Pair : VariantTransforms)
    {
        if (Pair.Key && Pair.Value.Num() > 0)
        {
            Pair.Key->AddInstances(Pair.Value, false);
        }
    }

    UE_LOG(LogTemp, Warning, TEXT("ScatterFoliage: done."));
}

// Fast, pure 2D mathematical projection helper 
static float PointToSegmentDistance2DSquared(const FVector2D& Point, const FVector2D& Start, const FVector2D& End)
{
    const float SegLengthSq = FVector2D::DistSquared(Start, End);
    if (SegLengthSq < 1e-4f)
    {
        return FVector2D::DistSquared(Point, Start);
    }

    // Projection factor t, clamped to [0, 1]
    const float t = FMath::Clamp(FVector2D::DotProduct(Point - Start, End - Start) / SegLengthSq, 0.0f, 1.0f);
    const FVector2D Projection = Start + t * (End - Start);
    return FVector2D::DistSquared(Point, Projection);
}

bool ARoadNetworkVisualizer::IsNearAnyRoad(Network* RoadNetwork, const FVector2D& Point, float Distance) const
{
    if (!RoadNetwork) return false;

    for (const auto& NodePair : RoadNetwork->getNodes())
    {
        const Node& OriginNode = NodePair.second;
        const FVector2D StartLoc((OriginNode.getX() - OriginOffsetX) * 100.0, (OriginNode.getY() - OriginOffsetY) * 100.0);

        for (const Road& Edge : OriginNode.outgoingEdges)
        {
            const Node* DestNode = RoadNetwork->getNode(Edge.getDest());
            if (!DestNode) continue;

            const FVector2D EndLoc((DestNode->getX() - OriginOffsetX) * 100.0, (DestNode->getY() - OriginOffsetY) * 100.0);

            const int32 SafeLanes = FMath::Max(1, Edge.getLanes());
            // Total clearance = Median Offset + Full Road Width + User Clearance Buffer
            const float EdgeClearance = MedianGapCm + (SafeLanes * 350.0f) + Distance;
            const float DistSqLimit = FMath::Square(EdgeClearance);

            if (Edge.hasCurveGeometry())
            {
                const auto& Geom = Edge.getGeometry();
                if (Geom.size() >= 2)
                {
                    for (size_t i = 0; i < Geom.size() - 1; ++i)
                    {
                        const FVector2D P0((Geom[i].x - OriginOffsetX) * 100.0, (Geom[i].y - OriginOffsetY) * 100.0);
                        const FVector2D P1((Geom[i + 1].x - OriginOffsetX) * 100.0, (Geom[i + 1].y - OriginOffsetY) * 100.0);

                        // Fast AABB check using Unreal's FBox2D
                        FBox2D SegBox(ForceInit);
                        SegBox += P0;
                        SegBox += P1;
                        SegBox = SegBox.ExpandBy(EdgeClearance);

                        if (!SegBox.IsInside(Point))
                        {
                            continue;
                        }

                        if (PointToSegmentDistance2DSquared(Point, P0, P1) < DistSqLimit)
                        {
                            return true;
                        }
                    }
                }
            }
            else
            {
                FBox2D EdgeBox(ForceInit);
                EdgeBox += StartLoc;
                EdgeBox += EndLoc;
                EdgeBox = EdgeBox.ExpandBy(EdgeClearance);

                if (EdgeBox.IsInside(Point))
                {
                    if (PointToSegmentDistance2DSquared(Point, StartLoc, EndLoc) < DistSqLimit)
                    {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

void ARoadNetworkVisualizer::BuildFlatMeshSection(UProceduralMeshComponent* TargetMesh, const FBox2D& Bounds, float ZHeight, int32 Subdivisions)
{
    if (!TargetMesh) return;

    TArray<FVector> Vertices;
    TArray<int32> Triangles;
    TArray<FVector> Normals;
    TArray<FVector2D> UVs;
    TArray<FProcMeshTangent> Tangents;

    Vertices.Add(FVector(Bounds.Min.X, Bounds.Min.Y, ZHeight));
    Vertices.Add(FVector(Bounds.Max.X, Bounds.Min.Y, ZHeight));
    Vertices.Add(FVector(Bounds.Max.X, Bounds.Max.Y, ZHeight));
    Vertices.Add(FVector(Bounds.Min.X, Bounds.Max.Y, ZHeight));

    Triangles.Add(0); Triangles.Add(2); Triangles.Add(1);
    Triangles.Add(0); Triangles.Add(3); Triangles.Add(2);

    Normals.Add(FVector::UpVector);
    Normals.Add(FVector::UpVector);
    Normals.Add(FVector::UpVector);
    Normals.Add(FVector::UpVector);

    // Dynamic UV tiling based on world coords to prevent texture stretching
    float UVScale = 0.001f; // 1 UV tile per 10 meters
    UVs.Add(FVector2D(Bounds.Min.X * UVScale, Bounds.Min.Y * UVScale));
    UVs.Add(FVector2D(Bounds.Max.X * UVScale, Bounds.Min.Y * UVScale));
    UVs.Add(FVector2D(Bounds.Max.X * UVScale, Bounds.Max.Y * UVScale));
    UVs.Add(FVector2D(Bounds.Min.X * UVScale, Bounds.Max.Y * UVScale));

    TargetMesh->CreateMeshSection(0, Vertices, Triangles, Normals, UVs, TArray<FColor>(), Tangents, false);
}