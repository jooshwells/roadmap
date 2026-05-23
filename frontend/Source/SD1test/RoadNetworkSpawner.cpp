#include "RoadNetworkSpawner.h"
#include "Misc/Paths.h"
#include "Engine/World.h"
#include "Components/SplineComponent.h"

ARoadNetworkSpawner::ARoadNetworkSpawner()
{
    PrimaryActorTick.bCanEverTick = false;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Public entry points
// ─────────────────────────────────────────────────────────────────────────────

void ARoadNetworkSpawner::GenerateRoadNetwork()
{
    if (!RoadActorClass)
    {
        UE_LOG(LogTemp, Error, TEXT("RoadNetworkSpawner: RoadActorClass is not assigned in the Details panel."));
        return;
    }

    UWorld* World = GetWorld();
    if (!World) return;

    // ── 1. Build the network from the two JSONL files ─────────────────
    const FString ContentDir = FPaths::ProjectContentDir();
    const std::string NodePath = TCHAR_TO_UTF8(*(ContentDir / NodesJsonLPath));
    const std::string EdgePath = TCHAR_TO_UTF8(*(ContentDir / EdgesJsonLPath));

    Network RoadNetwork = NetworkBuilder::buildNetworkFromJSONL(NodePath, EdgePath);

    UE_LOG(LogTemp, Log, TEXT("RoadNetworkSpawner: Network built. Walking edges to spawn roads..."));

    ClearGeneratedRoads();

    FActorSpawnParameters SpawnParams;
    SpawnParams.Owner = this;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    int32 RoadsSpawned = 0;
    int32 EdgeIndex = 0;

    // ── 2. Walk every node → every outgoing edge ──────────────────────
    // Network::nodes is private, so we iterate by ID.
    // numNodes is also private — we rely on getNode() returning nullptr to stop.
    // Your data uses sequential IDs starting at 1.
    for (int NodeId = 1; ; ++NodeId)
    {
        Node* FromNode = RoadNetwork.getNode(NodeId);
        if (!FromNode) break;   // Past the last node

        const FVector FromPos = NodeToUEPosition(FromNode);

        for (Road& Edge : FromNode->outgoingEdges)
        {
            const int ToId = Edge.getDest();
            Node* ToNode = RoadNetwork.getNode(ToId);
            if (!ToNode)
            {
                UE_LOG(LogTemp, Warning,
                    TEXT("RoadNetworkSpawner: Edge %d->%d references missing destination node."),
                    NodeId, ToId);
                continue;
            }

            const FVector ToPos = NodeToUEPosition(ToNode);

            // ── Catmull-Rom tangents ──────────────────────────────────
            // For the start tangent: look for any neighbour of FromNode that isn't ToNode
            // For the end tangent:   look for any neighbour of ToNode   that isn't FromNode
            const FVector* StartPrev = nullptr;
            const FVector* EndNext = nullptr;
            FVector        StartPrevVec, EndNextVec;

            for (Road& NeighEdge : FromNode->outgoingEdges)
            {
                if (NeighEdge.getDest() != ToId)
                {
                    Node* Prev = RoadNetwork.getNode(NeighEdge.getDest());
                    if (Prev)
                    {
                        StartPrevVec = NodeToUEPosition(Prev);
                        StartPrev = &StartPrevVec;
                        break;
                    }
                }
            }

            for (Road& NeighEdge : ToNode->outgoingEdges)
            {
                if (NeighEdge.getDest() != NodeId)
                {
                    Node* Next = RoadNetwork.getNode(NeighEdge.getDest());
                    if (Next)
                    {
                        EndNextVec = NodeToUEPosition(Next);
                        EndNext = &EndNextVec;
                        break;
                    }
                }
            }

            const FVector StartTangent = ComputeCatmullRomTangent(StartPrev, FromPos, &ToPos, CatmullRomTension);
            const FVector EndTangent = ComputeCatmullRomTangent(&FromPos, ToPos, EndNext, CatmullRomTension);

            // ── Spawn road actor at the FROM position ─────────────────
            FTransform SpawnTransform(FRotator::ZeroRotator, FromPos);
            AActor* RoadActor = World->SpawnActor<AActor>(RoadActorClass, SpawnTransform, SpawnParams);
            if (!RoadActor)
            {
                UE_LOG(LogTemp, Warning,
                    TEXT("RoadNetworkSpawner: SpawnActor failed for edge %d->%d."), NodeId, ToId);
                continue;
            }

            RoadActor->SetActorLabel(
                FString::Printf(TEXT("Road_%d_%d_%d"), EdgeIndex++, NodeId, ToId));
            SpawnedRoads.Add(RoadActor);

            // ── Configure the spline ──────────────────────────────────
            USplineComponent* Spline = RoadActor->FindComponentByClass<USplineComponent>();
            if (!Spline)
            {
                UE_LOG(LogTemp, Warning,
                    TEXT("RoadNetworkSpawner: Road actor has no USplineComponent (edge %d->%d)."), NodeId, ToId);
                continue;
            }

            // Spline points are in the actor's local space; actor sits at FromPos
            const FVector LocalStart = FVector::ZeroVector;
            const FVector LocalEnd = ToPos - FromPos;

            SetupSpline(Spline, LocalStart, LocalEnd, StartTangent, EndTangent);
            ++RoadsSpawned;
        }
    }

    UE_LOG(LogTemp, Log, TEXT("RoadNetworkSpawner: Done — %d road segments spawned."), RoadsSpawned);

#if WITH_EDITOR
    if (World->GetCurrentLevel())
        World->GetCurrentLevel()->MarkPackageDirty();
#endif
}

void ARoadNetworkSpawner::ClearGeneratedRoads()
{
    for (AActor* Actor : SpawnedRoads)
    {
        if (IsValid(Actor))
            Actor->Destroy();
    }
    SpawnedRoads.Empty();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Coordinate conversion
// ─────────────────────────────────────────────────────────────────────────────

FVector ARoadNetworkSpawner::NodeToUEPosition(Node* InNode) const
{
    // GeographicLib LocalCartesian convention:
    //   x = East  (metres from origin)
    //   y = North (metres from origin)
    //   z = Up    (metres, 0 for flat data)
    //
    // UE5 default world axes:
    //   X = Forward → map to North  (GeographicLib y)
    //   Y = Right   → map to East   (GeographicLib x)
    //   Z = Up      → keep as-is    (GeographicLib z, likely 0)
    //
    // Multiply by MetresToCm (default 100) to convert metres → centimetres.

    return FVector(
        InNode->getY() * MetresToCm,   // North → UE X
        InNode->getX() * MetresToCm,   // East  → UE Y
        0.0f                            // flat; replace with landscape trace if needed
    );
}

// ─────────────────────────────────────────────────────────────────────────────
//  Catmull-Rom tangent
// ─────────────────────────────────────────────────────────────────────────────

FVector ARoadNetworkSpawner::ComputeCatmullRomTangent(
    const FVector* PrevPos,
    const FVector& CurrPos,
    const FVector* NextPos,
    float Tension) const
{
    if (PrevPos && NextPos)
        return Tension * (*NextPos - *PrevPos);   // central difference

    if (NextPos)
        return Tension * (*NextPos - CurrPos);    // forward difference (start of chain)

    if (PrevPos)
        return Tension * (CurrPos - *PrevPos);    // backward difference (end of chain)

    return FVector::ZeroVector;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Spline setup
// ─────────────────────────────────────────────────────────────────────────────

void ARoadNetworkSpawner::SetupSpline(
    USplineComponent* Spline,
    const FVector& LocalStart,
    const FVector& LocalEnd,
    const FVector& StartTangent,
    const FVector& EndTangent) const
{
    Spline->ClearSplinePoints(false);

    Spline->AddSplinePoint(LocalStart, ESplineCoordinateSpace::Local, false);
    Spline->SetTangentsAtSplinePoint(0, StartTangent, StartTangent,
        ESplineCoordinateSpace::Local, false);
    Spline->SetSplinePointType(0, ESplinePointType::CurveCustomTangent, false);

    Spline->AddSplinePoint(LocalEnd, ESplineCoordinateSpace::Local, false);
    Spline->SetTangentsAtSplinePoint(1, EndTangent, EndTangent,
        ESplineCoordinateSpace::Local, false);
    Spline->SetSplinePointType(1, ESplinePointType::CurveCustomTangent, false);

    Spline->UpdateSpline();
}