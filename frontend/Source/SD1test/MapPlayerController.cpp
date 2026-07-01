#include "MapPlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "RoadNetworkVisualizer.h" 
#include "Blueprint/UserWidget.h" 
#include "SimulationManager.h"


void AMapPlayerController::BeginPlay()
{
	Super::BeginPlay();
	
	// Ensure the mouse cursor is visible over the map
	bShowMouseCursor = true; 
	bEnableClickEvents = true; 
	bEnableMouseOverEvents = true;

	// Create and display the UI if the blueprint is assigned
	if (EditorUIClass)
	{
		EditorUIWidget = CreateWidget<UUserWidget>(this, EditorUIClass);
		if (EditorUIWidget)
		{
			EditorUIWidget->AddToViewport();
		}
	}
}

void AMapPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	// Bind the "LeftClick" action to our custom function
	if (InputComponent)
	{
		InputComponent->BindAction("LeftClick", IE_Pressed, this, &AMapPlayerController::OnLeftMouseClick);
	}
}

bool AMapPlayerController::GetMouseIntersectionOnZPlane(FVector& OutIntersection)
{
	FVector WorldLocation, WorldDirection;
	if (DeprojectMousePositionToWorld(WorldLocation, WorldDirection))
	{
		if (WorldDirection.Z != 0.0f)
		{
			float t = -WorldLocation.Z / WorldDirection.Z;
			OutIntersection = WorldLocation + (WorldDirection * t);
			OutIntersection.Z = 0.0f;
			return true;
		}
	}
	return false;
}

void AMapPlayerController::SetDrawMode(bool bEnable, int32 InLanes, bool bTwoWay)
{
	bIsDrawingMode = bEnable;
	CurrentDrawLanes = InLanes;
	bIsTwoWayStreet = bTwoWay;
	bHasStartNode = false;

	if (bEnable && !CachedVisualizer)
	{
		// Find the visualizer in the world when draw mode starts
		AActor* FoundActor = UGameplayStatics::GetActorOfClass(GetWorld(), ARoadNetworkVisualizer::StaticClass());
		CachedVisualizer = Cast<ARoadNetworkVisualizer>(FoundActor);
	}
}

void AMapPlayerController::OnLeftMouseClick()
{
	if (!bIsDrawingMode)
	{
		// 1. Check if the input action is firing at all
		UE_LOG(LogTemp, Warning, TEXT("=== CLICK REGISTERED ==="));

		FHitResult HitResult;
		bool bHit = GetHitResultUnderCursor(ECC_Visibility, false, HitResult);

		if (bHit)
		{
			AActor* HitActor = HitResult.GetActor();

			// 2. Check WHAT the raycast actually hit
			FString HitName = HitActor ? HitActor->GetName() : TEXT("Unknown Actor");
			UE_LOG(LogTemp, Warning, TEXT("Raycast Hit: %s"), *HitName);

			ARoadNetworkVisualizer* ClickedVisualizer = Cast<ARoadNetworkVisualizer>(HitActor);
			if (ClickedVisualizer)
			{
				// 3. Confirm we recognized it as your specific visualizer class
				UE_LOG(LogTemp, Warning, TEXT("Successfully cast to RoadNetworkVisualizer."));

				int32 HitInstanceIndex = HitResult.Item;

				// 4. Check the instance index
				UE_LOG(LogTemp, Warning, TEXT("Hit Instance Index: %d"), HitInstanceIndex);

				if (HitInstanceIndex != INDEX_NONE)
				{
					int64 EdgeId = ClickedVisualizer->GetEdgeIdFromHitItem(HitInstanceIndex);
					UE_LOG(LogTemp, Warning, TEXT("SUCCESS! Edge ID: %lld"), EdgeId);

					// Do something //				
				}
				else
				{
					UE_LOG(LogTemp, Error, TEXT("Hit the visualizer, but no specific instance was found (Index is -1)."));
				}
			}
			// Check if we clicked the Simulation Manager (Vehicles)
			ASimulationManager* SimManager = Cast<ASimulationManager>(HitActor);
			if (SimManager)
			{
				// Verify we actually clicked the vehicle instances, not just the actor root
				if (HitResult.Item != INDEX_NONE)
				{
					FVehicleIDMStats Stats;
					if (SimManager->GetVehicleStatsFromInstance(HitResult.Item, Stats))
					{
						UE_LOG(LogTemp, Warning, TEXT("Clicked Vehicle ID: %d"), Stats.VehicleID);

						// Fire the event to open the Widget in Blueprints!
						OnVehicleClickedUI(Stats);
					}
				}
				return; // End execution since we found a vehicle
			}
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("Raycast fired, but hit absolutely nothing."));
		}
		return;
	}
	else
	{
		FVector ClickedLocation;
		if (GetMouseIntersectionOnZPlane(ClickedLocation))
		{
			if (!bHasStartNode)
			{
				// === CLICK 1: ENFORCE STARTING ON EXISTING NETWORK ===
				int64 SnappedNodeId;
				FVector SnappedLoc;

				if (CachedVisualizer->FindClosestNode(ClickedLocation, SnapRadius, SnappedLoc, SnappedNodeId))
				{
					StartNodeId = SnappedNodeId;
					StartNodeLocation = SnappedLoc;
					bHasStartNode = true;
					if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Cyan, FString::Printf(TEXT("Start Node Locked: %lld"), StartNodeId));
				}
				else
				{
					if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Red, TEXT("Error: You must start drawing from an existing intersection!"));
				}
			}
			else
			{
				// === CLICK 2: PLACE END NODE ===
				int64 EndNodeId = -1;
				FVector EndNodeLoc = ClickedLocation;

				int64 SnappedNodeId;
				FVector SnappedLoc;
				if (CachedVisualizer->FindClosestNode(ClickedLocation, SnapRadius, SnappedLoc, SnappedNodeId))
				{
					EndNodeId = SnappedNodeId;
					EndNodeLoc = SnappedLoc;
				}

				// 1. Draw and Export the Forward Direction (A -> B)
				CachedVisualizer->AddSingleRoadVisually(StartNodeLocation, EndNodeLoc, CurrentDrawLanes);
				int64 FinalEndNodeId = CachedVisualizer->ExportNewRoadSegment(StartNodeId, EndNodeId, EndNodeLoc, CurrentDrawLanes);

				// Find the Simulation Manager to update the live backend graph
				AActor* SimManagerActor = UGameplayStatics::GetActorOfClass(GetWorld(), ASimulationManager::StaticClass());
				ASimulationManager* SimManager = Cast<ASimulationManager>(SimManagerActor);

				if (SimManager) {
					float LengthMeters = FVector::Distance(StartNodeLocation, EndNodeLoc) / 100.0f;
					SimManager->NotifyBackendOfNewRoad(StartNodeId, FinalEndNodeId, EndNodeLoc, LengthMeters, CurrentDrawLanes);
				}
				// 2. Draw and Export the Reverse Direction (B -> A) if Two-Way is checked
				if (bIsTwoWayStreet)
				{
					// Reverse the locations to draw it coming back
					CachedVisualizer->AddSingleRoadVisually(EndNodeLoc, StartNodeLocation, CurrentDrawLanes);

					// Export using the FinalEndNodeId as the start, and the original StartNodeId as the end.
					// We pass '-1' for the end node ID here only if we somehow didn't have a start node, 
					// but since we enforce starting on an intersection, StartNodeId is always valid.
					CachedVisualizer->ExportNewRoadSegment(FinalEndNodeId, StartNodeId, StartNodeLocation, CurrentDrawLanes);

					if (SimManager) {
						float LengthMeters = FVector::Distance(EndNodeLoc, StartNodeLocation) / 100.0f;
						SimManager->NotifyBackendOfNewRoad(FinalEndNodeId, StartNodeId, StartNodeLocation, LengthMeters, CurrentDrawLanes);
					}
				}
				
				if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Green, TEXT("Road Created & Saved!"));

				// Reset for the next road segment
				bHasStartNode = false;
			}
		}
	}
}