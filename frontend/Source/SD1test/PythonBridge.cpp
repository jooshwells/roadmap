#include "PythonBridge.h"
#include "HAL/PlatformProcess.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

bool PythonBridge::RunTelemetryAnalysis(
    const FString& PythonExePath,
    const FString& ScriptPath,
    const FString& SimulationCsvPath
)
{
    FString Args = FString::Printf(
        TEXT("\"%s\" \"%s\""),
        *ScriptPath,
        *SimulationCsvPath
    );

    UE_LOG(LogTemp, Warning, TEXT("Starting Python telemetry script in background..."));
    UE_LOG(LogTemp, Warning, TEXT("Python path: %s"), *PythonExePath);
    UE_LOG(LogTemp, Warning, TEXT("Arguments: %s"), *Args);

    FString DoneFile = FPaths::Combine(
        FPaths::ProjectContentDir(),
        TEXT("ThirdParty/python_pipeline/telemetry/telemetry_done.txt")
    );

    FPaths::CollapseRelativeDirectories(DoneFile);

    IFileManager::Get().Delete(*DoneFile);

    // Launch the Python pipeline without blocking Unreal.
    FProcHandle ProcHandle = FPlatformProcess::CreateProc(
        *PythonExePath,
        *Args,
        true,   // detached
        true,   // hidden
        true,   // really hidden
        nullptr,
        0,
        nullptr,
        nullptr
    );

    if (!ProcHandle.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to start Python process."));
        return false;
    }

    // Do not wait here. Waiting freezes Unreal until Python finishes.
    FPlatformProcess::CloseProc(ProcHandle);

    UE_LOG(LogTemp, Warning, TEXT("Python telemetry script launched successfully."));
    return true;
}