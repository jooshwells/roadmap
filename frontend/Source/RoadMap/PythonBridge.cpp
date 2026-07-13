#include "PythonBridge.h"
#include "HAL/PlatformProcess.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Async/Async.h" // Required for asynchronous pipe reading

bool PythonBridge::RunTelemetryAnalysis(
    const FString& PipelineExePath,
    const FString& SimulationCsvPath,
    const FString& NetworkGraphCsvPath,
    const FString& EdgesJsonlPath
)
{
    // Positional args: simulation CSV, network-graph CSV, active edges JSONL.
    FString Args = FString::Printf(
        TEXT("\"%s\" \"%s\" \"%s\""),
        *SimulationCsvPath,
        *NetworkGraphCsvPath,
        *EdgesJsonlPath
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

    // 1. Create Read/Write pipes for IPC
    void* ReadPipe = nullptr;
    void* WritePipe = nullptr;
    if (!FPlatformProcess::CreatePipe(ReadPipe, WritePipe))
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to create anonymous pipes for telemetry process."));
        return false;
    }

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
        WritePipe,
        nullptr,
        WritePipe
    );

    if (!ProcHandle.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to launch telemetry executable."));
        return false;
    }
    FPlatformProcess::ClosePipe(nullptr, WritePipe);
    Async(EAsyncExecution::Thread, [ProcHandle, ReadPipe]() mutable
        {
            while (FPlatformProcess::IsProcRunning(ProcHandle))
            {
                FString NewOutput = FPlatformProcess::ReadPipe(ReadPipe);
                if (!NewOutput.IsEmpty())
                {
                    // Dispatch logs back to the Game Thread for thread-safe UI/UObject updates
                    AsyncTask(ENamedThreads::GameThread, [NewOutput]()
                        {
                            UE_LOG(LogTemp, Log, TEXT("[Python]: %s"), *NewOutput);
                        });
                }
                // Sleep briefly to prevent this thread from pegging a CPU core at 100%
                FPlatformProcess::Sleep(0.05f);
            }

            // 5. Drain any remaining buffered output after process termination
            FString FinalOutput = FPlatformProcess::ReadPipe(ReadPipe);
            if (!FinalOutput.IsEmpty())
            {
                AsyncTask(ENamedThreads::GameThread, [FinalOutput]()
                    {
                        UE_LOG(LogTemp, Log, TEXT("[Python]: %s"), *FinalOutput);
                    });
            }

            // 6. Clean up process handle and read pipe
            int32 ReturnCode = 0;
            FPlatformProcess::GetProcReturnCode(ProcHandle, &ReturnCode);
            FPlatformProcess::CloseProc(ProcHandle);
            FPlatformProcess::ClosePipe(ReadPipe, nullptr);

            AsyncTask(ENamedThreads::GameThread, [ReturnCode]()
                {
                    UE_LOG(LogTemp, Warning, TEXT("Telemetry executable finished with return code: %d"), ReturnCode);
                });
        });

    UE_LOG(LogTemp, Warning, TEXT("Telemetry executable launched successfully."));
    return true;
}