#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "TelemetryPanelBridge.h"
#include "TelemetryPanelWidget.generated.h"

class UComboBoxString;
class UBorder;
class UCanvasPanel;
class UImage;
class UHorizontalBox;
class UOverlay;
class UScrollBox;
class UTextBlock;
class UTexture2D;
class UVerticalBox;
class UWidget;
class UWidgetSwitcher;

DECLARE_DELEGATE_OneParam(FOnTelemetryRunSelected, int32);

// A run-list button remembers its array index so the panel can select that run.
UCLASS()
class ROADMAP_API UTelemetryRunButton : public UButton
{
    GENERATED_BODY()

public:
    // Stores the row index and connects the normal button click event.
    void InitializeRow(int32 InRunIndex);

    FOnTelemetryRunSelected OnRunSelected;

private:
    // Passes this row's index back to the telemetry panel.
    UFUNCTION()
    void HandleClicked();

    int32 RunIndex = INDEX_NONE;
};

/**
 * Builds and controls the complete telemetry panel in C++.
 * Python is only launched when the user requests a new heatmap.
 */
UCLASS()
class ROADMAP_API UTelemetryPanelWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    // Builds the panel and loads its first run list.
    virtual bool Initialize() override;
    // Locks game controls when the panel opens.
    virtual void NativeConstruct() override;
    // Restores game controls when the panel closes.
    virtual void NativeDestruct() override;
    // Finishes placing the automatic metric marker after a map opens.
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
    // Zooms the heatmap with the mouse wheel.
    virtual FReply NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
    // Starts a heatmap click or drag.
    virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
    // Finishes a heatmap click or drag.
    virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
    // Moves the heatmap and checks which road is under the mouse.
    virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

private:
    // Builds every tab and viewer widget.
    void BuildWidgetTree();
    // Stops the panel from moving the game camera.
    void SuppressGameInput();
    // Gives camera controls back to the game.
    void RestoreGameInput();

    // Creates text using the shared panel style.
    UTextBlock* MakeText(
        const FString& Text,
        int32 Size,
        const FLinearColor& Color,
        const FName& Typeface = FName("Regular"),
        int32 LetterSpacing = 0
    );

    // Creates a main or normal action button.
    UButton* MakeActionButton(const FString& Label, bool bPrimary);

    // Creates one button row for a saved run.
    UTelemetryRunButton* MakeRunRow(int32 RunIndex, const FTelemetryRunInfo& RunInfo);

    // Reloads saved runs for the current map.
    void RefreshRunList();

    // Shows details for the selected run.
    void RefreshSelectedRunDetails();

    // Highlights the selected run row.
    void RefreshRunRowStyles();
    // Lists the other runs that can be compared.
    void RefreshComparisonOptions(int32 PreferredRunIndex = INDEX_NONE);
    // Draws the finished comparison result.
    void RenderComparisonResult(const FTelemetryRunComparisonResult& Result);

    // Changes a metric label into its Python ID.
    FString GetMetricId(const FString& DisplayName) const;

    // Changes a metric ID into a friendly label.
    FString GetMetricDisplayName(const FString& MetricId) const;
    // Explains the selected metric in simple words.
    FString GetMetricHelpText(const FString& MetricId) const;
    // Changes a road-group label into its Python ID.
    FString GetFocusId(const FString& DisplayName) const;
    // Changes a road-group ID into a friendly label.
    FString GetFocusDisplayName(const FString& FocusId) const;

    // Shows a normal or error message in the panel.
    void SetStatus(const FString& Message, bool bIsError = false);

    // Builds the smooth legend image from the metric's colors.
    void UpdateHeatmapLegendGradient(const TArray<FString>& ColorsTopToBottom);

    // Applies the current zoom and pan without changing the widget's layout.
    void ApplyHeatmapViewTransform();
    // Changes the heatmap zoom around the mouse position.
    void SetHeatmapZoom(float NewZoom, const FVector2D* CursorScreenPosition = nullptr);
    // Returns the heatmap to its starting view.
    void ResetHeatmapView();
    // Keeps the heatmap from being dragged off screen.
    FVector2D ClampHeatmapPan(const FVector2D& RequestedPan) const;
    // Checks whether the mouse is inside the map area.
    bool IsPointerOverHeatmap(const FVector2D& ScreenPosition) const;
    // Updates the road shown while the mouse moves.
    void UpdateHeatmapRoadHover(const FVector2D& ScreenPosition);
    // Finds the road line closest to the mouse.
    bool FindNearestHeatmapRoad(
        const FVector2D& ScreenPosition,
        int32& OutRoadIndex,
        FVector2D& OutClosestNormalizedPoint
    ) const;
    // Fills the road information card.
    void ShowHeatmapRoadDetails(int32 RoadIndex, bool bPinned);
    // Clears the current hovered and pinned road.
    void ClearHeatmapRoadInteraction();
    // Moves the road marker to the correct place.
    void UpdateHeatmapRoadMarker();
    // Finds the road with the highest or lowest value for the current metric.
    void SelectHeatmapExtremeRoad();
    // Moves the automatic high or low marker to its road.
    void UpdateHeatmapExtremeMarker();
    // Places one marker over a normalized point on the SVG map.
    void PlaceHeatmapMarker(
        UWidget* MarkerWidget,
        const FVector2D& NormalizedPoint,
        const FVector2D& MarkerHalfSize
    ) const;
    // Formats a road value with the right unit.
    FString FormatHeatmapRoadValue(const FTelemetryHeatmapRoad& Road) const;
    // Loads one heatmap into the full-screen viewer.
    bool OpenHeatmapPath(const FString& HeatmapPath, const FString& Title, const FString& Metric);
    // Switches between the three comparison maps.
    void ShowComparisonHeatmapMode(int32 ModeIndex);
    // Clears the two-click delete warning.
    void ResetDeleteConfirmation();

    // Switches the right-side workspace while keeping the selected run active.
    void SetWorkspaceTab(int32 TabIndex);
    // Highlights the open workspace tab.
    void RefreshWorkspaceTabStyles();
    // Updates the Generate, View, and Regenerate buttons.
    void RefreshHeatmapActionState();
    // Checks whether the chosen heatmap already exists.
    bool DoesSelectedHeatmapExist() const;
    // Locks task buttons while a long job is running.
    void SetTelemetryTaskRunning(bool bIsRunning);
    // Handles the result of a heatmap job.
    void FinishHeatmapGeneration(
        const FString& RunId,
        const FString& Metric,
        const FString& Focus,
        bool bSucceeded
    );
    // Handles the result of an FDOT job.
    void FinishFDOTComparison(
        const FString& RunId,
        bool bSucceeded,
        const FTelemetryFDOTValidationSummary& Summary,
        const FString& ErrorMessage
    );

    // Handles the result of a run comparison.
    void FinishRunComparison(
        const FString& BaselineRunId,
        const FString& ComparisonRunId,
        bool bSucceeded,
        const FTelemetryRunComparisonResult& Result,
        const FString& ErrorMessage
    );
    // Handles the result of comparison heatmap work.
    void FinishComparisonHeatmaps(
        const FString& BaselineRunId,
        const FString& ComparisonRunId,
        bool bSucceeded,
        const FTelemetryHeatmapComparisonPaths& Paths,
        const FString& ErrorMessage
    );
    // Handles the result of deleting a run.
    void FinishDeleteRun(const FString& RunId, bool bSucceeded, const FString& ErrorMessage);

    // Selects a saved run from the list.
    void HandleRunSelected(int32 RunIndex);

    // Closes the whole telemetry panel.
    UFUNCTION()
    void HandleCloseClicked();

    // Reloads the saved-run list.
    UFUNCTION()
    void HandleRefreshClicked();

    // Opens the Overview tab.
    UFUNCTION()
    void HandleOverviewTabClicked();

    // Opens the Heatmaps tab.
    UFUNCTION()
    void HandleHeatmapsTabClicked();

    // Opens the FDOT tab.
    UFUNCTION()
    void HandleFDOTTabClicked();

    // Opens the Compare tab.
    UFUNCTION()
    void HandleCompareTabClicked();

    // Saves the second run chosen for comparison.
    UFUNCTION()
    void HandleComparisonRunChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

    // Starts a comparison between two runs.
    UFUNCTION()
    void HandleCompareRunsClicked();

    // Reverses which run is the baseline.
    UFUNCTION()
    void HandleSwapComparisonRunsClicked();

    // Saves the comparison heatmap metric.
    UFUNCTION()
    void HandleComparisonMetricChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

    // Saves the comparison road group.
    UFUNCTION()
    void HandleComparisonFocusChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

    // Starts creation of all three comparison maps.
    UFUNCTION()
    void HandleGenerateComparisonHeatmapsClicked();

    // Opens the comparison map viewer.
    UFUNCTION()
    void HandleViewComparisonHeatmapsClicked();

    // Shows the baseline comparison map.
    UFUNCTION()
    void HandleBaselineHeatmapModeClicked();

    // Shows the second run's comparison map.
    UFUNCTION()
    void HandleComparisonHeatmapModeClicked();

    // Shows the road-change comparison map.
    UFUNCTION()
    void HandleChangeHeatmapModeClicked();

    // Starts or confirms deletion of the selected run.
    UFUNCTION()
    void HandleDeleteRunClicked();

    // Saves the single-run heatmap metric.
    UFUNCTION()
    void HandleMetricSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

    // Saves the single-run road group.
    UFUNCTION()
    void HandleFocusSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

    // Generates or views the selected heatmap.
    UFUNCTION()
    void HandlePrimaryHeatmapClicked();

    // Starts a new single-run heatmap job.
    UFUNCTION()
    void HandleGenerateHeatmapClicked();

    // Opens an existing single-run heatmap.
    UFUNCTION()
    void HandleViewHeatmapClicked();

    // Starts the selected run's FDOT comparison.
    UFUNCTION()
    void HandleCompareFDOTClicked();

    // Opens the FDOT comparison map.
    UFUNCTION()
    void HandleViewFDOTHeatmapClicked();

    // Closes the full-screen heatmap viewer.
    UFUNCTION()
    void HandleCloseHeatmapClicked();

    // Zooms the map in one step.
    UFUNCTION()
    void HandleZoomInClicked();

    // Zooms the map out one step.
    UFUNCTION()
    void HandleZoomOutClicked();

    // Resets map zoom and position.
    UFUNCTION()
    void HandleResetHeatmapViewClicked();

    // Current run and option selections.
    TArray<FTelemetryRunInfo> SavedRuns;
    int32 SelectedRunIndex = INDEX_NONE;
    int32 ComparisonRunIndex = INDEX_NONE;
    TArray<int32> ComparisonRunIndices;
    FString SelectedMetric = TEXT("bottleneck_score");
    FString SelectedFocus = TEXT("all");
    FString SelectedComparisonMetric = TEXT("bottleneck_score");
    FString SelectedComparisonFocus = TEXT("all");
    FTelemetryHeatmapComparisonPaths ComparisonHeatmapPaths;
    FString ComparisonHeatmapBaselineLabel;
    FString ComparisonHeatmapComparisonLabel;
    // Short-lived panel state.
    bool bDeleteConfirmationPending = false;
    bool bTelemetryInputModeActive = false;
    bool bAddedMoveInputIgnore = false;
    bool bAddedLookInputIgnore = false;
    FString CurrentHeatmapMetric;
    FString ActiveMapName;
    int32 ActiveWorkspaceTab = 0;
    bool bTelemetryTaskRunning = false;
    // Zoom, pan, hover, and pinned-road state for the map viewer.
    bool bIsHeatmapPanning = false;
    bool bHeatmapPointerPressed = false;
    bool bHeatmapDragMoved = false;
    float HeatmapZoom = 1.0f;
    FVector2D HeatmapPan = FVector2D::ZeroVector;
    FVector2D LastPanPointerScreenPosition = FVector2D::ZeroVector;
    FVector2D HeatmapPointerDownScreenPosition = FVector2D::ZeroVector;
    FVector2D LastHeatmapPointerScreenPosition = FVector2D::ZeroVector;
    FVector2D HoveredRoadNormalizedPoint = FVector2D::ZeroVector;
    FVector2D PinnedRoadNormalizedPoint = FVector2D::ZeroVector;
    FVector2D ExtremeRoadNormalizedPoint = FVector2D::ZeroVector;
    int32 HoveredHeatmapRoadIndex = INDEX_NONE;
    int32 PinnedHeatmapRoadIndex = INDEX_NONE;
    int32 ExtremeHeatmapRoadIndex = INDEX_NONE;
    // Only update the automatic marker for a few layout frames after opening a map.
    int32 HeatmapMarkerLayoutFramesRemaining = 0;
    // Set by the Refresh button, consumed by NativeTick: the run rows are
    // destroyed and recreated, so it must not run inside the click.
    bool bRunListRefreshPending = false;
    FTelemetryHeatmapDisplayInfo CurrentHeatmapDisplayInfo;

    // Saved-run widgets.
    UPROPERTY()
    TObjectPtr<UVerticalBox> RunListBox;

    UPROPERTY()
    TObjectPtr<UTextBlock> RunDetailsText;

    UPROPERTY()
    TObjectPtr<UTextBlock> StatusText;

    UPROPERTY()
    TObjectPtr<UTextBlock> FDOTValidationText;

    // Workspace tab widgets.
    UPROPERTY()
    TObjectPtr<UWidgetSwitcher> WorkspaceSwitcher;

    UPROPERTY()
    TObjectPtr<UButton> OverviewTabButton;

    UPROPERTY()
    TObjectPtr<UButton> HeatmapsTabButton;

    UPROPERTY()
    TObjectPtr<UButton> FDOTTabButton;

    UPROPERTY()
    TObjectPtr<UButton> CompareTabButton;

    // Run comparison controls and results.
    UPROPERTY()
    TObjectPtr<UTextBlock> ComparisonBaselineText;

    UPROPERTY()
    TObjectPtr<UComboBoxString> ComparisonRunComboBox;

    UPROPERTY()
    TObjectPtr<UButton> CompareRunsButton;

    UPROPERTY()
    TObjectPtr<UButton> SwapComparisonRunsButton;

    UPROPERTY()
    TObjectPtr<UVerticalBox> ComparisonResultsBox;

    UPROPERTY()
    TObjectPtr<UComboBoxString> ComparisonMetricComboBox;

    UPROPERTY()
    TObjectPtr<UComboBoxString> ComparisonFocusComboBox;

    UPROPERTY()
    TObjectPtr<UButton> GenerateComparisonHeatmapsButton;

    UPROPERTY()
    TObjectPtr<UButton> ViewComparisonHeatmapsButton;

    UPROPERTY()
    TObjectPtr<UButton> DeleteRunButton;

    UPROPERTY()
    TObjectPtr<UTextBlock> DeleteRunButtonText;

    // Single-run heatmap controls.
    UPROPERTY()
    TObjectPtr<UComboBoxString> MetricComboBox;

    UPROPERTY()
    TObjectPtr<UComboBoxString> FocusComboBox;

    UPROPERTY()
    TObjectPtr<UTextBlock> MetricHelpText;

    UPROPERTY()
    TObjectPtr<UButton> PrimaryHeatmapButton;

    UPROPERTY()
    TObjectPtr<UTextBlock> PrimaryHeatmapButtonText;

    UPROPERTY()
    TObjectPtr<UButton> RegenerateHeatmapButton;

    // FDOT comparison controls.
    UPROPERTY()
    TObjectPtr<UButton> CompareFDOTButton;

    UPROPERTY()
    TObjectPtr<UButton> ViewFDOTMapButton;

    // Full-screen heatmap viewer widgets.
    UPROPERTY()
    TObjectPtr<UOverlay> HeatmapViewer;

    UPROPERTY()
    TObjectPtr<UImage> HeatmapImage;

    UPROPERTY()
    TObjectPtr<UWidget> HeatmapViewport;

    UPROPERTY()
    TObjectPtr<UTextBlock> HeatmapZoomText;

    UPROPERTY()
    TObjectPtr<UTextBlock> HeatmapInteractionHint;

    UPROPERTY()
    TObjectPtr<UHorizontalBox> ComparisonHeatmapModeBar;

    UPROPERTY()
    TObjectPtr<UButton> BaselineHeatmapModeButton;

    UPROPERTY()
    TObjectPtr<UButton> ComparisonHeatmapModeButton;

    UPROPERTY()
    TObjectPtr<UButton> ChangeHeatmapModeButton;

    // Text and marker shown when the user points at a road.
    UPROPERTY()
    TObjectPtr<UCanvasPanel> HeatmapMarkerLayer;

    UPROPERTY()
    TObjectPtr<UWidget> HeatmapRoadMarker;

    UPROPERTY()
    TObjectPtr<UWidget> HeatmapExtremeMarker;

    UPROPERTY()
    TObjectPtr<UBorder> HeatmapRoadCard;

    UPROPERTY()
    TObjectPtr<UTextBlock> HeatmapRoadNameText;

    UPROPERTY()
    TObjectPtr<UTextBlock> HeatmapRoadDetailsText;

    UPROPERTY()
    TObjectPtr<UTextBlock> HeatmapRoadPinText;

    // Summary and legend cards drawn around the SVG map.
    UPROPERTY()
    TObjectPtr<UTextBlock> HeatmapTitleText;

    UPROPERTY()
    TObjectPtr<UVerticalBox> HeatmapSummaryBox;

    UPROPERTY()
    TObjectPtr<UImage> HeatmapLegendGradient;

    UPROPERTY()
    TObjectPtr<UVerticalBox> HeatmapLegendTicks;

    UPROPERTY()
    TObjectPtr<UTextBlock> HeatmapLegendLabel;

    UPROPERTY()
    TObjectPtr<UWidget> HeatmapSummaryCard;

    UPROPERTY()
    TObjectPtr<UWidget> HeatmapLegendCard;

    // Unreal keeps the legend texture alive while the panel is open.
    UPROPERTY()
    TObjectPtr<UTexture2D> LoadedLegendTexture;

    UPROPERTY()
    TArray<TObjectPtr<UTelemetryRunButton>> RunRows;
};
