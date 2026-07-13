#include "TrafficControlVisualizer.h"
#include "TrafficSimulation.h"
#include "intersection_geometry.h"
#include "network.h"
#include "node.h"
#include "road.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include <algorithm>

namespace
{
    FTransform HiddenCopy(const FTransform& T)
    {
        return FTransform(T.GetRotation(), T.GetTranslation(), FVector::ZeroVector);
    }
}

ATrafficControlVisualizer::ATrafficControlVisualizer()
{
    PrimaryActorTick.bCanEverTick = false;

    auto MakeHISM = [this](const TCHAR* Name) -> UHierarchicalInstancedStaticMeshComponent*
    {
        UHierarchicalInstancedStaticMeshComponent* HISM =
            CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(Name);
        HISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        HISM->SetGenerateOverlapEvents(false);
        HISM->SetCastShadow(false);
        return HISM;
    };

    PoleHISM = MakeHISM(TEXT("PoleHISM"));
    RootComponent = PoleHISM;

    SignalHeadHISM = MakeHISM(TEXT("SignalHeadHISM"));
    SignalHeadHISM->SetupAttachment(RootComponent);

    LampRedHISM = MakeHISM(TEXT("LampRedHISM"));
    LampRedHISM->SetupAttachment(RootComponent);

    LampYellowHISM = MakeHISM(TEXT("LampYellowHISM"));
    LampYellowHISM->SetupAttachment(RootComponent);

    LampGreenHISM = MakeHISM(TEXT("LampGreenHISM"));
    LampGreenHISM->SetupAttachment(RootComponent);

    SignFaceMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("SignFaceMesh"));
    SignFaceMesh->SetupAttachment(RootComponent);
    SignFaceMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    SignFaceMesh->SetCastShadow(false);

    // Constructor-time hard references so the engine shapes and base material
    // get cooked into packaged builds (a runtime LoadObject would be invisible
    // to the cooker -- same rationale as the road visualizer's junction
    // material).
    static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(
        TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(
        TEXT("/Engine/BasicShapes/Cube.Cube"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(
        TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> BasicMaterialFinder(
        TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

    if (CylinderFinder.Succeeded()) PoleHISM->SetStaticMesh(CylinderFinder.Object);
    if (CubeFinder.Succeeded()) SignalHeadHISM->SetStaticMesh(CubeFinder.Object);
    if (SphereFinder.Succeeded())
    {
        LampRedHISM->SetStaticMesh(SphereFinder.Object);
        LampYellowHISM->SetStaticMesh(SphereFinder.Object);
        LampGreenHISM->SetStaticMesh(SphereFinder.Object);
    }
    if (BasicMaterialFinder.Succeeded()) BasicShapeMaterialBase = BasicMaterialFinder.Object;
}

UHierarchicalInstancedStaticMeshComponent* ATrafficControlVisualizer::LampHISM(int32 Color) const
{
    switch (Color)
    {
    case TrafficLightRenderState::YELLOW: return LampYellowHISM;
    case TrafficLightRenderState::GREEN:  return LampGreenHISM;
    default:                              return LampRedHISM;
    }
}

void ATrafficControlVisualizer::ApplyDefaultMaterials()
{
    auto Colored = [this](UMaterialInterface* Override, const FLinearColor& Color) -> UMaterialInterface*
    {
        if (Override) return Override;
        if (!BasicShapeMaterialBase) return nullptr;
        UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(BasicShapeMaterialBase, this);
        MID->SetVectorParameterValue(FName("Color"), Color);
        return MID;
    };

    if (UMaterialInterface* M = Colored(PoleMaterial, FLinearColor(0.05f, 0.05f, 0.06f)))
    {
        PoleHISM->SetMaterial(0, M);
    }
    if (UMaterialInterface* M = Colored(HeadMaterial, FLinearColor(0.02f, 0.02f, 0.025f)))
    {
        SignalHeadHISM->SetMaterial(0, M);
    }
    if (UMaterialInterface* M = Colored(LampRedMaterial, FLinearColor(1.0f, 0.02f, 0.02f)))
    {
        LampRedHISM->SetMaterial(0, M);
    }
    if (UMaterialInterface* M = Colored(LampYellowMaterial, FLinearColor(1.0f, 0.65f, 0.03f)))
    {
        LampYellowHISM->SetMaterial(0, M);
    }
    if (UMaterialInterface* M = Colored(LampGreenMaterial, FLinearColor(0.03f, 1.0f, 0.15f)))
    {
        LampGreenHISM->SetMaterial(0, M);
    }
    if (UMaterialInterface* M = Colored(SignRedMaterial, FLinearColor(0.55f, 0.01f, 0.02f)))
    {
        SignFaceMesh->SetMaterial(0, M);
    }
    if (UMaterialInterface* M = Colored(SignWhiteMaterial, FLinearColor(0.9f, 0.9f, 0.9f)))
    {
        SignFaceMesh->SetMaterial(1, M);
    }
}

void ATrafficControlVisualizer::BuildTrafficControls(Network* RoadNetwork, double InOriginOffsetX, double InOriginOffsetY)
{
    if (!RoadNetwork) return;

    OriginOffsetX = InOriginOffsetX;
    OriginOffsetY = InOriginOffsetY;

    PoleHISM->ClearInstances();
    SignalHeadHISM->ClearInstances();
    LampRedHISM->ClearInstances();
    LampYellowHISM->ClearInstances();
    LampGreenHISM->ClearInstances();
    SignFaceMesh->ClearAllMeshSections();
    SignalVisuals.Empty();

    ApplyDefaultMaterials();

    // Stop-sign faces accumulate into one two-section mesh.
    TArray<FVector> RedVerts, RedNormals, WhiteVerts, WhiteNormals;
    TArray<int32> RedTris, WhiteTris;
    TArray<FVector2D> RedUVs, WhiteUVs;

    for (const auto& NodePair : RoadNetwork->getNodes())
    {
        const Node& JunctionNode = NodePair.second;
        if (JunctionNode.type == Node::PASS_THROUGH) continue;

        const uint64 NodeId = JunctionNode.getId();

        TSet<uint64> SeenOrigins;
        for (uint64_t PredId : JunctionNode.incomingEdgeNodeIds)
        {
            if (SeenOrigins.Contains(PredId)) continue;
            SeenOrigins.Add(PredId);

            // A yield node only controls its minor approaches; the major road
            // flows freely and gets no sign.
            if (JunctionNode.type == Node::YIELD_STOP)
            {
                const auto& Minor = JunctionNode.minorRoadOriginIds;
                if (std::find(Minor.begin(), Minor.end(), PredId) == Minor.end()) continue;
            }

            Node* PredNode = RoadNetwork->getNode(PredId);
            if (!PredNode) continue;

            const Road* Edge = nullptr;
            for (const Road& E : PredNode->outgoingEdges)
            {
                if (E.getDest() == NodeId) { Edge = &E; break; }
            }
            if (!Edge || Edge->getLength() <= 0.0) continue;

            // Anchor at the stop line: the same arc position the physics
            // engine brakes for and where the road visuals stop short of the
            // junction box.
            const double EdgeLen = Edge->getLength();
            const double Dist = (double)RoadIntersectionUtil::GetStopLineArcPos(
                RoadNetwork, *Edge, JunctionNode, RoadIntersectionUtil::MedianGapMeters);

            double PX, PY, TX, TY, PZ;
            if (!Edge->samplePointAt(Dist, PX, PY, TX, TY, PZ))
            {
                // Straight fallback, mirroring the vehicle renderer.
                const double DX = JunctionNode.getX() - PredNode->getX();
                const double DY = JunctionNode.getY() - PredNode->getY();
                const double Len = FMath::Sqrt(DX * DX + DY * DY);
                if (Len < 0.0001) continue;
                const double T = Dist / EdgeLen;
                PX = PredNode->getX() + T * DX;
                PY = PredNode->getY() + T * DY;
                PZ = PredNode->getZ() + T * (JunctionNode.getZ() - PredNode->getZ());
                TX = DX / Len;
                TY = DY / Len;
            }

            // Roadside point: lanes span MedianGap .. MedianGap + lanes*width
            // to the (-TY, TX) side of the centerline (see the lane offset in
            // TrafficSimulation::GetVehicleRenderStates), so the fixture goes
            // just beyond the outer lane edge.
            const double Lateral = RoadIntersectionUtil::MedianGapMeters
                + Edge->getLanes() * RoadIntersectionUtil::LaneWidthMeters
                + RoadsideMarginMeters;
            const double BX = PX + (-TY) * Lateral;
            const double BY = PY + (TX)*Lateral;

            const FVector BaseLoc(
                (BX - OriginOffsetX) * 100.0,
                (BY - OriginOffsetY) * 100.0,
                PZ * 100.0);
            const FVector Forward(FVector((float)TX, (float)TY, 0.0f).GetSafeNormal2D());
            if (Forward.IsNearlyZero()) continue;

            if (JunctionNode.type == Node::TRAFFIC_LIGHT)
            {
                AddSignalFixture(NodeId, PredId, BaseLoc, Forward);
            }
            else // FOUR_WAY_STOP or minor approach of YIELD_STOP
            {
                AddStopSign(BaseLoc, Forward,
                    RedVerts, RedTris, RedNormals, RedUVs,
                    WhiteVerts, WhiteTris, WhiteNormals, WhiteUVs);
            }
        }
    }

    if (RedVerts.Num() > 0)
    {
        SignFaceMesh->CreateMeshSection(0, RedVerts, RedTris, RedNormals, RedUVs,
            TArray<FColor>(), TArray<FProcMeshTangent>(), false);
        SignFaceMesh->CreateMeshSection(1, WhiteVerts, WhiteTris, WhiteNormals, WhiteUVs,
            TArray<FColor>(), TArray<FProcMeshTangent>(), false);
    }

    UE_LOG(LogTemp, Log, TEXT("TrafficControlVisualizer: %d signal fixtures, %d stop signs."),
        SignalHeadHISM->GetInstanceCount(), RedVerts.Num() / 8);
}

void ATrafficControlVisualizer::AddSignalFixture(uint64 NodeId, uint64 ApproachOriginId,
    const FVector& BaseLoc, const FVector& Forward)
{
    const float YawDeg = FMath::RadiansToDegrees(FMath::Atan2(Forward.Y, Forward.X));
    const FRotator Yaw(0.0f, YawDeg, 0.0f);

    // Pole (engine cylinder: 100 cm tall, pivot centered).
    PoleHISM->AddInstance(FTransform(
        FRotator::ZeroRotator,
        BaseLoc + FVector(0, 0, PoleHeightCm * 0.5f),
        FVector(PoleDiameterCm / 100.0f, PoleDiameterCm / 100.0f, PoleHeightCm / 100.0f)));

    // Head housing on top of the pole (engine cube: 100 cm, pivot centered).
    const FVector HeadCenter = BaseLoc + FVector(0, 0, PoleHeightCm + HeadHeightCm * 0.5f);
    SignalHeadHISM->AddInstance(FTransform(
        Yaw,
        HeadCenter,
        FVector(HeadDepthCm / 100.0f, HeadWidthCm / 100.0f, HeadHeightCm / 100.0f)));

    // Lamps sit proud of the face pointing back at oncoming traffic.
    const FVector Facing = -Forward;
    const FVector LampBase = HeadCenter + Facing * (HeadDepthCm * 0.5f + LampDiameterCm * 0.25f);
    const FVector LampScale(LampDiameterCm / 100.0f);

    FSignalLampSet LampSet;
    LampSet.ApproachOriginId = ApproachOriginId;

    const float ZOffset[3] = { LampSpacingCm, 0.0f, -LampSpacingCm }; // red top, yellow, green
    for (int32 Color = 0; Color < 3; Color++)
    {
        const FTransform T(FRotator::ZeroRotator, LampBase + FVector(0, 0, ZOffset[Color]), LampScale);
        LampSet.LampTransform[Color] = T;

        // Start red-only until the sim reports a phase.
        const bool bVisible = (Color == TrafficLightRenderState::RED);
        LampSet.LampIndex[Color] = LampHISM(Color)->AddInstance(bVisible ? T : HiddenCopy(T));
    }

    SignalVisuals.FindOrAdd(NodeId).Approaches.Add(LampSet);
}

void ATrafficControlVisualizer::AddStopSign(const FVector& BaseLoc, const FVector& Forward,
    TArray<FVector>& RedVerts, TArray<int32>& RedTris, TArray<FVector>& RedNormals, TArray<FVector2D>& RedUVs,
    TArray<FVector>& WhiteVerts, TArray<int32>& WhiteTris, TArray<FVector>& WhiteNormals, TArray<FVector2D>& WhiteUVs) const
{
    // Post.
    PoleHISM->AddInstance(FTransform(
        FRotator::ZeroRotator,
        BaseLoc + FVector(0, 0, SignPostHeightCm * 0.5f),
        FVector(SignPostDiameterCm / 100.0f, SignPostDiameterCm / 100.0f, SignPostHeightCm / 100.0f)));

    // Octagon face near the top of the post, flat-edge down (22.5 deg start),
    // facing oncoming traffic. The white inset floats slightly in front and
    // behind the red plate so both read without z-fighting.
    const FVector FaceCenter = BaseLoc + FVector(0, 0, SignPostHeightCm - SignFaceRadiusCm);

    AppendPolygonFace(FaceCenter - Forward * 1.5f, Forward, SignFaceRadiusCm, 8, 22.5f,
        RedVerts, RedTris, RedNormals, RedUVs);
    AppendPolygonFace(FaceCenter - Forward * 3.0f, Forward, SignFaceRadiusCm * 0.62f, 8, 22.5f,
        WhiteVerts, WhiteTris, WhiteNormals, WhiteUVs);
}

void ATrafficControlVisualizer::AppendPolygonFace(const FVector& Center, const FVector& Forward,
    float RadiusCm, int32 NumSides, float FirstVertexAngleDeg,
    TArray<FVector>& Verts, TArray<int32>& Tris, TArray<FVector>& Normals, TArray<FVector2D>& UVs)
{
    const FVector Side(-Forward.Y, Forward.X, 0.0f);
    const FVector Up(0.0f, 0.0f, 1.0f);

    const int32 Base = Verts.Num();
    for (int32 i = 0; i < NumSides; i++)
    {
        const float A = FMath::DegreesToRadians(FirstVertexAngleDeg + i * (360.0f / NumSides));
        const float C = FMath::Cos(A);
        const float S = FMath::Sin(A);
        Verts.Add(Center + (Side * C + Up * S) * RadiusCm);
        Normals.Add(-Forward);
        UVs.Add(FVector2D(0.5f + 0.5f * C, 0.5f - 0.5f * S));
    }

    // Double-sided fan: emit both windings so the plate is visible from the
    // front and the back regardless of approach orientation.
    for (int32 i = 1; i < NumSides - 1; i++)
    {
        Tris.Add(Base); Tris.Add(Base + i); Tris.Add(Base + i + 1);
        Tris.Add(Base); Tris.Add(Base + i + 1); Tris.Add(Base + i);
    }
}

void ATrafficControlVisualizer::UpdateLightStates(const std::vector<TrafficLightRenderState>& LightStates)
{
    bool bDirty[3] = { false, false, false };

    for (const TrafficLightRenderState& State : LightStates)
    {
        FSignalNodeVisual* Visual = SignalVisuals.Find(State.nodeId);
        if (!Visual) continue;

        if (Visual->LastColor[0] == State.axisColor[0] &&
            Visual->LastColor[1] == State.axisColor[1])
        {
            continue; // nothing changed at this light
        }
        Visual->LastColor[0] = State.axisColor[0];
        Visual->LastColor[1] = State.axisColor[1];

        for (const FSignalLampSet& LampSet : Visual->Approaches)
        {
            // Which axis is this approach on? Unknown approaches show red.
            uint8 Color = TrafficLightRenderState::RED;
            for (int32 Axis = 0; Axis < 2; Axis++)
            {
                const auto& Origins = State.axisOrigins[Axis];
                if (std::find(Origins.begin(), Origins.end(), LampSet.ApproachOriginId) != Origins.end())
                {
                    Color = State.axisColor[Axis];
                    break;
                }
            }

            for (int32 Lamp = 0; Lamp < 3; Lamp++)
            {
                if (LampSet.LampIndex[Lamp] == INDEX_NONE) continue;
                const FTransform& T = (Lamp == Color)
                    ? LampSet.LampTransform[Lamp]
                    : HiddenCopy(LampSet.LampTransform[Lamp]);
                LampHISM(Lamp)->UpdateInstanceTransform(LampSet.LampIndex[Lamp], T,
                    /*bWorldSpace=*/false, /*bMarkRenderStateDirty=*/false, /*bTeleport=*/true);
                bDirty[Lamp] = true;
            }
        }
    }

    for (int32 Lamp = 0; Lamp < 3; Lamp++)
    {
        if (bDirty[Lamp]) LampHISM(Lamp)->MarkRenderStateDirty();
    }
}
