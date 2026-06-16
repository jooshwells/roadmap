// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "RoadEditorManager.h"
#include "RoadNodeActor.h"   
#include "RoadActor.h"     
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h" // Gameplay utilities

ARoadEditorManager::ARoadEditorManager()
{
	PrimaryActorTick.bCanEverTick = true;

	LastPlacedNode = nullptr;
}

void ARoadEditorManager::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (GetWorld()->GetFirstPlayerController()->WasInputKeyJustPressed(EKeys::LeftMouseButton))
	{
		HandleMouseClick();
	}
}
void ARoadEditorManager::HandleMouseClick()
{
    APlayerController* PC = GetWorld()->GetFirstPlayerController();
    if (!PC) return; // Null pointer safety check

    FHitResult Hit;
    PC->GetHitResultUnderCursor(ECC_Visibility, false, Hit);

    if (Hit.bBlockingHit)
    {
        FVector Location = Hit.Location;

        // 1. Spawn the node actor at the hit location
        ARoadNodeActor* NewNode = GetWorld()->SpawnActor<ARoadNodeActor>(Location, FRotator::ZeroRotator);

        // 2. If we already have a previous anchor, spawn a road connecting them
        if (LastPlacedNode)
        {
            if (RoadSegmentClass)
            {
                FActorSpawnParameters SpawnParams;
                SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

                ARoadActor* Road = GetWorld()->SpawnActor<ARoadActor>(RoadSegmentClass, Location, FRotator::ZeroRotator, SpawnParams);

                if (Road)
                {
                    // Apply our vertical offset lift to prevent Z-fighting clipping
                    FVector LiftedStart = LastPlacedNode->GetActorLocation() + FVector(0.f, 0.f, 3.f);
                    FVector LiftedEnd = Location + FVector(0.f, 0.f, 3.f);

                    Road->InitializeRoad(LiftedStart, LiftedEnd);
                }
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("RoadEditorManager: Cannot spawn road! No RoadSegmentClass assigned in the inspector."));
            }
        }

        // 3. Hand over the pointer. This node becomes the starting anchor for the NEXT left-click.
        LastPlacedNode = NewNode;
    }
}

void ARoadEditorManager::EndCurrentRoadSegment()
{
    if (LastPlacedNode)
    {
        UE_LOG(LogTemp, Log, TEXT("RoadEditorManager: Segment broken. Starting a fresh chain on next click."));
        LastPlacedNode = nullptr;
    }
}

void ARoadEditorManager::ClearAllRoads()
{
    // Force current placement chain to break
	EndCurrentRoadSegment();

    // Find and destroy every existing road segment and node in the level
	TArray<AActor*> FoundRoads;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), ARoadActor::StaticClass(), FoundRoads);
    for (AActor* Road : FoundRoads)
    {
        Road->Destroy();
	}

	TArray<AActor*> FoundNodes;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), ARoadNodeActor::StaticClass(), FoundNodes);
    for (AActor* Node : FoundNodes)
    {
       if(Node) Node->Destroy();
    }

	UE_LOG(LogTemp, Log, TEXT("RoadEditorManager: User created roads cleared successfully."));
}

void ARoadEditorManager::DeleteRoadSegmentUnderCursor()
{
    APlayerController* PC = GetWorld()->GetFirstPlayerController();
    if (!PC) return; // Null pointer safety check
    FHitResult Hit;
    PC->GetHitResultUnderCursor(ECC_Visibility, false, Hit);
    if (Hit.bBlockingHit)
    {
        ARoadActor* HitRoad = Cast<ARoadActor>(Hit.GetActor());
        if (HitRoad)
        {
            HitRoad->Destroy();
            UE_LOG(LogTemp, Log, TEXT("RoadEditorManager: Road segment deleted under cursor."));
        }
    }
}

void ARoadEditorManager::BeginPlay()
{
    Super::BeginPlay(); // Gives Unreal's base class a chance to initialize
}
