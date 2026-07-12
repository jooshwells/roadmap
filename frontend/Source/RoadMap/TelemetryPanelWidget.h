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
    // Builds the widget tree the first time this widget is initialized.
    virtual bool Initialize() override;

private:
    // Builds every visible panel and heatmap-viewer widget.
    void BuildWidgetTree();

    // Creates consistently styled text for the telemetry interface.
    UTextBlock* MakeText(
        const FString& Text,
        int32 Size,
        const FLinearColor& Color,
        const FName& Typeface = FName("Regular"),
        int32 LetterSpacing = 0
    );

    // Creates a primary or secondary RoadMap action button.
    UButton* MakeActionButton(const FString& Label, bool bPrimary);

    // Creates one selectable row for a saved simulation run.
    UTelemetryRunButton* MakeRunRow(int32 RunIndex, const FTelemetryRunInfo& RunInfo);

    // Loads the saved runs directly through the C++ telemetry bridge.
    void RefreshRunList();

    // Loads and formats details for the currently selected run.
    void RefreshSelectedRunDetails();

    // Updates each run row so the selected row has the amber highlight.
    void RefreshRunRowStyles();

    // Converts a readable dropdown option into the Python metric ID.
    FString GetMetricId(const FString& DisplayName) const;

    // Converts the active metric ID into a readable heatmap title.
    FString GetMetricDisplayName(const FString& MetricId) const;

    // Displays a normal or error status message at the bottom of the panel.
    void SetStatus(const FString& Message, bool bIsError = false);

    // Selects a saved run when its list row is clicked.
    void HandleRunSelected(int32 RunIndex);

    // Closes the complete telemetry panel.
    UFUNCTION()
    void HandleCloseClicked();

    // Reloads the saved telemetry run folders.
    UFUNCTION()
    void HandleRefreshClicked();

    // Keeps the readable metric option and internal metric ID synchronized.
    UFUNCTION()
    void HandleMetricSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

    // Launches Python to generate only the selected metric heatmap.
    UFUNCTION()
    void HandleGenerateHeatmapClicked();

    // Loads an existing heatmap PNG and opens the large viewer overlay.
    UFUNCTION()
    void HandleViewHeatmapClicked();

    // Closes the large heatmap viewer without closing the telemetry panel.
    UFUNCTION()
    void HandleCloseHeatmapClicked();

    TArray<FTelemetryRunInfo> SavedRuns;
    int32 SelectedRunIndex = INDEX_NONE;
    FString SelectedMetric = TEXT("bottleneck_score");

    UPROPERTY()
    TObjectPtr<UVerticalBox> RunListBox;

    UPROPERTY()
    TObjectPtr<UTextBlock> RunDetailsText;

    UPROPERTY()
    TObjectPtr<UTextBlock> StatusText;

    UPROPERTY()
    TObjectPtr<UComboBoxString> MetricComboBox;

    UPROPERTY()
    TObjectPtr<UOverlay> HeatmapViewer;

    UPROPERTY()
    TObjectPtr<UImage> HeatmapImage;

    UPROPERTY()
    TObjectPtr<UTextBlock> HeatmapTitleText;

    UPROPERTY()
    TObjectPtr<UVerticalBox> HeatmapSummaryBox;

    UPROPERTY()
    TObjectPtr<UVerticalBox> HeatmapLegendBar;

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
    TArray<TObjectPtr<UTelemetryRunButton>> RunRows;
};
