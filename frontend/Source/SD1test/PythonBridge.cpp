#include "PythonBridge.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

bool PythonBridge::RunTelemetryAnalysis(
    const FString& ExecutablePath,
    const FString& SimulationCsvPath
)
{
    // Convert to absolute OS-native paths
    FString FullExePath = FPaths::ConvertRelativePathToFull(ExecutablePath);
    FPaths::MakePlatformFilename(FullExePath);
    
    FString FullCsvPath = FPaths::ConvertRelativePathToFull(SimulationCsvPath);
    FPaths::MakePlatformFilename(FullCsvPath);
    
    // Set explicit working directory
    FString WorkingDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
    FPaths::MakePlatformFilename(WorkingDir);

    FString Args = FString::Printf(TEXT("\"%s\""), *FullCsvPath);

    UE_LOG(LogTemp, Warning, TEXT("Starting telemetry executable: %s"), *FullExePath);

    // Update the DoneFile path to the new packaged location
    FString DoneFile = FPaths::Combine(
        FPaths::ProjectContentDir(),
        TEXT("ThirdParty/Telemetry/telemetry_done.txt")
    );
    FPaths::CollapseRelativeDirectories(DoneFile);
    IFileManager::Get().Delete(*DoneFile);

    FProcHandle ProcHandle = FPlatformProcess::CreateProc(
        *FullExePath,
        *Args,
        true,   // detached
        true,   // hidden
        true,   // really hidden
        nullptr,
        0,
        *WorkingDir, // Crucial for PyInstaller
        nullptr,
        nullptr
    );

    if (!ProcHandle.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to start telemetry executable."));
        return false;
    }

    FPlatformProcess::CloseProc(ProcHandle);
    UE_LOG(LogTemp, Warning, TEXT("Telemetry executable launched successfully."));
    return true;
}