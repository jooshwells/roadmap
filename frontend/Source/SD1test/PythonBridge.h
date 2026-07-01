#pragma once

#include "CoreMinimal.h"

// Handles running Python scripts from Unreal.
// This is used after the simulation finishes so Python can process the telemetry CSV.
class PythonBridge
{
public:
    static bool RunTelemetryAnalysis(
        const FString& ExecutablePath,
        const FString& SimulationCsvPath
    );
};