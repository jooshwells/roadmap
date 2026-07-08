#include "TelemetryPanelBridge.h"

#include "HAL/PlatformFilemanager.h"
#include "Misc/Paths.h"


// Finds the packaged telemetry EXE inside Content/ThirdParty.
FString UTelemetryPanelBridge::GetTelemetryExePath()
{
    const FString ProjectDir = FPaths::ProjectDir();

    return FPaths::ConvertRelativePathToFull(
        FPaths::Combine(ProjectDir, TEXT("Content"), TEXT("ThirdParty"), TEXT("python_pipeline"), TEXT("telemetry"), TEXT("run_pipeline.exe"))
    );
}

// Runs one telemetry panel command through the packaged telemetry EXE.
bool UTelemetryPanelBridge::RunTelemetryScript(const FString& RelativeScriptPath, const TArray<FString>& Arguments, FString& OutJson)
{
    OutJson.Empty();

    const FString TelemetryExePath = GetTelemetryExePath();

    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

    if (!PlatformFile.FileExists(*TelemetryExePath))
    {
        OutJson = FString::Printf(
            TEXT("{\"success\":false,\"error\":\"Telemetry executable not found.\",\"path\":\"%s\"}"),
            *TelemetryExePath
        );

        return false;
    }

    // Tell the packaged telemetry EXE that this request came from the telemetry panel.
    FString Params = TEXT("--panel-command ");

    // Convert the old script path into the matching command inside run_pipeline.exe.
    if (RelativeScriptPath.Contains(TEXT("list_available_metrics.py")))
    {
        Params += TEXT("list-metrics");
    }
    else if (RelativeScriptPath.Contains(TEXT("list_runs.py")))
    {
        Params += TEXT("list-runs");
    }
    else if (RelativeScriptPath.Contains(TEXT("get_run_details.py")))
    {
        Params += TEXT("get-run-details");
    }
    else if (RelativeScriptPath.Contains(TEXT("get_heatmap_path.py")))
    {
        Params += TEXT("get-heatmap-path");
    }
    else if (RelativeScriptPath.Contains(TEXT("generate_selected_heatmap.py")))
    {
        Params += TEXT("generate-heatmap");
    }
    else
    {
        OutJson = FString::Printf(
            TEXT("{\"success\":false,\"error\":\"Unknown telemetry panel script.\",\"script\":\"%s\"}"),
            *RelativeScriptPath
        );

        return false;
    }

    for (const FString& Arg : Arguments)
    {
        Params += TEXT(" ");
        Params += Arg;
    }

    FString StdOut;
    FString StdErr;
    int32 ReturnCode = -1;

    const bool bStarted = FPlatformProcess::ExecProcess(
        *TelemetryExePath,
        *Params,
        &ReturnCode,
        &StdOut,
        &StdErr
    );

    if (!bStarted)
    {
        OutJson = TEXT("{\"success\":false,\"error\":\"Failed to start telemetry EXE process.\"}");
        return false;
    }

    if (ReturnCode != 0)
    {
        OutJson = FString::Printf(
            TEXT("{\"success\":false,\"error\":\"Telemetry EXE command failed.\",\"return_code\":%d,\"stderr\":\"%s\"}"),
            ReturnCode,
            *StdErr.ReplaceCharWithEscapedChar()
        );

        return false;
    }

    OutJson = StdOut.TrimStartAndEnd();
    return true;
}


// Runs list_available_metrics.py and returns the JSON output as a string.
bool UTelemetryPanelBridge::ListAvailableMetrics(FString& OutJson)
{
    return RunTelemetryScript(
        TEXT("src/telemetry/list_available_metrics.py"),
        {},
        OutJson
    );
}


// Runs list_runs.py and returns the JSON output as a string.
bool UTelemetryPanelBridge::ListRuns(FString& OutJson)
{
    return RunTelemetryScript(
        TEXT("src/telemetry/list_runs.py"),
        {},
        OutJson
    );
}


// Runs get_run_details.py for one run and returns the JSON output as a string.
bool UTelemetryPanelBridge::GetRunDetails(const FString& RunId, FString& OutJson)
{
    return RunTelemetryScript(
        TEXT("src/telemetry/get_run_details.py"),
        {
            FString::Printf(TEXT("--run-id \"%s\""), *RunId)
        },
        OutJson
    );
}


// Runs get_heatmap_path.py for one run and metric, then returns the JSON output as a string.
bool UTelemetryPanelBridge::GetHeatmapPath(const FString& RunId, const FString& Metric, FString& OutJson)
{
    return RunTelemetryScript(
        TEXT("src/telemetry/get_heatmap_path.py"),
        {
            FString::Printf(TEXT("--run-id \"%s\""), *RunId),
            FString::Printf(TEXT("--metric \"%s\""), *Metric)
        },
        OutJson
    );
}


// Runs generate_selected_heatmap.py for one run and metric.
bool UTelemetryPanelBridge::GenerateSelectedHeatmap(const FString& RunId, const FString& Metric, FString& OutJson)
{
    return RunTelemetryScript(
        TEXT("src/heatmaps/generate_selected_heatmap.py"),
        {
            FString::Printf(TEXT("--run-id \"%s\""), *RunId),
            FString::Printf(TEXT("--metric \"%s\""), *Metric)
        },
        OutJson
    );
}