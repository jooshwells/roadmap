#include "MapPlayerController.h"
#include "RoadNetworkVisualizer.h" 

void AMapPlayerController::BeginPlay()
{
	Super::BeginPlay();
	
	// Ensure the mouse cursor is visible over the map
	bShowMouseCursor = true; 
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