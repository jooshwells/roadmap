#include "PythonBridge.h"
#include "HAL/PlatformProcess.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

bool PythonBridge::RunTelemetryAnalysis(
    const FString& PipelineExePath,
    const FString& SimulationCsvPath
)
{
    FString Args = FString::Printf(
        TEXT("\"%s\""),
        *SimulationCsvPath
    );

    UE_LOG(LogTemp, Warning, TEXT("Starting telemetry executable in background..."));
    UE_LOG(LogTemp, Warning, TEXT("Telemetry executable: %s"), *PipelineExePath);
    UE_LOG(LogTemp, Warning, TEXT("Arguments: %s"), *Args);

    FString DoneFile = FPaths::Combine(
        FPaths::ProjectContentDir(),
        TEXT("ThirdParty/python_pipeline/telemetry/telemetry_done.txt")
    );

    FPaths::CollapseRelativeDirectories(DoneFile);

    IFileManager::Get().Delete(*DoneFile);

    // Launch the Python pipeline without blocking Unreal.
    FProcHandle ProcHandle = FPlatformProcess::CreateProc(
        *PipelineExePath,
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
        UE_LOG(LogTemp, Error, TEXT("Failed to launch telemetry executable."));
        return false;
    }

    // Do not wait here. Waiting freezes Unreal until Python finishes.
    FPlatformProcess::CloseProc(ProcHandle);

    UE_LOG(LogTemp, Warning, TEXT("Telemetry executable launched successfully."));
    return true;
}