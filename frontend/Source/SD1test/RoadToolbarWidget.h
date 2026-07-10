#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/SlateWrapperTypes.h" // FEventReply
#include "RoadToolbarWidget.generated.h"

class UVerticalBox;
class UTextBlock;
class UEditableTextBox;
class UCheckBox;
class UButton;
class UComboBoxString;
class UBorder;
class UCanvasPanelSlot;
class UWidget;

// Self-contained road-drawing toolbar: the entire widget tree is built in
// C++ (no UMG asset needed). AMapPlayerController spawns it at BeginPlay
// (unless bUseBuiltInRoadToolbar is off). A small "Road Tools" button is
// always on screen; clicking it opens/closes the tool window, which can be
// dragged around by its title bar. The Draw Road button toggles draw mode;
// lanes / speed / two-way / turn lanes are pushed live through
// AMapPlayerController::SetDrawMode so mid-draw changes take effect on the
// next placed segment.
UCLASS()
class SD1TEST_API URoadToolbarWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    // Forces the draw toggle to a given state without pushing SetDrawMode
    // (used when the simulation starts and the controller exits draw mode
    // itself, so the button label / panel tint stay in sync).
    void SyncDrawState(bool bInDrawing);

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeConstruct() override;

private:
    UFUNCTION()
    void HandleToggleWindowClicked();

    UFUNCTION()
    void HandleDrawClicked();

    UFUNCTION()
    void HandleLanesCommitted(const FText& Text, ETextCommit::Type CommitMethod);

    UFUNCTION()
    void HandleSpeedCommitted(const FText& Text, ETextCommit::Type CommitMethod);

    UFUNCTION()
    void HandleTwoWayChanged(bool bIsChecked);

    UFUNCTION()
    void HandleTurnLaneComboChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

    // Builds the light-text widget for one turn-lane combo entry, used for both
    // the closed button content and the dropdown rows.
    UFUNCTION()
    UWidget* MakeTurnLaneEntry(FString Item);

    // Title-bar drag: press starts, move repositions the window, release ends.
    UFUNCTION()
    FEventReply HandleTitleBarMouseDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent);

    UFUNCTION()
    FEventReply HandleTitleBarMouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent);

    UFUNCTION()
    FEventReply HandleTitleBarMouseUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent);

    // Sends the current control values (and draw-mode state) to the player
    // controller. Called on toggle and on every control change while drawing.
    void PushDrawParams();

    // Rebuilds one turn dropdown per lane, seeding selections from
    // CurrentTurnLanes.
    void RebuildTurnLaneCombos();

    // Joins the dropdown selections back into an OSM turn:lanes string.
    // Empty when every lane is "(none)".
    FString ComposeTurnLanesFromCombos() const;

    // Updates the button label / status line for the current draw state.
    void RefreshDrawStateVisuals();

    // Adds one "Label:  [input]" row to the panel.
    void AddRow(UVerticalBox* Parent, const FText& Label, UWidget* Input);

    bool bDrawing = false;
    bool bWindowOpen = false;

    // Current (already clamped) input values; the text boxes are re-synced to
    // these whenever an entry is committed.
    int32 CurrentLanes = 2;
    int32 CurrentSpeedMph = 45;
    FString CurrentTurnLanes;

    // Title-bar drag state (in slate screen space / canvas local space).
    bool bDraggingWindow = false;
    FVector2D DragStartScreenPos = FVector2D::ZeroVector;
    FVector2D DragStartPanelPos = FVector2D::ZeroVector;

    UPROPERTY() UButton* ToggleButton = nullptr;
    UPROPERTY() UBorder* Panel = nullptr;
    UPROPERTY() UCanvasPanelSlot* PanelSlot = nullptr;
    UPROPERTY() UBorder* TitleBar = nullptr;
    UPROPERTY() UButton* DrawButton = nullptr;
    UPROPERTY() UTextBlock* DrawButtonLabel = nullptr;
    UPROPERTY() UTextBlock* StatusText = nullptr;
    UPROPERTY() UEditableTextBox* LanesBox = nullptr;
    UPROPERTY() UEditableTextBox* SpeedBox = nullptr; // shown in mph
    UPROPERTY() UCheckBox* TwoWayCheck = nullptr;
    UPROPERTY() UVerticalBox* TurnLaneRows = nullptr;
    UPROPERTY() TArray<UComboBoxString*> TurnLaneCombos;

    static constexpr int32 MinLanes = 1;
    static constexpr int32 MaxLanes = 6;
    static constexpr int32 MinSpeedMph = 5;
    static constexpr int32 MaxSpeedMph = 80;

    // Speed limits are meters/second everywhere in the sim; the UI shows mph.
    static constexpr float MpsToMph = 2.23694f;
};
