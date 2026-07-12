#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SimControlBarWidget.generated.h"

class ASimulationManager;
class UBorder;
class UButton;
class URoadmapRowButton;
class UTextBlock;
class UWidgetSwitcher;

// Video-player style transport bar for the traffic simulation, anchored at the
// top-center of the HUD and built entirely in C++ (no UMG asset needed), in the
// main menu's asphalt-and-amber palette. AMapPlayerController spawns it at
// BeginPlay (unless bUseBuiltInSimControlBar is off).
//
//   [play/pause]  [stop]  |  0.5x 1x 2x 4x  |  status
//
// Play starts the sim, or resumes it when paused; pause freezes stepping in
// place; stop is the full ASimulationManager::StopSimulation reset (telemetry
// pipeline included). The speed pills set the sim's playback rate and may be
// picked before starting. The bar re-polls the manager every tick, so it stays
// in sync no matter what else starts or stops the simulation.
UCLASS()
class ROADMAP_API USimControlBarWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	UFUNCTION()
	void HandlePlayPauseClicked();

	UFUNCTION()
	void HandleStopClicked();

	void HandleSpeedSelected(int32 PresetIndex);

	// Finds (and caches) the simulation manager in the world.
	ASimulationManager* ResolveSimManager();

	// Pushes the given transport state into every control and remembers it so
	// NativeTick only restyles when something actually changed.
	void RefreshVisuals(bool bRunning, bool bPaused, int32 SpeedIndex);

	// Nearest speed pill for whatever multiplier the manager currently holds.
	int32 SpeedIndexForMultiplier(float Multiplier) const;

	UPROPERTY() ASimulationManager* CachedSimManager = nullptr;

	UPROPERTY() UButton* PlayPauseButton = nullptr;
	UPROPERTY() UWidgetSwitcher* PlayPauseIcon = nullptr; // 0 = play glyph, 1 = pause bars
	UPROPERTY() UButton* StopButton = nullptr;
	UPROPERTY() UBorder* StopIcon = nullptr;
	UPROPERTY() UTextBlock* StatusText = nullptr;
	UPROPERTY() TArray<URoadmapRowButton*> SpeedButtons;

	// Last state pushed into the widgets (see RefreshVisuals).
	bool bLastRunning = false;
	bool bLastPaused = false;
	int32 LastSpeedIndex = 1; // the 1x pill
};
