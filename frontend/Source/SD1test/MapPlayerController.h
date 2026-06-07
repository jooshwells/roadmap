#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "MapPlayerController.generated.h"

UCLASS()
class SD1TEST_API AMapPlayerController : public APlayerController
{
	GENERATED_BODY()

protected:
	// Runs when the game starts
	virtual void BeginPlay() override;

	// Where we bind our input keys
	virtual void SetupInputComponent() override;

	// The function that fires when we click
	void OnLeftMouseClick();
};