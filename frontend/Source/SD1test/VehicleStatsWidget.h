#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/SlateWrapperTypes.h" // FEventReply
#include "SimulationManager.h" // FVehicleIDMStats
#include "VehicleStatsWidget.generated.h"

class UVerticalBox;
class UTextBlock;
class UProgressBar;
class UButton;
class UBorder;
class UCanvasPanelSlot;

// Self-contained vehicle telemetry panel: the entire widget tree is built in
// C++ (no UMG asset needed), like RoadEditorWidget. AMapPlayerController
// opens it when a vehicle is clicked; while open it re-polls the simulation
// every UI tick so speed / acceleration / wait time read live rather than as
// a one-shot snapshot. When the vehicle despawns (arrived, or the sim was
// stopped) the live rows grey out and the header says so.
UCLASS()
class SD1TEST_API UVehicleStatsWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    // Targets the panel at a vehicle. Also retargets an already-open panel
    // when another vehicle is clicked.
    void InitWithStats(ASimulationManager* InSimManager, const FVehicleIDMStats& InStats);

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeConstruct() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
    UFUNCTION()
    void HandleCloseClicked();

    // Title-bar drag: press starts, move repositions the window, release ends.
    UFUNCTION()
    FEventReply HandleTitleBarMouseDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent);

    UFUNCTION()
    FEventReply HandleTitleBarMouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent);

    UFUNCTION()
    FEventReply HandleTitleBarMouseUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent);

    // Pushes Stats into the live readouts (speed, acceleration, lane, ...).
    void RefreshLiveFields();

    // Pushes Stats into the driver-profile rows. IDM parameters are fixed for
    // a vehicle's lifetime, so these are only written on (re)target.
    void RefreshProfileFields();

    // Greys the panel body out once the vehicle is gone and stops polling.
    void EnterStaleState();

    // Adds a "Label      value" row and returns the value text block.
    UTextBlock* AddValueRow(UVerticalBox* Parent, const FText& Label);

    // Adds a dim uppercase-style section header row.
    void AddSectionHeader(UVerticalBox* Parent, const FText& Label);

    FVehicleIDMStats Stats;
    TWeakObjectPtr<ASimulationManager> SimManager;

    // The vehicle vanished from the sim (arrived / despawned / sim stopped).
    bool bStale = false;

    // Live values re-poll a few times a second; the sim steps at ~10 Hz so
    // polling faster would just read the same numbers.
    float PollAccumulator = 0.0f;
    static constexpr float PollInterval = 0.1f;

    // Title-bar drag state (mirrors RoadEditorWidget).
    bool bDraggingWindow = false;
    FVector2D DragStartScreenPos = FVector2D::ZeroVector;
    FVector2D DragStartPanelPos = FVector2D::ZeroVector;

    UPROPERTY() UCanvasPanelSlot* PanelSlot = nullptr;
    UPROPERTY() UBorder* TitleBar = nullptr;
    UPROPERTY() UTextBlock* HeaderText = nullptr;
    UPROPERTY() UButton* CloseButton = nullptr;

    // Everything under the title bar, dimmed in one go when stale.
    UPROPERTY() UVerticalBox* BodyBox = nullptr;

    // Live section
    UPROPERTY() UTextBlock* SpeedBigText = nullptr;  // "34 mph"
    UPROPERTY() UTextBlock* StatusText = nullptr;    // Accelerating / Braking / ...
    UPROPERTY() UTextBlock* SpeedSubText = nullptr;  // m/s + desired speed
    UPROPERTY() UProgressBar* SpeedBar = nullptr;    // current vs desired speed
    UPROPERTY() UTextBlock* AccelValue = nullptr;
    UPROPERTY() UTextBlock* LaneValue = nullptr;
    UPROPERTY() UTextBlock* LimitValue = nullptr;    // current road's speed limit
    UPROPERTY() UTextBlock* WaitValue = nullptr;
    UPROPERTY() UTextBlock* RouteValue = nullptr;
    UPROPERTY() UProgressBar* RouteBar = nullptr;

    // Driver profile (static IDM parameters)
    UPROPERTY() UTextBlock* DesiredSpeedValue = nullptr;
    UPROPERTY() UTextBlock* MaxAccelValue = nullptr;
    UPROPERTY() UTextBlock* BrakeValue = nullptr;
    UPROPERTY() UTextBlock* MinGapValue = nullptr;
    UPROPERTY() UTextBlock* HeadwayValue = nullptr;
    UPROPERTY() UTextBlock* AccelExpValue = nullptr;
    UPROPERTY() UTextBlock* PolitenessValue = nullptr;

    // Speeds are meters/second everywhere in the sim; the UI leads with mph
    // to match the road editor panel.
    static constexpr float MpsToMph = 2.23694f;
};
