#include "MapPlayerController.h"
#include "RoadNetworkVisualizer.h" 

void AMapPlayerController::BeginPlay()
{
	Super::BeginPlay();
	
	// Ensure the mouse cursor is visible over the map
	bShowMouseCursor = true; 
	bEnableClickEvents = true; 
	bEnableMouseOverEvents = true;
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

void AMapPlayerController::OnLeftMouseClick()
{
	UE_LOG(LogTemp, Warning, TEXT("=== CLICK REGISTERED ==="));
	FHitResult HitResult;
	bool bHit = GetHitResultUnderCursor(ECC_Visibility, false, HitResult);

	if (bHit)
	{
		AActor* HitActor = HitResult.GetActor();

		// 1. Check if we clicked the Simulation Manager (Vehicles)
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

		// 2. Check if we clicked the Road Network Visualizer (Your existing code)
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
		else
		{
			UE_LOG(LogTemp, Error, TEXT("Hit something, but it was NOT the RoadNetworkVisualizer."));
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Raycast fired, but hit absolutely nothing."));
	}
}