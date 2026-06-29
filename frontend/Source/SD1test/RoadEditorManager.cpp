#include "RoadEditorManager.h"
#include "RoadNetworkVisualizer.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "Components/SplineComponent.h"
#include "DrawDebugHelpers.h"

ARoadEditorManager::ARoadEditorManager()
{
	PrimaryActorTick.bCanEverTick = true;

	// Establish structural defaults at compile-time
	ActiveTool = ERoadToolType::Straight;
	bIsPlacingRoad = true;
	VisualizerTarget = nullptr;
	SimulationNetwork = new Network(); // Placeholder blank staging network

	// Hard-wire the tracking boundaries 
	LastPlacedNodeID = 0;
	LastPlacedPhysicalLocation = FVector::ZeroVector;

	// 1. Create a completely blank, stationary scene root component
	USceneComponent* NeutralRoot = CreateDefaultSubobject<USceneComponent>(TEXT("NeutralRoot"));
	RootComponent = NeutralRoot;

	// 2. Build the procedural tracking component
	GhostMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("GhostMeshComponent"));

	// 3. Attach your Ghost Mesh to this neutral root so it can move independently
	GhostMeshComponent->SetupAttachment(NeutralRoot);

	GhostMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GhostMeshComponent->SetCastShadow(false);
}

void ARoadEditorManager::BeginPlay()
{
	Super::BeginPlay();

	// Create a dynamic material from whatever material is already on the ghost mesh
	if (GhostMeshComponent)
	{
		UMaterialInterface* BaseMaterial = GhostMeshComponent->GetMaterial(0);
		if (BaseMaterial)
		{
			GhostDynamicMaterial = GhostMeshComponent->CreateDynamicMaterialInstance(0, BaseMaterial);
		}
	}


}

void ARoadEditorManager::UpdateGhostVisuals()
{
	if (!GhostMeshComponent || !GhostDynamicMaterial) return;

	// Make sure the ghost is visible if a tool is active
	GhostMeshComponent->SetVisibility(ActiveTool != ERoadToolType::None);

	switch (ActiveTool)
	{
	case ERoadToolType::Straight:
		// Set the material to a nice semi-transparent Green/Blue for building
		GhostDynamicMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.0f, 1.0f, 0.2f, 0.5f));
		// Optional: Swap to your standard road segment mesh if needed
		// GhostMeshComponent->SetStaticMesh(StraightRoadMeshAsset);
		break;

	case ERoadToolType::BezierCurve:
		// Maybe a distinct color like Yellow/Orange to differentiate from straight lines
		GhostDynamicMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.0f, 0.6f, 0.0f, 0.5f));
		break;

	case ERoadToolType::Delete:
		// Turn it bright Red to warn the user they are in destructive mode
		GhostDynamicMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.0f, 0.0f, 0.0f, 0.6f));
		// Optional: Swap the mesh to a bounding box or removal indicator icon
		break;

	case ERoadToolType::None:
	default:
		GhostMeshComponent->SetVisibility(false);
		break;
	}
}

void ARoadEditorManager::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (bIsPlacingRoad && GhostMeshComponent)
	{
		APlayerController* PC = GetWorld()->GetFirstPlayerController();
		if (!PC) return;

		FHitResult Hit;
		PC->GetHitResultUnderCursor(ECC_Visibility, false, Hit);

		if (Hit.bBlockingHit)
		{
			GhostMeshComponent->SetVisibility(true);

			if (LastPlacedNodeID != 0)
			{
				FVector StartPoint = LastPlacedPhysicalLocation;
				FVector EndPoint = Hit.Location;

				FVector Direction = EndPoint - StartPoint;
				float DistanceCM = Direction.Size();
				FRotator PlacementRotation = Direction.Rotation();

				// Match your teammate's pivot-to-center offset algorithm
				FVector MeshLocation = StartPoint + (Direction * 0.5f);
				float BaseLen = VisualizerTarget ? VisualizerTarget->MeshBaseLengthCm : 100.0f;
				float NewScaleX = DistanceCM / FMath::Max(1.0f, BaseLen);

				GhostMeshComponent->SetWorldLocationAndRotation(MeshLocation, PlacementRotation);
				GhostMeshComponent->SetWorldScale3D(FVector(NewScaleX, 1.0f, 1.0f));
			}
			else
			{
				GhostMeshComponent->SetWorldLocationAndRotation(Hit.Location, FRotator::ZeroRotator);
				GhostMeshComponent->SetWorldScale3D(FVector(1.0f, 1.0f, 1.0f));
			}
		}
		else
		{
			GhostMeshComponent->SetVisibility(false);
		}
	}
	else if (GhostMeshComponent)
	{
		GhostMeshComponent->SetVisibility(false);
	}
}

void ARoadEditorManager::SetActiveNetwork(int64 ExternalNetworkPointerAddress)
{
	if (ExternalNetworkPointerAddress == 0) return;

	Network* LoadedNetwork = reinterpret_cast<Network*>(ExternalNetworkPointerAddress);
	if (LoadedNetwork)
	{
		if (SimulationNetwork)
		{
			delete SimulationNetwork;
		}
		SimulationNetwork = LoadedNetwork;

		if (VisualizerTarget)
		{
			VisualizerTarget->bIsLoadingFromFile = true; // Let initial load wipe layout clean
			VisualizerTarget->BuildVisualNetwork(SimulationNetwork);
		}
	}
}

void ARoadEditorManager::HandleMouseClick()
{
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!PC) return;

	FHitResult Hit;
	PC->GetHitResultUnderCursor(ECC_Visibility, false, Hit);

	if (Hit.bBlockingHit)
	{
		// 1. Simply log the physical world position into our lightweight staging array
		StagedClickLocations.Add(Hit.Location);

		// 2. Advance our tracking pointers for the real-time stretching ghost visualizer
		uint64 MockNodeID = FPlatformTime::Cycles(); // Temporary non-zero ID to keep Tick() stretching
		LastPlacedNodeID = MockNodeID;
		LastPlacedPhysicalLocation = Hit.Location;

		UE_LOG(LogTemp, Warning, TEXT("Staged Node Placement %d at: %s"), StagedClickLocations.Num(), *Hit.Location.ToString());
	}
}

void ARoadEditorManager::EndCurrentRoadSegment(int32 LaneCount, FVector StartPoint, FVector EndPoint, UInstancedStaticMeshComponent* TargetISMComponent)
{
	if (LaneCount <= 0) return;

	// Calculate base vector directions
	FVector ForwardDir = (EndPoint - StartPoint).GetSafeNormal();
	// Cross product with Up Vector gets the local Right Direction
	FVector RightDir = FVector::CrossProduct(ForwardDir, FVector::UpVector).GetSafeNormal();

	// Set up clean tangents for smooth curves/lines
	FVector SegmentTangent = ForwardDir * FVector::Distance(StartPoint, EndPoint);

	// Loop and spawn parallel lane splines
	for (int32 i = 0; i < LaneCount; ++i)
	{
		// Calculate the centering offset
		float LaneOffsetMultiplier = (float)i - ((float)LaneCount - 1.0f) / 2.0f;
		FVector LateralOffset = RightDir * (LaneOffsetMultiplier * LaneWidth);

		// Shift both start and end locations laterally
		FVector LaneStart = StartPoint + LateralOffset;
		FVector LaneEnd = EndPoint + LateralOffset;

		// 1. [Spline Component Logic]
		FString SplineName = FString::Printf(TEXT("LaneSpline_Component_%d"), i);
		USplineComponent* NewLaneSpline = NewObject<USplineComponent>(this, FName(*SplineName));

		if (NewLaneSpline)
		{
			NewLaneSpline->RegisterComponent();
			NewLaneSpline->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);
			NewLaneSpline->ClearSplinePoints(true);

			NewLaneSpline->AddSplinePoint(LaneStart, ESplineCoordinateSpace::World, false);
			NewLaneSpline->SetTangentAtSplinePoint(0, SegmentTangent, ESplineCoordinateSpace::World, false);
			NewLaneSpline->AddSplinePoint(LaneEnd, ESplineCoordinateSpace::World, false);
			NewLaneSpline->SetTangentAtSplinePoint(1, SegmentTangent, ESplineCoordinateSpace::World, false);
			NewLaneSpline->UpdateSpline();

			// Persistent debug visuals
			DrawDebugLine(GetWorld(), LaneStart, LaneEnd, FColor::Green, true, -1.0f, 0, 12.0f);
			DrawDebugSphere(GetWorld(), LaneStart, 35.0f, 8, FColor::Red, true);
		}

		// Every iteration loops here and stamps the same seamless center asset side-by-side
		// 2. [Mesh Instance Stamping]
		// 
		// Tell the component to allocate 1 float per instance for the shader to read
		TargetISMComponent->NumCustomDataFloats = 1;

		// 2. [Mesh Instance Stamping] 
		if (TargetISMComponent)
		{
			FVector LaneDirection = LaneEnd - LaneStart;
			float DistanceCM = LaneDirection.Size();
			FRotator PlacementRotation = LaneDirection.Rotation();

			FVector MeshLocation = LaneStart + (LaneDirection * 0.5f);
			float BaseLen = VisualizerTarget ? VisualizerTarget->MeshBaseLengthCm : 100.0f;
			float NewScaleX = DistanceCM / FMath::Max(1.0f, BaseLen);

			float BaseWidth = (VisualizerTarget && VisualizerTarget->MeshBaseLengthCm > 1.0f) ? VisualizerTarget->MeshBaseLengthCm : 100.0f;
			float NewScaleY = LaneWidth / FMath::Max(1.0f, BaseWidth);

			FTransform InstanceTransform;
			InstanceTransform.SetLocation(MeshLocation);
			InstanceTransform.SetRotation(PlacementRotation.Quaternion());
			InstanceTransform.SetScale3D(FVector(NewScaleX, NewScaleY, 1.0f));

			// 👇 1. Capture the index of the newly added instance
			int32 InstanceIndex = TargetISMComponent->AddInstance(InstanceTransform, true);

			// 👇 2. Send NewScaleX to Custom Data Index 0 so the shader can fix the tiling and draw the lines!
			TargetISMComponent->SetCustomDataValue(InstanceIndex, 0, NewScaleX, true);
		}
	}
}

void ARoadEditorManager::ClearAllRoads() {}
void ARoadEditorManager::DeleteRoadSegmentUnderCursor() {}