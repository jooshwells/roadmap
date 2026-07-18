#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Engine/Texture2D.h"
#include "TelemetryPanelBridge.generated.h"

// Basic information used to build one row in the saved-runs list.
USTRUCT(BlueprintType)
struct FTelemetryRunInfo
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    FString RunId;

    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    FString Status;

    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    FString MapName;

    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    FString MapId;

    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    FString CreatedAt;

    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    int32 TotalVehicles = 0;

    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    float AverageSpeedMph = 0.0f;
};

// Detailed values shown on the Overview tab for the selected run.
USTRUCT(BlueprintType)
struct FTelemetryRunDetails
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    FString RunId;

    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    FString Status;

    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    int32 TotalVehicles = 0;

    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    float SimulationDurationSeconds = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    int32 EdgesUsed = 0;

    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    float AverageSpeedMph = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    float TotalWaitAddedSeconds = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    float MaximumWaitSeconds = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    FString WorstBottleneckRoad;

    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    int32 WorstBottleneckEdgeId = INDEX_NONE;

    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    float WorstBottleneckScore = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    int32 BottleneckIndexVersion = 1;
};

// Stores one label and value displayed in the heatmap summary card.
USTRUCT(BlueprintType)
struct FTelemetryHeatmapSummaryRow
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    FString Label;

    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    FString Value;
};

// One high-level value compared between a baseline and a second simulation run.
USTRUCT(BlueprintType)
struct FTelemetryRunComparisonMetric
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry") FString Label;
    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry") FString Unit;
    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry") FString Status;
    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry") FString Explanation;
    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry") float BaselineValue = 0.0f;
    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry") float ComparisonValue = 0.0f;
    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry") float Delta = 0.0f;
    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry") float PercentChange = 0.0f;
    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry") bool bHasPercentChange = false;
};

// One shared road with a notable change between the selected runs.
USTRUCT(BlueprintType)
struct FTelemetryRunRoadChange
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry") int32 EdgeId = INDEX_NONE;
    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry") FString RoadName;
    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry") FString Status;
    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry") float BaselineSpeedMph = 0.0f;
    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry") float ComparisonSpeedMph = 0.0f;
    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry") float SpeedDeltaMph = 0.0f;
    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry") float BaselineWaitSeconds = 0.0f;
    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry") float ComparisonWaitSeconds = 0.0f;
    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry") float WaitDeltaSeconds = 0.0f;
    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry") float BottleneckDelta = 0.0f;
};

// Complete result used by the run-comparison tab.
USTRUCT(BlueprintType)
struct FTelemetryRunComparisonResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry") FString BaselineRunId;
    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry") FString ComparisonRunId;
    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry") FString BaselineCreatedAt;
    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry") FString ComparisonCreatedAt;
    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry") int32 SharedRoads = 0;
    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry") float SharedCoveragePercent = 0.0f;
    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry") bool bPreliminary = false;
    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry") TArray<FString> Warnings;
    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry") TArray<FTelemetryRunComparisonMetric> Metrics;
    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry") TArray<FTelemetryRunRoadChange> TopRoadChanges;
};

// Files generated for the three run-comparison map views.
USTRUCT(BlueprintType)
struct FTelemetryHeatmapComparisonPaths
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry") FString BaselinePath;
    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry") FString ComparisonPath;
    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry") FString ChangePath;
    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry") int32 SharedRoads = 0;
};

// One analyzed road and its map shape for mouse hover and clicks.
USTRUCT(BlueprintType)
struct FTelemetryHeatmapRoad
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    int32 EdgeId = INDEX_NONE;

    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    FString RoadName;

    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    FString RouteRef;

    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    FString HighwayType;

    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    float MetricValue = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    bool bIsComparison = false;

    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    float BaselineValue = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    float ComparisonValue = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    float RawDelta = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    FString ComparisonStatus;

    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    FString ComparisonMetric;

    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    FString ComparisonUnit;

    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    TArray<FVector2D> Points;
};

// Stores the information Unreal draws around the SVG road map.
USTRUCT(BlueprintType)
struct FTelemetryHeatmapDisplayInfo
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    FString Title;

    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    FString LegendLabel;

    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    TArray<FString> LegendTicksTopToBottom;

    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    TArray<FString> LegendColorsTopToBottom;

    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    TArray<FTelemetryHeatmapSummaryRow> SummaryRows;

    // Position of the road map inside the full SVG image.
    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    FVector4 MapRect = FVector4(0.0f, 0.0f, 1.0f, 1.0f);

    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    TArray<FTelemetryHeatmapRoad> Roads;
};

// Stores one high-priority road difference from an FDOT reference comparison.
USTRUCT(BlueprintType)
struct FTelemetryFDOTRoadDifference
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    FString RoadName;

    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    float SimulationFlowVehPerHour = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    float FDOTFlowVehPerHour = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    float PercentDifference = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    float GEHScore = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    FString Result;
};

// Stores the compact FDOT reference summary shown for one selected run.
USTRUCT(BlueprintType)
struct FTelemetryFDOTValidationSummary
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    int32 MatchedEdges = 0;

    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    int32 GoodEdges = 0;

    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    int32 ReviewEdges = 0;

    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    int32 PoorEdges = 0;

    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    float GoodPercent = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    float MeanGEH = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    float SimulationDurationSeconds = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    int32 TotalRoadDirections = 0;

    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    int32 UnmatchedRoadDirections = 0;

    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    float CoveragePercent = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    bool bPreliminary = false;

    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    FString Warning;

    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    TArray<FTelemetryFDOTRoadDifference> TopRoadDifferences;
};

UCLASS()
class ROADMAP_API UTelemetryPanelBridge : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    // Run list and overview data.

    // Gets saved runs and puts their values into a form the panel can read.
    UFUNCTION(BlueprintCallable, Category = "RoadMap Telemetry")
    static bool GetSavedRuns(
        const FString& ActiveMapName,
        TArray<FTelemetryRunInfo>& OutRuns,
        FString& OutError
    );

    // Gets one saved run and reads the values shown on the Overview tab.
    UFUNCTION(BlueprintCallable, Category = "RoadMap Telemetry")
    static bool GetSavedRunDetails(
        const FString& RunId,
        FTelemetryRunDetails& OutDetails,
        FString& OutError
    );

    // Run comparison actions.
    // Compares two saved runs from the same map using the packaged Python tool.
    static bool CompareSavedRuns(
        const FString& BaselineRunId,
        const FString& ComparisonRunId,
        FTelemetryRunComparisonResult& OutResult,
        FString& OutError
    );

    // Creates the baseline, second-run, and change heatmap files.
    static bool GenerateComparisonHeatmaps(
        const FString& BaselineRunId,
        const FString& ComparisonRunId,
        const FString& Metric,
        const FString& Focus,
        FTelemetryHeatmapComparisonPaths& OutPaths,
        FString& OutError
    );

    // Removes one saved run after Python checks that its path is safe.
    static bool DeleteSavedRun(const FString& RunId, FString& OutError);

    // Heatmap files and display data.
    // Gets the full file path for an existing generated heatmap.
    // This gives the panel a path and an error message without extra file reading.
    UFUNCTION(BlueprintCallable, Category = "RoadMap Telemetry")
    static bool GetGeneratedHeatmapPath(
        const FString& RunId,
        const FString& Metric,
        const FString& Focus,
        FString& OutHeatmapPath,
        FString& OutError
    );

    // Loads the title, legend, summary, and road data saved beside a heatmap.
    static bool GetHeatmapDisplayInfo(
        const FString& HeatmapPath,
        FTelemetryHeatmapDisplayInfo& OutInfo,
        FString& OutError
    );

    // Loads a generated heatmap PNG from disk as an Unreal texture.
    UFUNCTION(BlueprintCallable, Category = "RoadMap Telemetry")
    static UTexture2D* LoadHeatmapTexture(
        const FString& HeatmapPath,
        FString& OutError
    );

    // Generates one metric and road-focus combination for a saved run.
    UFUNCTION(BlueprintCallable, Category = "RoadMap Telemetry")
    static bool GenerateSelectedHeatmap(
        const FString& RunId,
        const FString& Metric,
        const FString& Focus,
        FString& OutJson
    );

    // FDOT comparison.
    // Compares one saved run with its map-specific FDOT design-hour targets.
    UFUNCTION(BlueprintCallable, Category = "RoadMap Telemetry")
    static bool CompareSelectedRunWithFDOT(
        const FString& RunId,
        FTelemetryFDOTValidationSummary& OutSummary,
        FString& OutError
    );

private:

    // Finds the folder where saved telemetry runs are stored.
    static FString GetTelemetryRunsPath();

    // Finds the packaged telemetry EXE inside Content/ThirdParty.
    static FString GetTelemetryExePath();

    // Sends one command to the packaged telemetry executable.
    static bool RunTelemetryCommand(const FString& Command, const TArray<FString>& Arguments, FString& OutJson);
};
