#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Engine/Texture2D.h"
#include "TelemetryPanelBridge.generated.h"

// Stores the basic information for one saved telemetry run.
// Blueprint can use this struct to create readable run entries in the telemetry panel.
USTRUCT(BlueprintType)
struct FTelemetryRunInfo
{
    GENERATED_BODY()

    // Unique folder/run name used to identify this saved simulation run.
    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    FString RunId;

    // Current processing status of the saved telemetry run.
    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    FString Status;

    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    FString MapName;

    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    FString MapId;

    // Date and time when the telemetry run was created.
    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    FString CreatedAt;

    // Total number of vehicles recorded during the simulation.
    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    int32 TotalVehicles = 0;

    // Average vehicle speed recorded during the simulation.
    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    float AverageSpeedMph = 0.0f;
};

// Stores the detailed telemetry summary for one selected simulation run.
USTRUCT(BlueprintType)
struct FTelemetryRunDetails
{
    GENERATED_BODY()

    // Internal ID used to locate the saved run folder.
    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    FString RunId;

    // Current processing status for the saved run.
    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    FString Status;

    // Number of vehicles recorded during the simulation.
    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    int32 TotalVehicles = 0;

    // Length of the recorded simulation in seconds.
    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    float SimulationDurationSeconds = 0.0f;

    // Number of road edges used by at least one vehicle.
    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    int32 EdgesUsed = 0;

    // Average speed across the simulation.
    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    float AverageSpeedMph = 0.0f;

    // Total additional waiting time recorded across all vehicles.
    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    float TotalWaitAddedSeconds = 0.0f;

    // Longest individual vehicle wait recorded during the run.
    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    float MaximumWaitSeconds = 0.0f;

    // Readable road name for the highest-ranked bottleneck.
    UPROPERTY(BlueprintReadOnly, Category = "RoadMap Telemetry")
    FString WorstBottleneckRoad;

    // Calculated bottleneck score for the worst-ranked road.
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

    // Runs generate_selected_heatmap.py for one run and metric.
    UFUNCTION(BlueprintCallable, Category = "RoadMap Telemetry")
    static bool GenerateSelectedHeatmap(
        const FString& RunId,
        const FString& Metric,
        const FString& Focus,
        FString& OutJson
    );

private:

    // Finds the folder where saved telemetry runs are stored.
    static FString GetTelemetryRunsPath();

    // Finds the packaged telemetry EXE inside Content/ThirdParty.
    static FString GetTelemetryExePath();

    // Runs one telemetry Python script and returns anything printed by Python.
    static bool RunTelemetryScript(const FString& RelativeScriptPath, const TArray<FString>& Arguments, FString& OutJson);
};
