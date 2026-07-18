#include "RoadNetworkVisualizer.h"
#include "RoadTurnLaneOptions.h"
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

    // Bridge dressing: deck slabs (cube) and support pillars (cylinder) from
    // the engine's basic shapes, so no project asset is required and the
    // constructor hard references get them cooked.
    DeckHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("DeckHISM"));
    DeckHISM->SetupAttachment(RootComponent);
    DeckHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    DeckHISM->SetCastShadow(false);

    PillarHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("PillarHISM"));
    PillarHISM->SetupAttachment(RootComponent);
    PillarHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    PillarHISM->SetCastShadow(false);

    static ConstructorHelpers::FObjectFinderOptional<UStaticMesh> CubeFinder(
        TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (CubeFinder.Succeeded())
    {
        DeckHISM->SetStaticMesh(CubeFinder.Get());
    }
    static ConstructorHelpers::FObjectFinderOptional<UStaticMesh> CylinderFinder(
        TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    if (CylinderFinder.Succeeded())
    {
        PillarHISM->SetStaticMesh(CylinderFinder.Get());
    }
}

void ARoadNetworkVisualizer::BuildVisualNetwork(Network* RoadNetwork, FString InNodesPath, FString InEdgesPath)
{
    if (!RoadNetwork) return;

    // 1. Store the paths for later exporting
    NodesFilePath = InNodesPath;
    EdgesFilePath = InEdgesPath;

    // Fresh map: remember the network for runtime edits/rebuilds and let the
    // origin be recomputed from its bounds.
    CachedNetwork = RoadNetwork;
    bOriginLocked = false;

    RefreshRoadVisuals();
}

void ARoadNetworkVisualizer::RefreshRoadVisuals()
{
    Network* RoadNetwork = CachedNetwork;
    if (!RoadNetwork) return;

    // Re-derive elevations before drawing. Runtime edits leave stale z data
    // behind (splits insert ground-level vertices into elevated centerlines,
    // new nodes default to z 0, layer edits change profiles entirely); this
    // pass is idempotent and keeps node heights, ramps, and junction-face
    // flat zones consistent with the current graph. The median gap must match
    // the one the setback trimming below uses.
    RoadNetwork->applyVerticality(Network::DefaultLayerHeightM, Network::DefaultRampLengthM,
        MedianGapCm / 100.0);

    // Reset max IDs
    CurrentMaxNodeId = 0;
    CurrentMaxEdgeId = 0;

    RoadHISM->ClearInstances();
    InstanceIndexToEdgeId.Empty();
    EdgeIdToNodes.Empty();
    CachedNodeLocations.Empty();

    const auto& AllNodes = RoadNetwork->getNodes();
    if (AllNodes.empty()) return;

    if (!bOriginLocked)
    {
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

        // Set the offset to the exact center of the bounding box. Locked from
        // here on: a runtime rebuild must not shift the world under the
        // camera (or under the sim, whose own origin was computed at load).
        OriginOffsetX = (MinX + MaxX) / 2.0;
        OriginOffsetY = (MinY + MaxY) / 2.0;
        bOriginLocked = true;
    }

    // --- Pillar-vs-lower-road occlusion ------------------------------------
    // Elevated piers drop straight to the ground, so one landing on a road
    // passing underneath punches through its pavement. Index every edge's
    // centerline (with surface height and pavement half-width) in a coarse
    // 2D grid first; pier placement below queries it and shifts or drops
    // piers that would clip a lower road.
    struct FBlockerSeg
    {
        FVector2D A, B;      // centerline segment endpoints (Unreal cm)
        float ZA, ZB;        // road surface height at each endpoint (cm)
        float HalfWidthCm;   // centerline to outer pavement edge
        uint64_t KeyU, KeyV; // canonical node pair, to skip a pier's own edge
    };
    TArray<FBlockerSeg> BlockerSegs;
    TMap<FIntPoint, TArray<int32>> BlockerGrid;
    const float BlockerCellCm = 3000.0f;

    if (bElevatedRoadDecor)
    {
        for (const auto& NodePair : AllNodes)
        {
            const Node& FromNode = NodePair.second;
            for (const Road& Edge : FromNode.outgoingEdges)
            {
                Node* ToNode = RoadNetwork->getNode(Edge.getDest());
                if (!ToNode) continue;

                const float HalfWidthCm =
                    MedianGapCm + FMath::Max(1, Edge.getLanes()) * 350.0f;
                const uint64_t KeyU = FMath::Min(FromNode.getId(), Edge.getDest());
                const uint64_t KeyV = FMath::Max(FromNode.getId(), Edge.getDest());

                auto AddBlockerSeg = [&](double X0, double Y0, double Z0,
                                         double X1, double Y1, double Z1)
                {
                    FBlockerSeg S;
                    S.A = FVector2D((X0 - OriginOffsetX) * 100.0, (Y0 - OriginOffsetY) * 100.0);
                    S.B = FVector2D((X1 - OriginOffsetX) * 100.0, (Y1 - OriginOffsetY) * 100.0);
                    if (S.A.Equals(S.B, 1.0f)) return;
                    S.ZA = static_cast<float>(Z0 * 100.0);
                    S.ZB = static_cast<float>(Z1 * 100.0);
                    S.HalfWidthCm = HalfWidthCm;
                    S.KeyU = KeyU;
                    S.KeyV = KeyV;
                    const int32 Idx = BlockerSegs.Add(S);

                    // Register in every cell the segment's inflated bounds touch.
                    const int32 CX0 = FMath::FloorToInt((FMath::Min(S.A.X, S.B.X) - HalfWidthCm) / BlockerCellCm);
                    const int32 CX1 = FMath::FloorToInt((FMath::Max(S.A.X, S.B.X) + HalfWidthCm) / BlockerCellCm);
                    const int32 CY0 = FMath::FloorToInt((FMath::Min(S.A.Y, S.B.Y) - HalfWidthCm) / BlockerCellCm);
                    const int32 CY1 = FMath::FloorToInt((FMath::Max(S.A.Y, S.B.Y) + HalfWidthCm) / BlockerCellCm);
                    for (int32 CX = CX0; CX <= CX1; CX++)
                        for (int32 CY = CY0; CY <= CY1; CY++)
                            BlockerGrid.FindOrAdd(FIntPoint(CX, CY)).Add(Idx);
                };

                if (Edge.hasCurveGeometry())
                {
                    const std::vector<RoadGeomPoint>& Geom = Edge.getGeometry();
                    for (size_t i = 0; i + 1 < Geom.size(); i++)
                    {
                        AddBlockerSeg(Geom[i].x, Geom[i].y, Geom[i].z,
                                      Geom[i + 1].x, Geom[i + 1].y, Geom[i + 1].z);
                    }
                }
                else
                {
                    AddBlockerSeg(FromNode.getX(), FromNode.getY(), FromNode.getZ(),
                                  ToNode->getX(), ToNode->getY(), ToNode->getZ());
                }
            }
        }
    }

    // True when a pier shaft at C (radius RadiusCm, spanning ground..TopZ)
    // would land within PillarClearanceCm of a road lower than the deck it
    // supports. The pier's own edge is excluded so a ramp can't block its
    // own piers; higher decks and below-ground underpasses never block.
    auto PillarBlocked = [&](const FVector2D& C, float RadiusCm, float TopZ,
                             uint64_t OwnU, uint64_t OwnV) -> bool
    {
        if (BlockerSegs.Num() == 0) return false;
        const float Reach = RadiusCm + PillarClearanceCm;
        const int32 CX0 = FMath::FloorToInt((C.X - Reach) / BlockerCellCm);
        const int32 CX1 = FMath::FloorToInt((C.X + Reach) / BlockerCellCm);
        const int32 CY0 = FMath::FloorToInt((C.Y - Reach) / BlockerCellCm);
        const int32 CY1 = FMath::FloorToInt((C.Y + Reach) / BlockerCellCm);
        for (int32 CX = CX0; CX <= CX1; CX++)
        {
            for (int32 CY = CY0; CY <= CY1; CY++)
            {
                const TArray<int32>* Cell = BlockerGrid.Find(FIntPoint(CX, CY));
                if (!Cell) continue;
                for (int32 Idx : *Cell)
                {
                    const FBlockerSeg& S = BlockerSegs[Idx];
                    if (S.KeyU == OwnU && S.KeyV == OwnV) continue;

                    const FVector2D AB = S.B - S.A;
                    const float LenSq = AB.SizeSquared();
                    const float T = (LenSq > 1.0f)
                        ? FMath::Clamp(FVector2D::DotProduct(C - S.A, AB) / LenSq, 0.0f, 1.0f)
                        : 0.0f;
                    if (FVector2D::Distance(C, S.A + AB * T) >= S.HalfWidthCm + Reach) continue;

                    const float SurfZ = FMath::Lerp(S.ZA, S.ZB, T);
                    if (SurfZ < TopZ - 1.0f && SurfZ > -DeckThicknessCm) return true;
                }
            }
        }
        return false;
    };

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

    // The four arrays grow in lockstep (one entry per road piece in AddSeg),
    // so the previous build's count sizes all of them. A fixed 120k reserve
    // here cost ~11.5 MB of FTransforms up front on every rebuild, even for
    // tiny maps; only the first build now pays amortized growth.
    const int32 ReserveGuess = (LastRoadInstanceCount > 0)
        ? LastRoadInstanceCount + LastRoadInstanceCount / 16 + 256
        : 4096;
    Transforms.Reserve(ReserveGuess);
    TempEdgeIds.Reserve(ReserveGuess);
    TempScaleX.Reserve(ReserveGuess);
    TempLanes.Reserve(ReserveGuess);

    // Bridge dressing gathered alongside the road pieces.
    TArray<FTransform> DeckTransforms;
    TArray<FTransform> PillarTransforms;

    // Per-edge scratch, hoisted out of the loop so thousands of per-edge
    // heap allocations become Reset() reuse. Every array is refilled from
    // scratch at the top of each edge iteration.
    TArray<FVector> Pts;            // raw centerline
    TArray<float> Cum;              // cumulative arc length at each vertex
    TArray<FVector> TPts;           // trimmed centerline actually drawn
    TArray<float> TCum;
    TArray<FVector> SegDir;
    TArray<FVector> SegRight;
    TArray<FRotator> SegRot;
    TArray<float> SegSlopeScale;
    TArray<float> JointSin;

    for (const auto& NodePair : AllNodes)
    {
        const Node& OriginNode = NodePair.second;

        // Node elevation (cm) from the sim's verticality pass -- nonzero where
        // elevated spans meet (e.g. mid-viaduct joints), 0 at ground level.
        const double NodeZCm = OriginNode.getZ() * 100.0;

        FVector NodeLoc((OriginNode.getX() - OriginOffsetX) * 100.0,
            (OriginNode.getY() - OriginOffsetY) * 100.0,
            NodeZCm - 2.0);

        if (bSetbackAtIntersections)
        {
            // Fill the junction box the road setbacks carve out with a pavement
            // polygon that meets each incident road's end face.
            if (RoadIntersectionUtil::IsIntersectionNode(OriginNode))
            {
                // Just below the road surface so short-edge overlap hides
                // under the roads, but above typical floor/ground actors.
                const FVector JunctionCenter(NodeLoc.X, NodeLoc.Y, NodeZCm - 0.2);
                AppendJunctionPolygon(RoadNetwork, OriginNode, JunctionCenter,
                    JunctionVerts, JunctionTris, JunctionNormals, JunctionUVs);

                // An elevated junction gets one central pier down to the
                // ground -- unless a lower road runs beneath the junction, in
                // which case the deck has to span it unsupported.
                const float PierTopZ = NodeZCm - DeckThicknessCm;
                if (bElevatedRoadDecor && PierTopZ > PillarMinHeightCm)
                {
                    const float SetbackCm = RoadIntersectionUtil::GetNodeSetbackMeters(
                        RoadNetwork, OriginNode, MedianGapCm / 100.0f) * 100.0f;
                    const float Dia = FMath::Clamp(SetbackCm * 0.8f, 300.0f, 600.0f);
                    if (!PillarBlocked(FVector2D(NodeLoc.X, NodeLoc.Y), Dia * 0.5f, PierTopZ, 0, 0))
                    {
                        PillarTransforms.Add(FTransform(FQuat::Identity,
                            FVector(NodeLoc.X, NodeLoc.Y, PierTopZ * 0.5f),
                            FVector(Dia / 100.0f, Dia / 100.0f, PierTopZ / 100.0f)));
                    }
                }
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

            // Recorded before any visual-culling 'continue' below, so every
            // edge is resolvable for property edits.
            EdgeIdToNodes.Add(Edge.getEdgeId(), TPair<uint64_t, uint64_t>(OriginNode.getId(), Edge.getDest()));

            Node* DestNode = RoadNetwork->getNode(Edge.getDest());
            if (!DestNode) continue;

            // Subtracting Origin forces the geographic center to 0,0. Z comes
            // from the node elevations (0 for ground-level roads).
            FVector StartLoc((OriginNode.getX() - OriginOffsetX) * 100.0,
                (OriginNode.getY() - OriginOffsetY) * 100.0,
                OriginNode.getZ() * 100.0);

            FVector EndLoc((DestNode->getX() - OriginOffsetX) * 100.0,
                (DestNode->getY() - OriginOffsetY) * 100.0,
                DestNode->getZ() * 100.0);

            // Ensure we always have at least 1 lane to prevent divide-by-zero in the shader
            int32 SafeLanes = FMath::Max(1, Edge.getLanes());

            // --- Real-world centerline (OSM geometry_xy) ------------------------
            // Curved edges render as a chain of straight pieces that follow the
            // OSM shape polyline instead of one chord instance node-to-node. Its
            // endpoints were snapped onto the node coordinates at network build,
            // so the chain stays flush at junctions. Shapeless edges (runtime
            // roads, missing data) fall back to the straight chord.
            Pts.Reset();
            if (Edge.hasCurveGeometry())
            {
                const std::vector<RoadGeomPoint>& Geom = Edge.getGeometry();
                Pts.Reserve(Geom.size());
                for (const RoadGeomPoint& P : Geom)
                {
                    Pts.Add(FVector((P.x - OriginOffsetX) * 100.0,
                                    (P.y - OriginOffsetY) * 100.0,
                                    P.z * 100.0));
                }
            }
            else
            {
                Pts.Add(StartLoc);
                Pts.Add(EndLoc);
            }

            // Cumulative arc length (cm) at each centerline vertex.
            Cum.Reset();
            Cum.Reserve(Pts.Num());
            Cum.Add(0.0f);
            for (int32 i = 1; i < Pts.Num(); i++)
            {
                Cum.Add(Cum[i - 1] + FVector::Dist2D(Pts[i - 1], Pts[i]));
            }
            const float TotalLenCm = Cum.Last();
            if (TotalLenCm < 1.0f) continue;

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
            TPts.Reset();
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

            // Per-segment frame of the trimmed centerline. Arc lengths and the
            // placement axes (SegDir/SegRight) stay 2D so setbacks, tapers and
            // widths are unaffected by slope; SegRot carries the true 3D pitch
            // so pieces tilt along bridge ramps, and SegSlopeScale stretches a
            // piece's length back out so its horizontal footprint still covers
            // the intended 2D span.
            const int32 NumSegs = TPts.Num() - 1;
            TCum.Reset();
            SegDir.Reset();
            SegRight.Reset();
            SegRot.Reset();
            SegSlopeScale.Reset();
            TCum.Reserve(TPts.Num());
            SegDir.Reserve(NumSegs);
            SegRight.Reserve(NumSegs);
            SegRot.Reserve(NumSegs);
            SegSlopeScale.Reserve(NumSegs);
            TCum.Add(0.0f);
            for (int32 i = 0; i < NumSegs; i++)
            {
                const FVector D = TPts[i + 1] - TPts[i];
                const float Len2D = D.Size2D();
                TCum.Add(TCum[i] + Len2D);
                const FVector DN2 = D.GetSafeNormal2D();
                SegDir.Add(DN2);
                SegRight.Add(FVector(-DN2.Y, DN2.X, 0.0));
                SegRot.Add(D.GetSafeNormal().Rotation());
                SegSlopeScale.Add(Len2D > KINDA_SMALL_NUMBER ? D.Size() / Len2D : 1.0f);
            }
            const float DrawLenCm = TCum.Last();

            // Turn sharpness (sin of the heading change) at each interior
            // vertex, used to size the miter fills. Ends stay 0.
            // Reset+AddZeroed instead of Init: Init calls Empty(N) internally,
            // which would reallocate to exact size every edge.
            JointSin.Reset(TPts.Num());
            JointSin.AddZeroed(TPts.Num());
            for (int32 i = 1; i < NumSegs; i++)
            {
                const float Cross = SegDir[i - 1].X * SegDir[i].Y - SegDir[i - 1].Y * SegDir[i].X;
                JointSin[i] = FMath::Min(FMath::Abs(Cross), 1.0f);
            }

            const float FullWidthCm = SafeLanes * 350.0f;

            // --- Lane-drop / lane-gain detection -------------------------------
            // Which through-continuation each end tapers to (and by how many
            // lanes) is decided by the shared RoadIntersectionUtil helper, so
            // the vehicle renderer clamps cars onto exactly the pavement drawn
            // here. Keep any tuning of these knobs mirrored there.
            int32 DownstreamLanes = SafeLanes; // lanes we taper DOWN to at the end
            int32 UpstreamLanes   = SafeLanes; // lanes we taper UP from at the start
            if (bTaperLaneDrops && bScaleWidthByLanes)
            {
                DownstreamLanes = RoadIntersectionUtil::GetTaperNeighborLanes(
                    RoadNetwork, Edge, /*AtEnd=*/true, TaperAlignmentDot, TaperMaxLaneDelta);
                UpstreamLanes = RoadIntersectionUtil::GetTaperNeighborLanes(
                    RoadNetwork, Edge, /*AtEnd=*/false, TaperAlignmentDot, TaperMaxLaneDelta);
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

                    // Height at the piece's pivot. SegDir is planar, so Loc.Z is
                    // still TPts[i].Z here; replace it with the slope-interpolated
                    // height so the pitched piece (rotated about its pivot by
                    // SegRot) lands its ends on the centerline. AlongRef can sit
                    // slightly outside the segment on mitered joints -- the
                    // unclamped lerp extends the same slope, which is what the
                    // miter needs.
                    const float SegLen2D = TCum[i + 1] - TCum[i];
                    if (SegLen2D > KINDA_SMALL_NUMBER)
                    {
                        Loc.Z = FMath::Lerp(TPts[i].Z, TPts[i + 1].Z, AlongRef / SegLen2D);
                    }
                    Loc.Z += (i % 3) * 0.03f;

                    // SegSlopeScale keeps the horizontal footprint of a pitched
                    // piece equal to its 2D arc span.
                    const float SX = PieceLen * SegSlopeScale[i] / FMath::Max(1.0f, MeshBaseLengthCm);
                    const float SY = bScaleWidthByLanes ? (WidthCm / FMath::Max(1.0f, MeshBaseWidthCm)) : 1.0f;

                    Transforms.Add(FTransform(SegRot[i], Loc, FVector(SX, SY, 1.0f)));
                    TempEdgeIds.Add(Edge.getEdgeId());
                    TempScaleX.Add(SX);
                    TempLanes.Add(LaneData);

                    // Concrete deck slab under a raised piece so the span has
                    // thickness instead of floating as a paper strip. The cube
                    // pivot is always centered, so recompute the piece-center
                    // position even when the road mesh pivots at its edge.
                    if (bElevatedRoadDecor && Loc.Z > DeckMinHeightCm)
                    {
                        const float MidAlong = Local0 + PieceLen * 0.5f;
                        FVector DeckLoc = TPts[i] + SegDir[i] * MidAlong
                                        + SegRight[i] * ((WidthCm * 0.5f) + MedianGapCm);
                        if (SegLen2D > KINDA_SMALL_NUMBER)
                        {
                            DeckLoc.Z = FMath::Lerp(TPts[i].Z, TPts[i + 1].Z, MidAlong / SegLen2D);
                        }
                        DeckLoc.Z -= DeckThicknessCm * 0.5f + 1.0f; // top just under the surface
                        DeckTransforms.Add(FTransform(SegRot[i], DeckLoc, FVector(
                            PieceLen * SegSlopeScale[i] / 100.0f,
                            WidthCm / 100.0f,
                            DeckThicknessCm / 100.0f)));
                    }
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

            // --- Support pillars under elevated spans ----------------------
            // Two-way streets are two directed edges over one centerline, so
            // only the canonical direction emits piers, centred under the
            // median where one row carries both decks. One-way spans get the
            // row under their own deck centre instead.
            if (bElevatedRoadDecor)
            {
                bool bHasReverse = false;
                for (const Road& Rev : DestNode->outgoingEdges)
                {
                    if (Rev.getDest() == OriginNode.getId()) { bHasReverse = true; break; }
                }
                if (!bHasReverse || OriginNode.getId() < Edge.getDest())
                {
                    const float LateralCm = bHasReverse ? 0.0f : (MedianGapCm + FullWidthCm * 0.5f);
                    const float DiaCm = bHasReverse
                        ? FMath::Clamp(2.0f * MedianGapCm + FullWidthCm * 0.5f, 200.0f, 500.0f)
                        : FMath::Clamp(FullWidthCm * 0.5f, 150.0f, 400.0f);

                    // Deck point + pier top height at arc distance SArc.
                    auto PierAt = [&](float SArc, FVector& OutP, float& OutTopZ) -> bool
                    {
                        int32 I = 0;
                        while (I + 1 < NumSegs && TCum[I + 1] < SArc) I++;
                        const float PSegLen = TCum[I + 1] - TCum[I];
                        if (PSegLen <= KINDA_SMALL_NUMBER) return false;
                        OutP = FMath::Lerp(TPts[I], TPts[I + 1],
                            (SArc - TCum[I]) / PSegLen) + SegRight[I] * LateralCm;
                        OutTopZ = OutP.Z - DeckThicknessCm;
                        return OutTopZ >= PillarMinHeightCm;
                    };

                    const uint64_t OwnU = FMath::Min(OriginNode.getId(), Edge.getDest());
                    const uint64_t OwnV = FMath::Max(OriginNode.getId(), Edge.getDest());

                    // A pier that would clip a road below slides along the
                    // span to the nearest clear spot; if nothing near the
                    // ideal position is clear, the deck spans the gap alone.
                    const float NudgeCm[] = { 0.0f,
                         0.2f * PillarSpacingCm, -0.2f * PillarSpacingCm,
                         0.4f * PillarSpacingCm, -0.4f * PillarSpacingCm };
                    for (float S = PillarSpacingCm * 0.5f; S < DrawLenCm; S += PillarSpacingCm)
                    {
                        for (float Nudge : NudgeCm)
                        {
                            const float STry = FMath::Clamp(S + Nudge, 0.0f, DrawLenCm);
                            FVector P;
                            float TopZ;
                            if (!PierAt(STry, P, TopZ)) continue;
                            if (PillarBlocked(FVector2D(P.X, P.Y), DiaCm * 0.5f, TopZ, OwnU, OwnV)) continue;

                            PillarTransforms.Add(FTransform(FQuat::Identity,
                                FVector(P.X, P.Y, TopZ * 0.5f),
                                FVector(DiaCm / 100.0f, DiaCm / 100.0f, TopZ / 100.0f)));
                            break;
                        }
                    }
                }
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

    // Bridge dressing: deck slabs + pillars. Concrete-grey fallback material
    // so the engine shapes don't render bright white against the asphalt.
    DeckHISM->ClearInstances();
    PillarHISM->ClearInstances();
    if (DeckTransforms.Num() > 0 || PillarTransforms.Num() > 0)
    {
        if (!ElevatedConcreteMaterial)
        {
            if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr,
                TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")))
            {
                UMaterialInstanceDynamic* Concrete = UMaterialInstanceDynamic::Create(Base, this);
                Concrete->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.085f, 0.085f, 0.08f));
                ElevatedConcreteMaterial = Concrete;
            }
        }
        if (ElevatedConcreteMaterial)
        {
            DeckHISM->SetMaterial(0, ElevatedConcreteMaterial);
            PillarHISM->SetMaterial(0, ElevatedConcreteMaterial);
        }

        DeckHISM->AddInstances(DeckTransforms, false);
        PillarHISM->AddInstances(PillarTransforms, false);
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

    LastRoadInstanceCount = Transforms.Num();

    RoadHISM->MarkRenderStateDirty();
}

// Position + unit tangent at 'ArcM' meters along an edge's raw shape polyline
// (map coordinates). Unlike Road::samplePointAt this measures true polyline
// arc length -- the same measure the visualizer trims setbacks with -- so a
// junction face lands exactly on the trimmed road end.
//
// Must stay out of line: MSVC 14.44's optimizer crashes (C1001) folding this
// polyline walk into AppendJunctionPolygon's neighbour loop, where it lands at
// two call sites under full optimization; newer toolsets inline it cleanly.
static FORCENOINLINE bool SampleGeometryAtArcMeters(const std::vector<RoadGeomPoint>& G, double ArcM,
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

    // A raised junction pavement gets side walls and an underside so the slab
    // matches the thickness of the road decks meeting it, instead of reading
    // as a floating sheet between them.
    if (bElevatedRoadDecor && CenterLoc.Z > DeckMinHeightCm)
    {
        const float BottomZ = CenterLoc.Z - DeckThicknessCm;

        auto AddSideVert = [&](const FVector& P, const FVector& OutNormal) -> int32
        {
            Normals.Add(OutNormal);
            UVs.Add(FVector2D(P.X + P.Y, P.Z) * UVScale);
            return Verts.Add(P);
        };

        for (int32 i = 0; i < N; i++)
        {
            const FVector& A = RingPts[i].Pos;
            const FVector& B = RingPts[(i + 1) % N].Pos;
            const FVector A2(A.X, A.Y, BottomZ);
            const FVector B2(B.X, B.Y, BottomZ);
            const FVector Out = FVector(
                (A.X + B.X) * 0.5f - CenterLoc.X,
                (A.Y + B.Y) * 0.5f - CenterLoc.Y, 0.0f).GetSafeNormal();

            const int32 IA = AddSideVert(A, Out);
            const int32 IB = AddSideVert(B, Out);
            const int32 IA2 = AddSideVert(A2, Out);
            const int32 IB2 = AddSideVert(B2, Out);

            // Both triangles face outward (front face = clockwise seen from
            // outside, same convention the top fan above establishes).
            Tris.Add(IA); Tris.Add(IB); Tris.Add(IA2);
            Tris.Add(IB); Tris.Add(IB2); Tris.Add(IA2);
        }

        auto AddBottomVert = [&](const FVector& P) -> int32
        {
            Normals.Add(-FVector::UpVector);
            UVs.Add(FVector2D(P.X, P.Y) * UVScale);
            return Verts.Add(P);
        };

        const int32 CB = AddBottomVert(FVector(CenterLoc.X, CenterLoc.Y, BottomZ));
        TArray<int32> RingB;
        RingB.Reserve(N);
        for (const FRingPoint& R : RingPts)
        {
            RingB.Add(AddBottomVert(FVector(R.Pos.X, R.Pos.Y, BottomZ)));
        }
        for (int32 i = 0; i < N; i++)
        {
            // Same fan as the top but not reversed: front face points down.
            Tris.Add(CB);
            Tris.Add(RingB[i]);
            Tris.Add(RingB[(i + 1) % N]);
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

bool ARoadNetworkVisualizer::FindClosestInspectableNode(FVector SearchLocation, float SnapRadiusCM, FVector& OutNodeLocation, int64& OutNodeId)
{
    const float RadiusSq = SnapRadiusCM * SnapRadiusCM;
    float BestJunctionDistSq = RadiusSq;
    float BestAnyDistSq = RadiusSq;
    FVector BestJunctionLoc = FVector::ZeroVector, BestAnyLoc = FVector::ZeroVector;
    int64 BestJunctionId = -1, BestAnyId = -1;

    for (const auto& Pair : CachedNodeLocations)
    {
        const float DistSq = FVector::DistSquaredXY(SearchLocation, Pair.Value);
        if (DistSq >= BestAnyDistSq && DistSq >= BestJunctionDistSq) continue;

        if (DistSq < BestAnyDistSq)
        {
            BestAnyDistSq = DistSq;
            BestAnyLoc = Pair.Value;
            BestAnyId = Pair.Key;
        }

        if (DistSq < BestJunctionDistSq && CachedNetwork)
        {
            const Node* N = CachedNetwork->getNode(Pair.Key);
            if (N && (N->type != Node::PASS_THROUGH || RoadIntersectionUtil::IsIntersectionNode(*N)))
            {
                BestJunctionDistSq = DistSq;
                BestJunctionLoc = Pair.Value;
                BestJunctionId = Pair.Key;
            }
        }
    }

    if (BestJunctionId == -1 && BestAnyId == -1) return false;

    OutNodeLocation = (BestJunctionId != -1) ? BestJunctionLoc : BestAnyLoc;
    OutNodeLocation.Z = 0.0f;
    OutNodeId = (BestJunctionId != -1) ? BestJunctionId : BestAnyId;
    return true;
}

int64 ARoadNetworkVisualizer::ExportNewRoadSegment(int64 StartNodeId, int64 EndNodeId, FVector EndNodeUnrealLoc, int32 Lanes, float SpeedLimit, FString TurnLanes, int32 Layer)
{
    // 1. Handle Node Generation (If the user clicked in empty space)
    int64 FinalEndNodeId = EndNodeId;
    FVector2D EndJsonCoords = ConvertUnrealToJSONCoords(EndNodeUnrealLoc);

    if (FinalEndNodeId == -1)
    {
        FinalEndNodeId = AllocateNodeId();
        AppendNodeRecord(FinalEndNodeId, EndNodeUnrealLoc);
    }

    // 2. Handle Edge Generation
    CurrentMaxEdgeId++;
    FVector StartNodeUnrealLoc = CachedNodeLocations[StartNodeId];
    FVector2D StartJsonCoords = ConvertUnrealToJSONCoords(StartNodeUnrealLoc);

    // Length in meters. 2D on purpose: arc lengths are horizontal everywhere
    // in the sim, and cached node locations of elevated nodes carry a z that
    // must not inflate the edge length.
    double LengthMeters = FVector::Dist2D(StartNodeUnrealLoc, EndNodeUnrealLoc) / 100.0;

    TSharedPtr<FJsonObject> EdgeObj = MakeShareable(new FJsonObject);
    EdgeObj->SetNumberField(TEXT("u"), StartNodeId);
    EdgeObj->SetNumberField(TEXT("v"), FinalEndNodeId);
    EdgeObj->SetNumberField(TEXT("length_m"), LengthMeters);
    EdgeObj->SetNumberField(TEXT("speed_mps"), SpeedLimit); // use custom speedlimit field

    EdgeObj->SetNumberField(TEXT("lanes"), Lanes);
    EdgeObj->SetBoolField(TEXT("oneway"), true);
    EdgeObj->SetStringField(TEXT("highway"), TEXT("residential"));

    // Vertical layer, in the same OSM-style tagging the map exports use so
    // NetworkBuilder::parseEdgeLayer picks it up when the sim reloads the
    // files. Ground roads stay untagged, matching hand-exported data.
    if (Layer != 0)
    {
        EdgeObj->SetNumberField(TEXT("layer"), Layer);
        EdgeObj->SetStringField(Layer > 0 ? TEXT("bridge") : TEXT("tunnel"), TEXT("yes"));
    }

    // turn lanes if needed ("turn_lanes" is the key NetworkBuilder reads)
    if (!TurnLanes.IsEmpty())
    {
        EdgeObj->SetStringField(TEXT("turn_lanes"), TurnLanes);
    }

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

    // Mirror the new edge into the visual network so RefreshRoadVisuals draws
    // it with the full geometry pipeline. The two-point centerline matters:
    // applyVerticalProfile only writes ramps onto stored geometry, so without
    // it an elevated segment would lerp node-to-node with no ramps or
    // junction flat zones. (The live sim gets its copy through
    // SimulationManager::NotifyBackendOfNewRoad and the JSONL reload.)
    if (CachedNetwork)
    {
        std::vector<RoadGeomPoint> Centerline = {
            { StartJsonCoords.X, -StartJsonCoords.Y, 0.0 },
            { EndJsonCoords.X,   -EndJsonCoords.Y,   0.0 }
        };
        CachedNetwork->addDirectedEdge(StartNodeId, FinalEndNodeId, LengthMeters, SpeedLimit, Lanes,
            std::move(Centerline), Layer,
            TurnLane::fromOsmString(TCHAR_TO_UTF8(*TurnLanes), Lanes));

        // The new edge adds a movement at both endpoints, which changes what
        // the neighbouring approaches' inferred turn maps should say.
        CachedNetwork->assignInferredTurnLanes();

        // Endpoints that just became intersections get the same default
        // control (stop/signal/yield) the loader would give them, so the
        // fixture rebuild after the draw has something to plant.
        CachedNetwork->refreshTrafficControlAt(StartNodeId);
        CachedNetwork->refreshTrafficControlAt(FinalEndNodeId);
    }

    return FinalEndNodeId;
}

void ARoadNetworkVisualizer::AppendNodeRecord(int64 NodeId, FVector UnrealLoc)
{
    const FVector2D JsonCoords = ConvertUnrealToJSONCoords(UnrealLoc);

    TSharedPtr<FJsonObject> NodeObj = MakeShareable(new FJsonObject);
    NodeObj->SetNumberField(TEXT("id"), NodeId);
    NodeObj->SetNumberField(TEXT("lon"), 0.0); // Or reverse Mercator projection if needed
    NodeObj->SetNumberField(TEXT("lat"), 0.0);
    NodeObj->SetNumberField(TEXT("x"), JsonCoords.X);
    NodeObj->SetNumberField(TEXT("y"), JsonCoords.Y);
    // traffic_control is null

    FString NodeString;
    TSharedRef<TJsonWriter<>> NodeWriter = TJsonWriterFactory<>::Create(&NodeString, 0);
    FJsonSerializer::Serialize(NodeObj.ToSharedRef(), NodeWriter);

    NodeString.ReplaceInline(TEXT("\n"), TEXT(""));
    NodeString.ReplaceInline(TEXT("\r"), TEXT(""));
    NodeString += TEXT("\n"); // Make it JSONL compliant

    // Append to Nodes file
    FFileHelper::SaveStringToFile(NodeString, *NodesFilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), EFileWrite::FILEWRITE_Append);

    // Cache it so the user can immediately snap to this newly created node,
    // and mirror it into the visual network (backend y is the flipped json y).
    CachedNodeLocations.Add(NodeId, UnrealLoc);
    if (CachedNetwork)
    {
        CachedNetwork->addNode(NodeId, 0.0, 0.0, JsonCoords.X, -JsonCoords.Y);
    }

    if (static_cast<uint64_t>(NodeId) > CurrentMaxNodeId)
    {
        CurrentMaxNodeId = static_cast<uint64_t>(NodeId);
    }
}

bool ARoadNetworkVisualizer::GetNodeLocation(int64 NodeId, FVector& OutLocation) const
{
    if (const FVector* Found = CachedNodeLocations.Find(static_cast<uint64_t>(NodeId)))
    {
        OutLocation = *Found;
        return true;
    }
    return false;
}

void ARoadNetworkVisualizer::GetEdgePolylineUnreal(const Node& FromNode, const Road& Edge, const Node& DestNode, TArray<FVector>& OutPts) const
{
    OutPts.Reset();
    if (Edge.hasCurveGeometry())
    {
        const std::vector<RoadGeomPoint>& Geom = Edge.getGeometry();
        OutPts.Reserve(Geom.size());
        for (const RoadGeomPoint& P : Geom)
        {
            OutPts.Add(FVector((P.x - OriginOffsetX) * 100.0, (P.y - OriginOffsetY) * 100.0, 0.0));
        }
    }
    else
    {
        OutPts.Add(FVector((FromNode.getX() - OriginOffsetX) * 100.0, (FromNode.getY() - OriginOffsetY) * 100.0, 0.0));
        OutPts.Add(FVector((DestNode.getX() - OriginOffsetX) * 100.0, (DestNode.getY() - OriginOffsetY) * 100.0, 0.0));
    }
}

bool ARoadNetworkVisualizer::FindClosestEdge(FVector SearchLocation, float SnapRadiusCM, FVector& OutPointOnEdge, int64& OutU, int64& OutV)
{
    if (!CachedNetwork) return false;

    // Keep the split point far enough from the edge's ends that neither half
    // degenerates and the junction pavement has room. Clicks near a node
    // should have been snapped to it by FindClosestNode first.
    const float MinEndDistCm = 500.0f;

    float BestDistSq = SnapRadiusCM * SnapRadiusCM;
    bool bFound = false;

    TArray<FVector> Pts;
    for (const auto& NodePair : CachedNetwork->getNodes())
    {
        const Node& From = NodePair.second;
        for (const Road& Edge : From.outgoingEdges)
        {
            Node* Dest = CachedNetwork->getNode(Edge.getDest());
            if (!Dest) continue;

            GetEdgePolylineUnreal(From, Edge, *Dest, Pts);
            if (Pts.Num() < 2) continue;

            // Cumulative arc lengths, so hits can be clamped away from ends.
            float TotalLen = 0.0f;
            for (int32 i = 0; i + 1 < Pts.Num(); i++) TotalLen += FVector::Dist2D(Pts[i], Pts[i + 1]);
            if (TotalLen < MinEndDistCm * 2.5f) continue; // too short to split

            float ArcAtSegStart = 0.0f;
            for (int32 i = 0; i + 1 < Pts.Num(); i++)
            {
                const FVector A = Pts[i];
                const FVector B = Pts[i + 1];
                const FVector AB = B - A;
                const float SegLenSq = AB.X * AB.X + AB.Y * AB.Y;
                const float SegLen = FMath::Sqrt(SegLenSq);
                if (SegLen < KINDA_SMALL_NUMBER) continue;

                float T = ((SearchLocation.X - A.X) * AB.X + (SearchLocation.Y - A.Y) * AB.Y) / SegLenSq;
                T = FMath::Clamp(T, 0.0f, 1.0f);

                // Clamp the hit's arc position away from the edge endpoints.
                float Arc = ArcAtSegStart + T * SegLen;
                Arc = FMath::Clamp(Arc, MinEndDistCm, TotalLen - MinEndDistCm);
                T = FMath::Clamp((Arc - ArcAtSegStart) / SegLen, 0.0f, 1.0f);

                const FVector P(A.X + AB.X * T, A.Y + AB.Y * T, 0.0f);
                const float DistSq = FVector::DistSquared2D(SearchLocation, P);
                if (DistSq < BestDistSq)
                {
                    BestDistSq = DistSq;
                    OutPointOnEdge = P;
                    OutU = static_cast<int64>(From.getId());
                    OutV = static_cast<int64>(Edge.getDest());
                    bFound = true;
                }

                ArcAtSegStart += SegLen;
            }
        }
    }

    return bFound;
}

bool ARoadNetworkVisualizer::FindFirstCrossing(const FVector& SegStart, const FVector& SegEnd, const TArray<int64>& IgnoreNodes,
    FVector& OutPoint, int64& OutU, int64& OutV, int64& OutExistingNodeId)
{
    if (!CachedNetwork) return false;

    // Crossings landing this close to an existing node weld to it instead of
    // splitting the edge right next to a junction.
    const float NodeWeldDistCm = 1000.0f; // 10 m

    const FVector2D P0(SegStart.X, SegStart.Y);
    const FVector2D P1(SegEnd.X, SegEnd.Y);
    const FVector2D D = P1 - P0;
    if (D.IsNearlyZero()) return false;

    float BestT = TNumericLimits<float>::Max();
    bool bFound = false;

    TArray<FVector> Pts;
    for (const auto& NodePair : CachedNetwork->getNodes())
    {
        const Node& From = NodePair.second;
        for (const Road& Edge : From.outgoingEdges)
        {
            const int64 U = static_cast<int64>(From.getId());
            const int64 V = static_cast<int64>(Edge.getDest());
            if (IgnoreNodes.Contains(U) || IgnoreNodes.Contains(V)) continue;

            Node* Dest = CachedNetwork->getNode(Edge.getDest());
            if (!Dest) continue;

            GetEdgePolylineUnreal(From, Edge, *Dest, Pts);

            for (int32 i = 0; i + 1 < Pts.Num(); i++)
            {
                const FVector2D Q0(Pts[i].X, Pts[i].Y);
                const FVector2D Q1(Pts[i + 1].X, Pts[i + 1].Y);
                const FVector2D E = Q1 - Q0;

                // Solve P0 + t*D == Q0 + s*E for t, s in (0, 1).
                const float Denom = D.X * E.Y - D.Y * E.X;
                if (FMath::Abs(Denom) < KINDA_SMALL_NUMBER) continue; // parallel

                const FVector2D W = Q0 - P0;
                const float T = (W.X * E.Y - W.Y * E.X) / Denom;
                const float S = (W.X * D.Y - W.Y * D.X) / Denom;

                const float Eps = 1e-4f;
                if (T <= Eps || T >= 1.0f - Eps || S < -Eps || S > 1.0f + Eps) continue;
                if (T >= BestT) continue;

                BestT = T;
                OutPoint = FVector(P0.X + D.X * T, P0.Y + D.Y * T, 0.0f);
                OutU = U;
                OutV = V;
                bFound = true;
            }
        }
    }

    if (!bFound) return false;

    // Weld to the crossed edge's endpoint when the hit is basically on it.
    OutExistingNodeId = -1;
    for (const int64 NodeId : { OutU, OutV })
    {
        FVector NodeLoc;
        if (GetNodeLocation(NodeId, NodeLoc) && FVector::Dist2D(NodeLoc, OutPoint) <= NodeWeldDistCm)
        {
            OutExistingNodeId = NodeId;
            OutPoint = FVector(NodeLoc.X, NodeLoc.Y, 0.0f);
            break;
        }
    }

    return true;
}

// Projects 'P' onto the polyline and splits it there: OutA keeps the points
// before the split plus P; OutB starts at P. Returns the arc fraction of the
// split in [0, 1]. Everything is in raw JSON-file coordinates.
static double SplitJsonPolylineAtPoint(const TArray<FVector2D>& Pts, const FVector2D& P,
    TArray<FVector2D>& OutA, TArray<FVector2D>& OutB)
{
    TArray<double> Cum;
    Cum.Reserve(Pts.Num());
    Cum.Add(0.0);
    for (int32 i = 1; i < Pts.Num(); i++)
    {
        Cum.Add(Cum[i - 1] + FVector2D::Distance(Pts[i - 1], Pts[i]));
    }
    const double Total = Cum.Last();
    if (Total <= 0.0) return 0.5;

    double BestS = 0.0;
    double BestD2 = TNumericLimits<double>::Max();
    for (int32 i = 0; i + 1 < Pts.Num(); i++)
    {
        const FVector2D A = Pts[i];
        const FVector2D AB = Pts[i + 1] - A;
        const double SegLen2 = AB.X * AB.X + AB.Y * AB.Y;
        double T = (SegLen2 > 0.0) ? ((P.X - A.X) * AB.X + (P.Y - A.Y) * AB.Y) / SegLen2 : 0.0;
        T = FMath::Clamp(T, 0.0, 1.0);
        const FVector2D Q = A + AB * T;
        const double D2 = FVector2D::DistSquared(P, Q);
        if (D2 < BestD2)
        {
            BestD2 = D2;
            BestS = Cum[i] + (Cum[i + 1] - Cum[i]) * T;
        }
    }

    // Same end margin as Network::splitDirectedEdge so both stay in agreement.
    const double EndMargin = FMath::Min(1.0, Total * 0.05);
    BestS = FMath::Clamp(BestS, EndMargin, Total - EndMargin);

    OutA.Reset();
    OutB.Reset();
    for (int32 i = 0; i < Pts.Num(); i++)
    {
        if (Cum[i] < BestS) OutA.Add(Pts[i]);
        else if (Cum[i] > BestS) OutB.Add(Pts[i]);
    }
    OutA.Add(P);
    OutB.Insert(P, 0);

    return BestS / Total;
}

bool ARoadNetworkVisualizer::SplitEdgeInFile(int64 U, int64 V, int64 NewNodeId, FVector2D SplitJsonCoords)
{
    TArray<FString> Lines;
    if (!FFileHelper::LoadFileToStringArray(Lines, *EdgesFilePath))
    {
        UE_LOG(LogTemp, Error, TEXT("SplitEdgeInFile: could not read %s"), *EdgesFilePath);
        return false;
    }

    // Cheap substring pre-filter before paying for a JSON parse per line.
    const FString UStr = FString::Printf(TEXT("%lld"), U);
    const FString VStr = FString::Printf(TEXT("%lld"), V);

    for (int32 LineIdx = 0; LineIdx < Lines.Num(); LineIdx++)
    {
        const FString& Line = Lines[LineIdx];
        if (Line.IsEmpty() || !Line.Contains(UStr) || !Line.Contains(VStr)) continue;

        TSharedPtr<FJsonObject> Obj;
        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Line);
        if (!FJsonSerializer::Deserialize(Reader, Obj) || !Obj.IsValid()) continue;

        int64 LineU = 0, LineV = 0;
        if (!Obj->TryGetNumberField(TEXT("u"), LineU) || !Obj->TryGetNumberField(TEXT("v"), LineV)) continue;
        if (LineU != U || LineV != V) continue;

        // Centerline in file coordinates: stored shape, or the node chord.
        TArray<FVector2D> Pts;
        const TArray<TSharedPtr<FJsonValue>>* GeomArray = nullptr;
        if (Obj->TryGetArrayField(TEXT("geometry_xy"), GeomArray))
        {
            for (const TSharedPtr<FJsonValue>& Val : *GeomArray)
            {
                const TSharedPtr<FJsonObject>* PtObj;
                if (Val->TryGetObject(PtObj))
                {
                    Pts.Add(FVector2D((*PtObj)->GetNumberField(TEXT("x")), (*PtObj)->GetNumberField(TEXT("y"))));
                }
            }
        }
        if (Pts.Num() < 2)
        {
            FVector ULoc, VLoc;
            if (!GetNodeLocation(U, ULoc) || !GetNodeLocation(V, VLoc)) return false;
            Pts.Reset();
            Pts.Add(ConvertUnrealToJSONCoords(ULoc));
            Pts.Add(ConvertUnrealToJSONCoords(VLoc));
        }

        TArray<FVector2D> PtsA, PtsB;
        const double Frac = SplitJsonPolylineAtPoint(Pts, SplitJsonCoords, PtsA, PtsB);

        const double OrigLen = Obj->GetNumberField(TEXT("length_m"));
        const double LenA = OrigLen * Frac;
        const double LenB = OrigLen - LenA;

        auto MakeGeomArray = [](const TArray<FVector2D>& InPts)
        {
            TArray<TSharedPtr<FJsonValue>> Arr;
            Arr.Reserve(InPts.Num());
            for (const FVector2D& Pt : InPts)
            {
                TSharedPtr<FJsonObject> PtObj = MakeShareable(new FJsonObject);
                PtObj->SetNumberField(TEXT("x"), Pt.X);
                PtObj->SetNumberField(TEXT("y"), Pt.Y);
                Arr.Add(MakeShareable(new FJsonValueObject(PtObj)));
            }
            return Arr;
        };

        auto SerializeLine = [](const TSharedPtr<FJsonObject>& InObj)
        {
            FString Out;
            TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out, 0);
            FJsonSerializer::Serialize(InObj.ToSharedRef(), Writer);
            Out.ReplaceInline(TEXT("\n"), TEXT(""));
            Out.ReplaceInline(TEXT("\r"), TEXT(""));
            return Out;
        };

        // First half: keep every other field (highway, lanes, turn:lanes, ...)
        // and retarget v; second half is a copy starting at the new node.
        // FJsonObject is shared, so deep-copy via a re-parse for the second line.
        TSharedPtr<FJsonObject> ObjB;
        TSharedRef<TJsonReader<>> ReaderB = TJsonReaderFactory<>::Create(Line);
        FJsonSerializer::Deserialize(ReaderB, ObjB);
        if (!ObjB.IsValid()) return false;

        Obj->SetNumberField(TEXT("v"), NewNodeId);
        Obj->SetNumberField(TEXT("length_m"), LenA);
        Obj->SetArrayField(TEXT("geometry_xy"), MakeGeomArray(PtsA));

        ObjB->SetNumberField(TEXT("u"), NewNodeId);
        ObjB->SetNumberField(TEXT("length_m"), LenB);
        ObjB->SetArrayField(TEXT("geometry_xy"), MakeGeomArray(PtsB));

        Lines[LineIdx] = SerializeLine(Obj);
        Lines.Insert(SerializeLine(ObjB), LineIdx + 1);

        return FFileHelper::SaveStringArrayToFile(Lines, *EdgesFilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    }

    UE_LOG(LogTemp, Warning, TEXT("SplitEdgeInFile: edge %lld -> %lld not found in %s"), U, V, *EdgesFilePath);
    return false;
}

bool ARoadNetworkVisualizer::SplitEdgeForNewNode(int64 U, int64 V, int64 NewNodeId, FVector SplitUnrealLoc)
{
    if (!CachedNetwork) return false;

    const FVector2D JsonCoords = ConvertUnrealToJSONCoords(SplitUnrealLoc);
    const double BackendX = JsonCoords.X;
    const double BackendY = -JsonCoords.Y;

    // Visual network first (it also creates the node), then persistence.
    const bool bFwd = CachedNetwork->splitDirectedEdge(U, V, NewNodeId, BackendX, BackendY);
    const bool bRev = CachedNetwork->splitDirectedEdge(V, U, NewNodeId, BackendX, BackendY);
    if (!bFwd && !bRev) return false;

    // One node record regardless of how many directions were split. Cache and
    // file both use the exact clicked point; the network node already does.
    AppendNodeRecord(NewNodeId, SplitUnrealLoc);

    if (bFwd) SplitEdgeInFile(U, V, NewNodeId, JsonCoords);
    if (bRev) SplitEdgeInFile(V, U, NewNodeId, JsonCoords);

    // The halves ending at the new mid-node face different movements than
    // the original edge did, so refresh the inferred turn maps.
    CachedNetwork->assignInferredTurnLanes();

    // The approaches into U and V now originate at the mid node, so their
    // yield minor-road lists must be recomputed.
    CachedNetwork->refreshTrafficControlAt(U);
    CachedNetwork->refreshTrafficControlAt(V);
    CachedNetwork->refreshTrafficControlAt(NewNodeId);

    return true;
}

TSharedPtr<FJsonObject> ARoadNetworkVisualizer::FindEdgeJson(int64 U, int64 V) const
{
    TArray<FString> Lines;
    if (!FFileHelper::LoadFileToStringArray(Lines, *EdgesFilePath)) return nullptr;

    const FString UStr = FString::Printf(TEXT("%lld"), U);
    const FString VStr = FString::Printf(TEXT("%lld"), V);

    for (const FString& Line : Lines)
    {
        if (Line.IsEmpty() || !Line.Contains(UStr) || !Line.Contains(VStr)) continue;

        TSharedPtr<FJsonObject> Obj;
        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Line);
        if (!FJsonSerializer::Deserialize(Reader, Obj) || !Obj.IsValid()) continue;

        int64 LineU = 0, LineV = 0;
        if (Obj->TryGetNumberField(TEXT("u"), LineU) && Obj->TryGetNumberField(TEXT("v"), LineV)
            && LineU == U && LineV == V)
        {
            return Obj;
        }
    }
    return nullptr;
}

bool ARoadNetworkVisualizer::GetEdgeInfo(int64 EdgeId, FRoadEdgeInfo& OutInfo)
{
    if (!CachedNetwork) return false;

    const TPair<uint64_t, uint64_t>* Nodes = EdgeIdToNodes.Find(static_cast<uint64_t>(EdgeId));
    if (!Nodes) return false;

    Node* From = CachedNetwork->getNode(Nodes->Key);
    if (!From) return false;

    const Road* Edge = nullptr;
    for (const Road& E : From->outgoingEdges)
    {
        if (E.getEdgeId() == static_cast<uint64_t>(EdgeId)) { Edge = &E; break; }
    }
    if (!Edge) return false;

    OutInfo.EdgeId = EdgeId;
    OutInfo.NodeU = static_cast<int64>(Nodes->Key);
    OutInfo.NodeV = static_cast<int64>(Nodes->Value);
    OutInfo.Lanes = Edge->getLanes();
    OutInfo.SpeedLimitMps = static_cast<float>(Edge->getSpeedLimit());
    OutInfo.LengthMeters = static_cast<float>(Edge->getLength());
    OutInfo.Layer = Edge->getLayer();

    // Two-way = an opposite directed edge exists between the same nodes.
    OutInfo.bTwoWay = false;
    if (Node* Dest = CachedNetwork->getNode(Nodes->Value))
    {
        for (const Road& E : Dest->outgoingEdges)
        {
            if (E.getDest() == Nodes->Key) { OutInfo.bTwoWay = true; break; }
        }
    }

    // Turn lanes: prefer the explicit tag in the JSONL record (the datasets
    // use "turn_lanes"; "turn:lanes" is what older editor builds wrote). Most
    // OSM edges carry null there, so fall back to the per-lane map the
    // network inferred from the movements available at the destination node.
    OutInfo.TurnLanes.Empty();
    OutInfo.bTurnLanesInferred = false;
    if (TSharedPtr<FJsonObject> EdgeJson = FindEdgeJson(OutInfo.NodeU, OutInfo.NodeV))
    {
        for (const TCHAR* Key : { TEXT("turn_lanes"), TEXT("turn_lanes_forward"), TEXT("turn:lanes") })
        {
            if (EdgeJson->TryGetStringField(Key, OutInfo.TurnLanes) && !OutInfo.TurnLanes.IsEmpty()) break;
        }
    }
    if (Edge->hasLaneTurnData())
    {
        // A partial tag like "left||" has its unmarked lanes completed by
        // the network (flagged TurnLane::Inferred); show the completed map
        // rather than the raw tag, and label it inferred so the user knows
        // some of it is a suggestion, not surveyed data.
        bool bAnyFilled = false;
        for (uint8_t Mask : Edge->getLaneTurns()) bAnyFilled |= (Mask & TurnLane::Inferred) != 0;
        if (OutInfo.TurnLanes.IsEmpty() || bAnyFilled)
        {
            OutInfo.TurnLanes = UTF8_TO_TCHAR(TurnLane::toOsmString(Edge->getLaneTurns()).c_str());
            OutInfo.bTurnLanesInferred = !Edge->isLaneTurnsFromOsm() || bAnyFilled;
        }
    }

    return true;
}

bool ARoadNetworkVisualizer::UpdateEdgesInFile(const TArray<FEdgeFileUpdate>& Updates, int32 Lanes, float SpeedMps, int32 Layer)
{
    if (Updates.Num() == 0) return true;

    TArray<FString> Lines;
    if (!FFileHelper::LoadFileToStringArray(Lines, *EdgesFilePath))
    {
        UE_LOG(LogTemp, Error, TEXT("UpdateEdgesInFile: could not read %s"), *EdgesFilePath);
        return false;
    }

    // Cheap substring prefilter per update, same as the old single-edge scan.
    TArray<FString> UStrs, VStrs;
    UStrs.Reserve(Updates.Num());
    VStrs.Reserve(Updates.Num());
    for (const FEdgeFileUpdate& Update : Updates)
    {
        UStrs.Add(FString::Printf(TEXT("%lld"), Update.U));
        VStrs.Add(FString::Printf(TEXT("%lld"), Update.V));
    }

    TBitArray<> Applied(false, Updates.Num());
    int32 Remaining = Updates.Num();

    for (int32 LineIdx = 0; LineIdx < Lines.Num() && Remaining > 0; LineIdx++)
    {
        const FString& Line = Lines[LineIdx];
        if (Line.IsEmpty()) continue;

        for (int32 UpdateIdx = 0; UpdateIdx < Updates.Num(); UpdateIdx++)
        {
            if (Applied[UpdateIdx]) continue;
            if (!Line.Contains(UStrs[UpdateIdx]) || !Line.Contains(VStrs[UpdateIdx])) continue;

            TSharedPtr<FJsonObject> Obj;
            TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Line);
            if (!FJsonSerializer::Deserialize(Reader, Obj) || !Obj.IsValid()) break;

            int64 LineU = 0, LineV = 0;
            if (!Obj->TryGetNumberField(TEXT("u"), LineU) || !Obj->TryGetNumberField(TEXT("v"), LineV)) break;
            if (LineU != Updates[UpdateIdx].U || LineV != Updates[UpdateIdx].V) continue;

            Obj->SetNumberField(TEXT("lanes"), Lanes);
            Obj->SetNumberField(TEXT("speed_mps"), SpeedMps);
            // "turn_lanes" is the key the datasets and NetworkBuilder read;
            // scrub the alternate spellings so one authoritative value remains.
            // An empty edit clears the tag entirely, handing the edge back to
            // assignInferredTurnLanes.
            Obj->RemoveField(TEXT("turn:lanes"));
            Obj->RemoveField(TEXT("turn_lanes_forward"));
            if (Updates[UpdateIdx].TurnLanes.IsEmpty())
            {
                Obj->RemoveField(TEXT("turn_lanes"));
            }
            else
            {
                Obj->SetStringField(TEXT("turn_lanes"), Updates[UpdateIdx].TurnLanes);
            }

            // Always written explicitly: parseEdgeLayer prefers "layer" over the
            // bridge/tunnel tag fallback, so grounding an OSM bridge (layer 0)
            // sticks even when the line keeps its original bridge tag.
            Obj->SetNumberField(TEXT("layer"), Layer);

            FString Out;
            TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out, 0);
            FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
            Out.ReplaceInline(TEXT("\n"), TEXT(""));
            Out.ReplaceInline(TEXT("\r"), TEXT(""));
            Lines[LineIdx] = Out;

            Applied[UpdateIdx] = true;
            Remaining--;
            break; // one JSONL line holds exactly one directed edge
        }
    }

    for (int32 UpdateIdx = 0; UpdateIdx < Updates.Num(); UpdateIdx++)
    {
        if (!Applied[UpdateIdx])
        {
            UE_LOG(LogTemp, Warning, TEXT("UpdateEdgesInFile: edge %lld -> %lld not found in %s"),
                Updates[UpdateIdx].U, Updates[UpdateIdx].V, *EdgesFilePath);
        }
    }

    if (Remaining == Updates.Num())
    {
        return false; // nothing matched; don't rewrite the file for no reason
    }

    const bool bSaved = FFileHelper::SaveStringArrayToFile(Lines, *EdgesFilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    return bSaved && Remaining == 0;
}

bool ARoadNetworkVisualizer::RemoveEdgeInFile(int64 U, int64 V)
{
    TArray<FString> Lines;
    if (!FFileHelper::LoadFileToStringArray(Lines, *EdgesFilePath))
    {
        UE_LOG(LogTemp, Error, TEXT("RemoveEdgeInFile: could not read %s"), *EdgesFilePath);
        return false;
    }

    const FString UStr = FString::Printf(TEXT("%lld"), U);
    const FString VStr = FString::Printf(TEXT("%lld"), V);

    for (int32 LineIdx = 0; LineIdx < Lines.Num(); LineIdx++)
    {
        const FString& Line = Lines[LineIdx];
        if (Line.IsEmpty() || !Line.Contains(UStr) || !Line.Contains(VStr)) continue;

        TSharedPtr<FJsonObject> Obj;
        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Line);
        if (!FJsonSerializer::Deserialize(Reader, Obj) || !Obj.IsValid()) continue;

        int64 LineU = 0, LineV = 0;
        if (!Obj->TryGetNumberField(TEXT("u"), LineU) || !Obj->TryGetNumberField(TEXT("v"), LineV)) continue;
        if (LineU != U || LineV != V) continue;

        Lines.RemoveAt(LineIdx);
        return FFileHelper::SaveStringArrayToFile(Lines, *EdgesFilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    }

    UE_LOG(LogTemp, Warning, TEXT("RemoveEdgeInFile: edge %lld -> %lld not found in %s"), U, V, *EdgesFilePath);
    return false;
}

bool ARoadNetworkVisualizer::RemoveNodeInFile(int64 NodeId)
{
    TArray<FString> Lines;
    if (!FFileHelper::LoadFileToStringArray(Lines, *NodesFilePath))
    {
        UE_LOG(LogTemp, Error, TEXT("RemoveNodeInFile: could not read %s"), *NodesFilePath);
        return false;
    }

    const FString IdStr = FString::Printf(TEXT("%lld"), NodeId);

    for (int32 LineIdx = 0; LineIdx < Lines.Num(); LineIdx++)
    {
        const FString& Line = Lines[LineIdx];
        if (Line.IsEmpty() || !Line.Contains(IdStr)) continue;

        TSharedPtr<FJsonObject> Obj;
        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Line);
        if (!FJsonSerializer::Deserialize(Reader, Obj) || !Obj.IsValid()) continue;

        int64 LineId = 0;
        if (!Obj->TryGetNumberField(TEXT("id"), LineId) || LineId != NodeId) continue;

        Lines.RemoveAt(LineIdx);
        return FFileHelper::SaveStringArrayToFile(Lines, *NodesFilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    }

    UE_LOG(LogTemp, Warning, TEXT("RemoveNodeInFile: node %lld not found in %s"), NodeId, *NodesFilePath);
    return false;
}

bool ARoadNetworkVisualizer::DeleteRoad(int64 U, int64 V, bool bBothDirections)
{
    if (!CachedNetwork) return false;

    const bool bFwd = CachedNetwork->removeDirectedEdge(U, V);
    const bool bRev = bBothDirections && CachedNetwork->removeDirectedEdge(V, U);
    if (!bFwd && !bRev) return false;

    if (bFwd) RemoveEdgeInFile(U, V);
    if (bRev) RemoveEdgeInFile(V, U);

    // A node whose last road just vanished has no reason to exist anymore:
    // scrub it from the network, the snap cache, and the nodes file.
    for (int64 NodeId : { U, V })
    {
        if (CachedNetwork->removeNodeIfIsolated(static_cast<uint64_t>(NodeId)))
        {
            CachedNodeLocations.Remove(static_cast<uint64_t>(NodeId));
            RemoveNodeInFile(NodeId);
        }
    }

    // Removing a movement changes what the surviving approaches at both
    // endpoints may do, so refresh the inferred turn maps.
    CachedNetwork->assignInferredTurnLanes();

    // Losing an approach can demote an endpoint back to pass-through or
    // change which surviving road is the minor one.
    CachedNetwork->refreshTrafficControlAt(U);
    CachedNetwork->refreshTrafficControlAt(V);

    // Full rebuild also refreshes junction pavement and tapers at both ends
    // and drops the deleted edge from the hit-test / edit maps.
    RefreshRoadVisuals();
    return true;
}

bool ARoadNetworkVisualizer::UpdateRoadProperties(int64 U, int64 V, int32 Lanes, float SpeedMps, const FString& TurnLanes, int32 Layer, bool bBothDirections)
{
    if (!CachedNetwork) return false;

    const int32 SafeLanes = FMath::Max(1, Lanes);
    const float SafeSpeed = FMath::Max(0.5f, SpeedMps);
    const int32 SafeLayer = FMath::Clamp(Layer, -5, 5); // same range parseEdgeLayer accepts

    auto ApplyToNetwork = [&](int64 A, int64 B, const FString& DirTurnLanes) -> bool
    {
        Node* From = CachedNetwork->getNode(static_cast<uint64_t>(A));
        if (!From) return false;
        for (Road& E : From->outgoingEdges)
        {
            if (E.getDest() == static_cast<uint64_t>(B))
            {
                E.setLanes(SafeLanes);
                E.setSpeedLimit(SafeSpeed);
                E.setLayer(SafeLayer);
                // Explicit turn lanes are authoritative; an empty edit hands
                // the edge back to the inference pass below.
                if (DirTurnLanes.IsEmpty())
                    E.clearLaneTurns();
                else
                    E.setLaneTurns(TurnLane::fromOsmString(TCHAR_TO_UTF8(*DirTurnLanes), SafeLanes), true);
                return true;
            }
        }
        return false;
    };

    // turn:lanes is ordered in the direction of travel, so the opposite
    // edge gets the mirrored string, not a verbatim copy.
    const FString MirroredTurnLanes = RoadTurnLaneOptions::MirrorTurnLanes(TurnLanes);

    TArray<FEdgeFileUpdate> FileUpdates;
    if (ApplyToNetwork(U, V, TurnLanes))
    {
        FileUpdates.Add({ U, V, TurnLanes });
    }
    if (bBothDirections && ApplyToNetwork(V, U, MirroredTurnLanes))
    {
        FileUpdates.Add({ V, U, MirroredTurnLanes });
    }

    const bool bAny = FileUpdates.Num() > 0;
    if (bAny)
    {
        // Both directions land in one file load + one save.
        UpdateEdgesInFile(FileUpdates, SafeLanes, SafeSpeed, SafeLayer);
    }

    if (bAny)
    {
        // Lane-count and turn edits invalidate the inferred maps on this
        // edge and its neighbouring approaches; recompute them (explicit
        // maps set above are untouched).
        CachedNetwork->assignInferredTurnLanes();

        // Speed/lane edits feed the yield minor-road comparison at both
        // ends, so right-of-way must track the new values.
        CachedNetwork->refreshTrafficControlAt(static_cast<uint64_t>(U));
        CachedNetwork->refreshTrafficControlAt(static_cast<uint64_t>(V));

        // Lane count changes road width and the layer changes elevation, so
        // rebuild (re-runs the elevation pass and refreshes tapers and
        // junction pavement at both ends).
        RefreshRoadVisuals();
    }
    return bAny;
}