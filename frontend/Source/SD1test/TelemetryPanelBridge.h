#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "TelemetryPanelBridge.generated.h"

UCLASS()
class SD1TEST_API UTelemetryPanelBridge : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:

    // Runs list_available_metrics.py and returns the JSON output as a string.
    UFUNCTION(BlueprintCallable, Category = "RoadMap Telemetry")
    static bool ListAvailableMetrics(FString& OutJson);

    // Runs list_runs.py and returns the JSON output as a string.
    UFUNCTION(BlueprintCallable, Category = "RoadMap Telemetry")
    static bool ListRuns(FString& OutJson);

    // Runs get_run_details.py for one run and returns the JSON output as a string.
    UFUNCTION(BlueprintCallable, Category = "RoadMap Telemetry")
    static bool GetRunDetails(const FString& RunId, FString& OutJson);

    // Runs get_heatmap_path.py for one run and metric, then returns the JSON output as a string.
    UFUNCTION(BlueprintCallable, Category = "RoadMap Telemetry")
    static bool GetHeatmapPath(const FString& RunId, const FString& Metric, FString& OutJson);

    // Runs generate_selected_heatmap.py for one run and metric.
    UFUNCTION(BlueprintCallable, Category = "RoadMap Telemetry")
    static bool GenerateSelectedHeatmap(const FString& RunId, const FString& Metric, FString& OutJson);

private:

    // Finds the packaged telemetry EXE inside Content/ThirdParty.
    static FString GetTelemetryExePath();

    // Runs one telemetry Python script and returns anything printed by Python.
    static bool RunTelemetryScript(const FString& RelativeScriptPath, const TArray<FString>& Arguments, FString& OutJson);
};