#pragma once

#include "CoreMinimal.h"

class PythonBridge
{
public:
    static bool RunTelemetryAnalysis(
        const FString& PipelineExePath,
        const FString& SimulationCsvPath,
        const FString& NetworkGraphCsvPath,
        const FString& EdgesJsonlPath
    );
};