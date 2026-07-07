#include "MainMenuGameMode.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "MainMenuWidget.h"
#include "TimerManager.h"

AMainMenuGameMode::AMainMenuGameMode()
{
	// UI-only level; nothing to possess.
	bStartPlayersAsSpectators = true;
	MenuWidgetClass = UMainMenuWidget::StaticClass();
}

void AMainMenuGameMode::BeginPlay()
{
	Super::BeginPlay();

	// Defer one tick: if the level blueprint still spawns the legacy
	// WBP_MainMenu on BeginPlay, it exists by then and the viewport wipe below
	// leaves only the new menu on screen.
	GetWorldTimerManager().SetTimerForNextTick(this, &AMainMenuGameMode::ShowMenu);
}

void AMainMenuGameMode::ShowMenu()
{
	UWidgetLayoutLibrary::RemoveAllWidgets(this);

	APlayerController* PlayerController = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!PlayerController || !MenuWidgetClass)
	{
		return;
	}

	MenuWidget = CreateWidget<UUserWidget>(PlayerController, MenuWidgetClass);
	if (!MenuWidget)
	{
		return;
	}
	MenuWidget->AddToViewport();

	PlayerController->bShowMouseCursor = true;
	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(MenuWidget->TakeWidget());
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	PlayerController->SetInputMode(InputMode);
}
