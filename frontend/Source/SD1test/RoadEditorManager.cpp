#include "RoadEditorManager.h"
#include "RoadNetworkVisualizer.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"

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

	// Build the procedural tracking component
	GhostMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("GhostMeshComponent"));
	RootComponent = GhostMeshComponent;

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

void ARoadEditorManager::EndCurrentRoadSegment()
{
	// Ensure we have at least 2 points to form an actual street segment
	if (StagedClickLocations.Num() >= 2 && VisualizerTarget && SimulationNetwork)
	{
		UE_LOG(LogTemp, Warning, TEXT("Committing batch network injection sequence..."));

		uint64 PreviousNodeID = 0;

		for (int32 i = 0; i < StagedClickLocations.Num(); i++)
		{
			FVector CurrentPoint = StagedClickLocations[i];

			// Translate absolute engine world units directly into matching simulation coordinates
			double SimX = (CurrentPoint.X / 100.0) + VisualizerTarget->OriginOffsetX;
			double SimY = (CurrentPoint.Y / 100.0) + VisualizerTarget->OriginOffsetY;

			// Generate a permanent distinct node entry
			uint64 CurrentNodeID = FPlatformTime::Cycles() + i;
			SimulationNetwork->addNode(CurrentNodeID, 0.0, 0.0, SimX, SimY);

			// Link back to the preceding point if we are past index 0
			if (i > 0 && PreviousNodeID != 0)
			{
				float DistanceMeters = FVector::Dist(StagedClickLocations[i - 1], CurrentPoint) / 100.0f;

				// FORCE BIDIRECTIONAL LOGIC: Cover both directed or undirected edge variations
				SimulationNetwork->addDirectedEdge(PreviousNodeID, CurrentNodeID, DistanceMeters, 0.0, 1);
				SimulationNetwork->addDirectedEdge(CurrentNodeID, PreviousNodeID, DistanceMeters, 0.0, 1);
			}

			PreviousNodeID = CurrentNodeID;
		}

		// Fire a single render refresh pass now that the matrix data modifications are complete
		VisualizerTarget->bIsLoadingFromFile = false;
		VisualizerTarget->BuildVisualNetwork(SimulationNetwork);
	}
	else {
		// === NEW DEBUG WARNINGS ===
		if (StagedClickLocations.Num() < 2)
			UE_LOG(LogTemp, Error, TEXT("Commit Failed: Need at least 2 clicks to make a road!"));
		if (!VisualizerTarget)
			UE_LOG(LogTemp, Error, TEXT("Commit Failed: VisualizerTarget is NULL! Assign it in the editor."));
		if (!SimulationNetwork)
			UE_LOG(LogTemp, Error, TEXT("Commit Failed: SimulationNetwork is NULL! Map wasn't loaded properly."));
	}

	// Wipe our temporary local caches clear for your next road editing chain
	StagedClickLocations.Empty();
	LastPlacedNodeID = 0;
	LastPlacedPhysicalLocation = FVector::ZeroVector;

	if (GhostMeshComponent)
	{
		GhostMeshComponent->SetVisibility(false);
	}
}

void ARoadEditorManager::ClearAllRoads() {}
void ARoadEditorManager::DeleteRoadSegmentUnderCursor() {}