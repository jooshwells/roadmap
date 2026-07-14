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
    float WorstBottleneckScore = 0.0f;
};

// Stores one label and value displayed in the native heatmap summary card.
USTRUCT(BlueprintType)
struct FTelemetryHeatmapSummaryRow
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    FString Label;

    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    FString Value;
};

// One analyzed road and its normalized shape for native hover/click hit testing.
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
    TArray<FVector2D> Points;
};

// Stores display information that Unreal draws around the vector road map.
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

    // Position of Matplotlib's map axes inside the fixed SVG canvas.
    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    FVector4 MapRect = FVector4(0.0f, 0.0f, 1.0f, 1.0f);

    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    TArray<FTelemetryHeatmapRoad> Roads;
};

// Stores one high-priority road difference from an FDOT validation.
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

// Stores the compact FDOT validation summary shown for one selected run.
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

    // Gets the saved telemetry runs and converts them into Blueprint-friendly structs.
    UFUNCTION(BlueprintCallable, Category = "RoadMap Telemetry")
    static bool GetSavedRuns(
        const FString& ActiveMapName,
        TArray<FTelemetryRunInfo>& OutRuns,
        FString& OutError
    );

    // Gets one saved run and converts its JSON summary into a Blueprint-friendly struct.
    UFUNCTION(BlueprintCallable, Category = "RoadMap Telemetry")
    static bool GetSavedRunDetails(
        const FString& RunId,
        FTelemetryRunDetails& OutDetails,
        FString& OutError
    );

    // Gets the full file path for an existing generated heatmap.
    // This returns Blueprint-friendly path and error outputs without JSON parsing.
    UFUNCTION(BlueprintCallable, Category = "RoadMap Telemetry")
    static bool GetGeneratedHeatmapPath(
        const FString& RunId,
        const FString& Metric,
        const FString& Focus,
        FString& OutHeatmapPath,
        FString& OutError
    );

    // Loads the sidecar information used by Unreal's native heatmap UI.
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
