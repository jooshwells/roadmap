#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "TelemetryPanelBridge.h"
#include "TelemetryPanelWidget.generated.h"

class UComboBoxString;
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

    TArray<FTelemetryRunInfo> SavedRuns;
    int32 SelectedRunIndex = INDEX_NONE;
    FString SelectedMetric = TEXT("bottleneck_score");
    FString SelectedFocus = TEXT("all");
    FString ActiveMapName;
    int32 ActiveWorkspaceTab = 0;
    bool bTelemetryTaskRunning = false;

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
