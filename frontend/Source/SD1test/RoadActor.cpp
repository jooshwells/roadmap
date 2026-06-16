#include "RoadActor.h"
#include "Components/SplineComponent.h"
#include "Components/SplineMeshComponent.h"

ARoadActor::ARoadActor()
{
    PrimaryActorTick.bCanEverTick = false;

    RoadSpline = CreateDefaultSubobject<USplineComponent>(TEXT("Spline"));
    RootComponent = RoadSpline;

	RoadStaticMesh = nullptr; // Default value, can be set in the Unreal Editor
}

void ARoadActor::InitializeRoad(FVector StartPos, FVector EndPos)
{
    if (!RoadSpline) return;

    // 1. Set the mathematical spline points using absolute WORLD coordinates
    RoadSpline->ClearSplinePoints(true);
    RoadSpline->AddSplinePoint(StartPos, ESplineCoordinateSpace::World, false);
    RoadSpline->AddSplinePoint(EndPos, ESplineCoordinateSpace::World, false);
    RoadSpline->UpdateSpline();

    if (!RoadStaticMesh)
    {
        UE_LOG(LogTemp, Warning, TEXT("Road mesh initialization failed: No Static Mesh assigned!"));
        return;
    }

    USplineMeshComponent* SplineMeshComp = NewObject<USplineMeshComponent>(this);
    if (SplineMeshComp)
    {
        SplineMeshComp->SetMobility(EComponentMobility::Movable);
        SplineMeshComp->RegisterComponent();
        SplineMeshComp->SetStaticMesh(RoadStaticMesh);

        // CRITICAL FIX: Attach using KeepWorldTransform so the spline doesn't shift when attached
        SplineMeshComp->AttachToComponent(RoadSpline, FAttachmentTransformRules::KeepWorldTransform);

        // 4. FIX: Change ESplineCoordinateSpace::Local to ESplineCoordinateSpace::World
        FVector StartLocation, StartTangent, EndLocation, EndTangent;
        RoadSpline->GetLocationAndTangentAtSplinePoint(0, StartLocation, StartTangent, ESplineCoordinateSpace::World);
        RoadSpline->GetLocationAndTangentAtSplinePoint(1, EndLocation, EndTangent, ESplineCoordinateSpace::World);

        // Safety fallback: If tangents are completely flat, give them a forward push
        if (StartTangent.IsNearlyZero()) { StartTangent = FVector(100.f, 0.f, 0.f); }
        if (EndTangent.IsNearlyZero()) { EndTangent = FVector(100.f, 0.f, 0.f); }

        // 5. Apply the positioning data to deform our custom asset model
        SplineMeshComp->SetStartAndEnd(StartLocation, StartTangent, EndLocation, EndTangent, true);
        SplineMeshComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);

        ActiveSplineMeshes.Add(SplineMeshComp);
    }
}