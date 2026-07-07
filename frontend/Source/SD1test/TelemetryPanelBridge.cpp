#include "TelemetryPanelBridge.h"

#include "HAL/PlatformFilemanager.h"
#include "Misc/Paths.h"


// Finds the python.exe inside the RoadMap python_pipeline virtual environment.
FString UTelemetryPanelBridge::GetPythonExePath()
{
    const FString ProjectDir = FPaths::ProjectDir();

    return FPaths::ConvertRelativePathToFull(
        FPaths::Combine(ProjectDir, TEXT(".."), TEXT("python_pipeline"), TEXT(".venv"), TEXT("Scripts"), TEXT("python.exe"))
    );
}


// Finds the telemetry folder that Unreal uses inside Content/ThirdParty.
FString UTelemetryPanelBridge::GetTelemetryRootPath()
{
    const FString ProjectDir = FPaths::ProjectDir();

    return FPaths::ConvertRelativePathToFull(
        FPaths::Combine(ProjectDir, TEXT("Content"), TEXT("ThirdParty"), TEXT("python_pipeline"), TEXT("telemetry"))
    );
}


// Runs one telemetry Python script and returns anything printed by Python.
bool UTelemetryPanelBridge::RunTelemetryScript(const FString& RelativeScriptPath, const TArray<FString>& Arguments, FString& OutJson)
{
    OutJson.Empty();

    const FString PythonExePath = GetPythonExePath();
    const FString TelemetryRootPath = GetTelemetryRootPath();

    const FString ScriptPath = FPaths::ConvertRelativePathToFull(
        FPaths::Combine(TelemetryRootPath, RelativeScriptPath)
    );

    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

    if (!PlatformFile.FileExists(*PythonExePath))
    {
        OutJson = FString::Printf(TEXT("{\"success\":false,\"error\":\"Python executable not found.\",\"path\":\"%s\"}"), *PythonExePath);
        return false;
    }

    if (!PlatformFile.FileExists(*ScriptPath))
    {
        OutJson = FString::Printf(TEXT("{\"success\":false,\"error\":\"Telemetry script not found.\",\"path\":\"%s\"}"), *ScriptPath);
        return false;
    }

    FString Params = FString::Printf(TEXT("\"%s\""), *ScriptPath);

    for (const FString& Arg : Arguments)
    {
        Params += TEXT(" ");
        Params += Arg;
    }

    FString StdOut;
    FString StdErr;
    int32 ReturnCode = -1;

    const bool bStarted = FPlatformProcess::ExecProcess(
        *PythonExePath,
        *Params,
        &ReturnCode,
        &StdOut,
        &StdErr
    );

    if (!bStarted)
    {
        OutJson = TEXT("{\"success\":false,\"error\":\"Failed to start telemetry Python process.\"}");
        return false;
    }

    if (ReturnCode != 0)
    {
        OutJson = FString::Printf(
            TEXT("{\"success\":false,\"error\":\"Telemetry Python script failed.\",\"return_code\":%d,\"stderr\":\"%s\"}"),
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