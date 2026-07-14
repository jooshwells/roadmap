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
    virtual bool Initialize() override;
    virtual FReply NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
    virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
    virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
    virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

private:
    // Panel setup and shared styling helpers.
    void BuildWidgetTree();

    UTextBlock* MakeText(
        const FString& Text,
        int32 Size,
        const FLinearColor& Color,
        const FName& Typeface = FName("Regular"),
        int32 LetterSpacing = 0
    );

    UButton* MakeActionButton(const FString& Label, bool bPrimary);

    UTelemetryRunButton* MakeRunRow(int32 RunIndex, const FTelemetryRunInfo& RunInfo);

    // Run selection and displayed details.
    void RefreshRunList();

    void RefreshSelectedRunDetails();

    void RefreshRunRowStyles();

    // Converts between UI labels and IDs used by the Python pipeline.
    FString GetMetricId(const FString& DisplayName) const;

    FString GetMetricDisplayName(const FString& MetricId) const;
    FString GetMetricHelpText(const FString& MetricId) const;
    FString GetFocusId(const FString& DisplayName) const;
    FString GetFocusDisplayName(const FString& FocusId) const;

    void SetStatus(const FString& Message, bool bIsError = false);

    // Builds a smooth native legend texture from the metric's ordered colors.
    void UpdateHeatmapLegendGradient(const TArray<FString>& ColorsTopToBottom);

    // Applies the current zoom and pan without changing the widget's layout.
    void ApplyHeatmapViewTransform();
    void SetHeatmapZoom(float NewZoom, const FVector2D* CursorScreenPosition = nullptr);
    void ResetHeatmapView();
    FVector2D ClampHeatmapPan(const FVector2D& RequestedPan) const;
    bool IsPointerOverHeatmap(const FVector2D& ScreenPosition) const;
    void UpdateHeatmapRoadHover(const FVector2D& ScreenPosition);
    bool FindNearestHeatmapRoad(
        const FVector2D& ScreenPosition,
        int32& OutRoadIndex,
        FVector2D& OutClosestNormalizedPoint
    ) const;
    void ShowHeatmapRoadDetails(int32 RoadIndex, bool bPinned);
    void ClearHeatmapRoadInteraction();
    void UpdateHeatmapRoadMarker();
    FString FormatHeatmapRoadValue(const FTelemetryHeatmapRoad& Road) const;

    // Switches the right-side workspace while keeping the selected run active.
    void SetWorkspaceTab(int32 TabIndex);
    void RefreshWorkspaceTabStyles();
    void RefreshHeatmapActionState();
    bool DoesSelectedHeatmapExist() const;
    void SetTelemetryTaskRunning(bool bIsRunning);
    void FinishHeatmapGeneration(
        const FString& RunId,
        const FString& Metric,
        const FString& Focus,
        bool bSucceeded
    );
    void FinishFDOTComparison(
        const FString& RunId,
        bool bSucceeded,
        const FTelemetryFDOTValidationSummary& Summary,
        const FString& ErrorMessage
    );

    // Widget event handlers.
    void HandleRunSelected(int32 RunIndex);

    UFUNCTION()
    void HandleCloseClicked();

    UFUNCTION()
    void HandleRefreshClicked();

    UFUNCTION()
    void HandleOverviewTabClicked();

    UFUNCTION()
    void HandleHeatmapsTabClicked();

    UFUNCTION()
    void HandleFDOTTabClicked();

    UFUNCTION()
    void HandleMetricSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

    UFUNCTION()
    void HandleFocusSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

    UFUNCTION()
    void HandlePrimaryHeatmapClicked();

    UFUNCTION()
    void HandleGenerateHeatmapClicked();

    UFUNCTION()
    void HandleViewHeatmapClicked();

    UFUNCTION()
    void HandleCompareFDOTClicked();

    UFUNCTION()
    void HandleViewFDOTHeatmapClicked();

    UFUNCTION()
    void HandleCloseHeatmapClicked();

    UFUNCTION()
    void HandleZoomInClicked();

    UFUNCTION()
    void HandleZoomOutClicked();

    UFUNCTION()
    void HandleResetHeatmapViewClicked();

    TArray<FTelemetryRunInfo> SavedRuns;
    int32 SelectedRunIndex = INDEX_NONE;
    FString SelectedMetric = TEXT("bottleneck_score");
    FString SelectedFocus = TEXT("all");
    FString CurrentHeatmapMetric;
    FString ActiveMapName;
    int32 ActiveWorkspaceTab = 0;
    bool bTelemetryTaskRunning = false;
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
    int32 HoveredHeatmapRoadIndex = INDEX_NONE;
    int32 PinnedHeatmapRoadIndex = INDEX_NONE;
    FTelemetryHeatmapDisplayInfo CurrentHeatmapDisplayInfo;

    UPROPERTY()
    TObjectPtr<UVerticalBox> RunListBox;

    UPROPERTY()
    TObjectPtr<UTextBlock> RunDetailsText;

    UPROPERTY()
    TObjectPtr<UTextBlock> StatusText;

    UPROPERTY()
    TObjectPtr<UTextBlock> FDOTValidationText;

    UPROPERTY()
    TObjectPtr<UWidgetSwitcher> WorkspaceSwitcher;

    UPROPERTY()
    TObjectPtr<UButton> OverviewTabButton;

    UPROPERTY()
    TObjectPtr<UButton> HeatmapsTabButton;

    UPROPERTY()
    TObjectPtr<UButton> FDOTTabButton;

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

    UPROPERTY()
    TObjectPtr<UButton> CompareFDOTButton;

    UPROPERTY()
    TObjectPtr<UButton> ViewFDOTMapButton;

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
    TObjectPtr<UCanvasPanel> HeatmapMarkerLayer;

    UPROPERTY()
    TObjectPtr<UWidget> HeatmapRoadMarker;

    UPROPERTY()
    TObjectPtr<UBorder> HeatmapRoadCard;

    UPROPERTY()
    TObjectPtr<UTextBlock> HeatmapRoadNameText;

    UPROPERTY()
    TObjectPtr<UTextBlock> HeatmapRoadDetailsText;

    UPROPERTY()
    TObjectPtr<UTextBlock> HeatmapRoadPinText;

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

    UPROPERTY()
    TObjectPtr<UTexture2D> LoadedHeatmapTexture;

    UPROPERTY()
    TObjectPtr<UTexture2D> LoadedLegendTexture;

    UPROPERTY()
    TArray<TObjectPtr<UTelemetryRunButton>> RunRows;
};
