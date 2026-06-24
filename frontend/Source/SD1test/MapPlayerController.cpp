// Fill out your copyright notice in the Description page of Project Settings.

#include "MapPlayerController.h"
#include "RoadNetworkVisualizer.h" 
#include "RoadEditorManager.h" 
#include "Kismet/GameplayStatics.h" 

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
	//UE_LOG(LogTemp, Warning, TEXT("=== CLICK REGISTERED ==="));

	//FHitResult HitResult;
	//bool bHit = GetHitResultUnderCursor(ECC_Visibility, false, HitResult);

	//if (bHit && HitResult.GetActor() != nullptr)
	//{
	//	AActor* HitActor = HitResult.GetActor();
	//	FString HitName = HitActor->GetName();
	//	UE_LOG(LogTemp, Warning, TEXT("Raycast Hit: %s"), *HitName);

	//	// Evaluate if the cursor raycast intersects an already existing visual road component segment
	//	ARoadNetworkVisualizer* ClickedVisualizer = Cast<ARoadNetworkVisualizer>(HitActor);
	//	if (ClickedVisualizer)
	//	{
	//		UE_LOG(LogTemp, Warning, TEXT("Successfully cast to RoadNetworkVisualizer. Selecting existing segment..."));

	//		int32 HitInstanceIndex = HitResult.Item;
	//		UE_LOG(LogTemp, Warning, TEXT("Hit Instance Index: %d"), HitInstanceIndex);

	//		if (HitInstanceIndex != INDEX_NONE)
	//		{
	//			int64 EdgeId = ClickedVisualizer->GetEdgeIdFromHitItem(HitInstanceIndex);
	//			UE_LOG(LogTemp, Warning, TEXT("SUCCESS! Queried Edge ID: %lld"), EdgeId);

	//			// Selection logic or inspections go here
	//		}
	//		else
	//		{
	//			UE_LOG(LogTemp, Error, TEXT("Hit the visualizer, but no specific instance was found (Index is -1)."));
	//		}
	//	}
	//	else
	//	{
	//		// We struck ground terrain (Floor). Route context data directly to Manager to handle node creation.
	//		UE_LOG(LogTemp, Log, TEXT("Hit environment/floor. Routing to RoadEditorManager for placement..."));

	//		ARoadEditorManager* EditorManager = Cast<ARoadEditorManager>(UGameplayStatics::GetActorOfClass(GetWorld(), ARoadEditorManager::StaticClass()));

	//		if (EditorManager)
	//		{
	//			EditorManager->HandleMouseClick();
	//		}
	//		else
	//		{
	//			UE_LOG(LogTemp, Warning, TEXT("Could not route placement click. No RoadEditorManager found in level outliner."));
	//		}
	//	}
	//}
	//else
	//{
	//	UE_LOG(LogTemp, Error, TEXT("Raycast fired, but hit absolutely nothing."));
	//}
}