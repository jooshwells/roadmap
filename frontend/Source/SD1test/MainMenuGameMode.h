#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "MainMenuGameMode.generated.h"

class UUserWidget;

/**
 * Game mode for the MainMenu level. Creates the C++-built main menu widget and
 * puts the player controller into UI-only input. Wired to the level by the
 * GameModeMapPrefixes entry for "MainMenu" in DefaultEngine.ini, so the .umap
 * itself needs no game mode override.
 */
UCLASS()
class SD1TEST_API AMainMenuGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AMainMenuGameMode();

protected:
	virtual void BeginPlay() override;

	// Defaults to the C++ UMainMenuWidget; override with a Blueprint subclass
	// if the menu ever needs designer tweaks.
	UPROPERTY(EditDefaultsOnly, Category = "Main Menu")
	TSubclassOf<UUserWidget> MenuWidgetClass;

private:
	void ShowMenu();

	UPROPERTY()
	TObjectPtr<UUserWidget> MenuWidget;
};
