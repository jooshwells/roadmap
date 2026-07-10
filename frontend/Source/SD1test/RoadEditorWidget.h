#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/SlateWrapperTypes.h" // FEventReply
#include "RoadNetworkVisualizer.h" // FRoadEdgeInfo
#include "RoadEditorWidget.generated.h"

class UVerticalBox;
class UHorizontalBox;
class UTextBlock;
class UEditableTextBox;
class UCheckBox;
class UButton;
class UComboBoxString;
class UBorder;
class UCanvasPanelSlot;
class UWidget;

// Self-contained road property editor: the entire widget tree is built in
// C++ (no UMG asset needed). AMapPlayerController opens it when a road is
// clicked outside draw mode; the window can be dragged around by its title
// bar. Apply pushes the edit through AMapPlayerController::ApplyRoadEdit
// (files + visuals + live sim).
UCLASS()
class SD1TEST_API URoadEditorWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    // Fills the panel with a road's current values. Also retargets an
    // already-open panel when another road is clicked.
    void InitWithEdgeInfo(const FRoadEdgeInfo& Info);

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeConstruct() override;

private:
    UFUNCTION()
    void HandleApplyClicked();

    UFUNCTION()
    void HandleCancelClicked();

    // First click arms ("Confirm delete?"), second click deletes the road
    // through AMapPlayerController::DeleteRoad. Retargeting the panel or
    // clicking Apply/Cancel disarms.
    UFUNCTION()
    void HandleDeleteClicked();

    UFUNCTION()
    void HandleLanesCommitted(const FText& Text, ETextCommit::Type CommitMethod);

    UFUNCTION()
    void HandleSpeedCommitted(const FText& Text, ETextCommit::Type CommitMethod);

    UFUNCTION()
    void HandleLayerComboChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

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

    // Pushes EdgeInfo's values into the input controls (if built yet).
    void RefreshFields();

    // Rebuilds one turn dropdown per lane, seeding selections from
    // CurrentTurnLanes.
    void RebuildTurnLaneCombos();

    // Joins the dropdown selections back into an OSM turn:lanes string.
    // Empty when every lane is "(none)".
    FString ComposeTurnLanesFromCombos() const;

    // Adds one "Label:  [input]" row to the panel.
    void AddRow(UVerticalBox* Parent, const FText& Label, UWidget* Input);

    FRoadEdgeInfo EdgeInfo;

    // True while the simulation is running: the panel shows the road's info
    // but every input is locked and Apply/Delete are hidden. Computed when
    // the panel is (re)targeted at a road.
    bool bReadOnly = false;

    // Current (already clamped) input values; the text boxes are re-synced to
    // these whenever an entry is committed.
    int32 CurrentLanes = 2;
    int32 CurrentSpeedMph = 45;
    FString CurrentTurnLanes;
    int32 CurrentLayer = 0; // vertical layer (0 = ground)

    // Title-bar drag state (in slate screen space / canvas local space).
    bool bDraggingWindow = false;
    FVector2D DragStartScreenPos = FVector2D::ZeroVector;
    FVector2D DragStartPanelPos = FVector2D::ZeroVector;

    UPROPERTY() UCanvasPanelSlot* PanelSlot = nullptr;
    UPROPERTY() UBorder* TitleBar = nullptr;
    UPROPERTY() UTextBlock* HeaderText = nullptr;
    UPROPERTY() UEditableTextBox* LanesBox = nullptr;
    UPROPERTY() UEditableTextBox* SpeedBox = nullptr; // shown in mph
    UPROPERTY() UComboBoxString* LayerCombo = nullptr; // elevation
    UPROPERTY() UVerticalBox* TurnLaneRows = nullptr;
    UPROPERTY() TArray<UComboBoxString*> TurnLaneCombos;
    UPROPERTY() UCheckBox* BothDirectionsCheck = nullptr;
    UPROPERTY() UButton* ApplyButton = nullptr;
    UPROPERTY() UButton* CancelButton = nullptr;
    UPROPERTY() UButton* DeleteButton = nullptr;
    UPROPERTY() UTextBlock* DeleteButtonText = nullptr;

    // Delete needs a second click to confirm.
    bool bDeleteArmed = false;

    static constexpr int32 MinLanes = 1;
    static constexpr int32 MaxLanes = 6;
    static constexpr int32 MinSpeedMph = 5;
    static constexpr int32 MaxSpeedMph = 80;

    // Speed limits are meters/second everywhere in the sim; the UI shows mph.
    static constexpr float MpsToMph = 2.23694f;
};
