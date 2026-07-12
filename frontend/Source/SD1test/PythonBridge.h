#pragma once

#include "CoreMinimal.h"

class PythonBridge
{
public:
    // Starts the Python telemetry pipeline using the simulation results
    // and the active roadmap's node and edge JSONL files.
    static bool RunTelemetryAnalysis(
        const FString& PipelineExePath,
        const FString& SimulationCsvPath,
        const FString& NodesJsonlPath,
        const FString& EdgesJsonlPath
    );
};