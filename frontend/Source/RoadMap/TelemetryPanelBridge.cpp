#include "TelemetryPanelBridge.h"

#include "HAL/PlatformFilemanager.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "ImageUtils.h"

namespace
{
    FString FindTelemetryRunFolder(const FString& RunsDirectory, const FString& RunId)
    {
        const FString LegacyPath = FPaths::Combine(RunsDirectory, RunId);
        if (IFileManager::Get().DirectoryExists(*LegacyPath))
        {
            return LegacyPath;
        }

        TArray<FString> MetadataPaths;
        IFileManager::Get().FindFilesRecursive(
            MetadataPaths,
            *RunsDirectory,
            TEXT("run_metadata.json"),
            true,
            false,
            false
        );

        for (const FString& MetadataPath : MetadataPaths)
        {
            const FString Candidate = FPaths::GetPath(MetadataPath);
            if (FPaths::GetCleanFilename(Candidate) == RunId)
            {
                return Candidate;
            }
        }

        return FString();
    }
}


// Finds the packaged telemetry EXE inside Content/ThirdParty.
FString UTelemetryPanelBridge::GetTelemetryExePath()
{
    const FString ProjectDir = FPaths::ProjectDir();

    return FPaths::ConvertRelativePathToFull(
        FPaths::Combine(ProjectDir, TEXT("Content"), TEXT("ThirdParty"), TEXT("python_pipeline"), TEXT("telemetry"), TEXT("run_pipeline.exe"))
    );
}

// Finds the folder where the Python telemetry pipeline stores saved runs.
FString UTelemetryPanelBridge::GetTelemetryRunsPath()
{
    const FString ProjectDir = FPaths::ProjectDir();

    return FPaths::ConvertRelativePathToFull(
        FPaths::Combine(
            ProjectDir,
            TEXT("Content"),
            TEXT("ThirdParty"),
            TEXT("python_pipeline"),
            TEXT("telemetry"),
            TEXT("outputs"),
            TEXT("runs")
        )
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
    if (RelativeScriptPath.Contains(TEXT("generate_selected_heatmap.py")))
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


// Gets saved telemetry runs directly from the run folders without launching Python.
bool UTelemetryPanelBridge::GetSavedRuns(
    const FString& ActiveMapName,
    TArray<FTelemetryRunInfo>& OutRuns,
    FString& OutError
)
{
    // Clear any previous results before loading the current saved runs.
    OutRuns.Empty();
    OutError.Empty();

    const FString RunsDirectory = GetTelemetryRunsPath();

    IPlatformFile& PlatformFile =
        FPlatformFileManager::Get().GetPlatformFile();

    // An empty or missing runs folder is valid when no simulations have been saved yet.
    if (!PlatformFile.DirectoryExists(*RunsDirectory))
    {
        return true;
    }

    TArray<FString> MetadataPaths;
    IFileManager::Get().FindFilesRecursive(
        MetadataPaths,
        *RunsDirectory,
        TEXT("run_metadata.json"),
        true,
        false,
        false
    );

    TArray<FString> RunFolderPaths;
    for (const FString& MetadataPath : MetadataPaths)
    {
        const FString FolderPath = FPaths::GetPath(MetadataPath);
        if (FPaths::GetCleanFilename(FolderPath).StartsWith(TEXT("run_")))
        {
            RunFolderPaths.Add(FolderPath);
        }
    }

    // Newer run IDs sort after older run IDs, so reverse sorting shows newest first.
    RunFolderPaths.Sort(
        [](const FString& Left, const FString& Right)
        {
            return FPaths::GetCleanFilename(Left) > FPaths::GetCleanFilename(Right);
        }
    );

    // Load the metadata and summary files for every saved run.
    for (const FString& RunFolderPath : RunFolderPaths)
    {
        const FString RunFolderName = FPaths::GetCleanFilename(RunFolderPath);

        const FString MetadataPath =
            FPaths::Combine(RunFolderPath, TEXT("run_metadata.json"));

        const FString SummaryPath =
            FPaths::Combine(RunFolderPath, TEXT("telemetry_summary.json"));

        FString MetadataText;
        FString SummaryText;

        TSharedPtr<FJsonObject> MetadataObject;
        TSharedPtr<FJsonObject> SummaryObject;

        // Load run_metadata.json when it exists.
        if (FFileHelper::LoadFileToString(MetadataText, *MetadataPath))
        {
            const TSharedRef<TJsonReader<>> MetadataReader =
                TJsonReaderFactory<>::Create(MetadataText);

            FJsonSerializer::Deserialize(
                MetadataReader,
                MetadataObject
            );
        }

        // Load telemetry_summary.json when it exists.
        if (FFileHelper::LoadFileToString(SummaryText, *SummaryPath))
        {
            const TSharedRef<TJsonReader<>> SummaryReader =
                TJsonReaderFactory<>::Create(SummaryText);

            FJsonSerializer::Deserialize(
                SummaryReader,
                SummaryObject
            );
        }

        FTelemetryRunInfo RunInfo;

        // Use the folder name as a safe fallback if metadata is missing.
        RunInfo.RunId = RunFolderName;
        RunInfo.Status = TEXT("unknown");

        FString RawCreatedAt;

        if (MetadataObject.IsValid())
        {
            MetadataObject->TryGetStringField(
                TEXT("run_id"),
                RunInfo.RunId
            );

            MetadataObject->TryGetStringField(
                TEXT("status"),
                RunInfo.Status
            );

            MetadataObject->TryGetStringField(
                TEXT("created_at"),
                RawCreatedAt
            );

            MetadataObject->TryGetStringField(TEXT("map_name"), RunInfo.MapName);
            MetadataObject->TryGetStringField(TEXT("map_id"), RunInfo.MapId);
        }

        // Runs created before map-aware telemetry are intentionally hidden from
        // a specific roadmap rather than being incorrectly attributed to it.
        if (!ActiveMapName.IsEmpty() && !RunInfo.MapName.Equals(ActiveMapName, ESearchCase::IgnoreCase))
        {
            continue;
        }

        // Older metadata files may not contain created_at, so use the run ID.
        if (RawCreatedAt.IsEmpty() && RunFolderName.StartsWith(TEXT("run_")))
        {
            RawCreatedAt = RunFolderName.RightChop(4);
        }

        // Keep the original timestamp if it cannot be converted.
        RunInfo.CreatedAt = RawCreatedAt;

        // Expected timestamp format:
        // 2026-07-10_18-09-13
        if (RawCreatedAt.Len() >= 19)
        {
            const int32 Year =
                FCString::Atoi(*RawCreatedAt.Mid(0, 4));

            const int32 Month =
                FCString::Atoi(*RawCreatedAt.Mid(5, 2));

            const int32 Day =
                FCString::Atoi(*RawCreatedAt.Mid(8, 2));

            const int32 Hour =
                FCString::Atoi(*RawCreatedAt.Mid(11, 2));

            const int32 Minute =
                FCString::Atoi(*RawCreatedAt.Mid(14, 2));

            const int32 Second =
                FCString::Atoi(*RawCreatedAt.Mid(17, 2));

            if (FDateTime::Validate(
                    Year,
                    Month,
                    Day,
                    Hour,
                    Minute,
                    Second,
                    0
                ))
            {
                const FDateTime ParsedDateTime(
                    Year,
                    Month,
                    Day,
                    Hour,
                    Minute,
                    Second
                );

                RunInfo.CreatedAt =
                    ParsedDateTime.ToFormattedString(
                        TEXT("%B %d, %Y - %I:%M %p")
                    );
            }
        }

        // Read summary values when telemetry_summary.json was loaded successfully.
        if (SummaryObject.IsValid())
        {
            double TotalVehiclesValue = 0.0;
            double AverageSpeedValue = 0.0;

            SummaryObject->TryGetNumberField(
                TEXT("total_vehicles"),
                TotalVehiclesValue
            );

            SummaryObject->TryGetNumberField(
                TEXT("average_speed_mph"),
                AverageSpeedValue
            );

            RunInfo.TotalVehicles =
                static_cast<int32>(TotalVehiclesValue);

            RunInfo.AverageSpeedMph =
                static_cast<float>(AverageSpeedValue);
        }

        OutRuns.Add(RunInfo);
    }

    return true;
}


// Loads one saved run directly from its JSON files without launching Python.
bool UTelemetryPanelBridge::GetSavedRunDetails(
    const FString& RunId,
    FTelemetryRunDetails& OutDetails,
    FString& OutError
)
{
    // Reset the outputs before loading the selected run.
    OutDetails = FTelemetryRunDetails();
    OutError.Empty();

    // Prevent invalid folder names from being used as run IDs.
    if (RunId.IsEmpty() ||
        RunId.Contains(TEXT("..")) ||
        RunId.Contains(TEXT("/")) ||
        RunId.Contains(TEXT("\\")))
    {
        OutError = TEXT("The selected run ID is invalid.");
        return false;
    }

    const FString RunFolderPath = FindTelemetryRunFolder(GetTelemetryRunsPath(), RunId);

    const FString MetadataPath = FPaths::Combine(
        RunFolderPath,
        TEXT("run_metadata.json")
    );

    const FString SummaryPath = FPaths::Combine(
        RunFolderPath,
        TEXT("telemetry_summary.json")
    );

    IPlatformFile& PlatformFile =
        FPlatformFileManager::Get().GetPlatformFile();

    // Make sure the selected run folder still exists.
    if (!PlatformFile.DirectoryExists(*RunFolderPath))
    {
        OutError = FString::Printf(
            TEXT("Saved run folder was not found: %s"),
            *RunFolderPath
        );

        return false;
    }

    FString MetadataText;
    FString SummaryText;

    // The metadata file contains the processing status and run information.
    if (!FFileHelper::LoadFileToString(MetadataText, *MetadataPath))
    {
        OutError = FString::Printf(
            TEXT("Could not read run_metadata.json for %s."),
            *RunId
        );

        return false;
    }

    // The summary file contains the main telemetry statistics.
    if (!FFileHelper::LoadFileToString(SummaryText, *SummaryPath))
    {
        OutError = FString::Printf(
            TEXT("Could not read telemetry_summary.json for %s."),
            *RunId
        );

        return false;
    }

    TSharedPtr<FJsonObject> MetadataObject;
    TSharedPtr<FJsonObject> SummaryObject;

    const TSharedRef<TJsonReader<>> MetadataReader =
        TJsonReaderFactory<>::Create(MetadataText);

    // Parse the saved run metadata JSON.
    if (!FJsonSerializer::Deserialize(
            MetadataReader,
            MetadataObject
        ) ||
        !MetadataObject.IsValid())
    {
        OutError = TEXT("Failed to parse run_metadata.json.");
        return false;
    }

    const TSharedRef<TJsonReader<>> SummaryReader =
        TJsonReaderFactory<>::Create(SummaryText);

    // Parse the saved telemetry summary JSON.
    if (!FJsonSerializer::Deserialize(
            SummaryReader,
            SummaryObject
        ) ||
        !SummaryObject.IsValid())
    {
        OutError = TEXT("Failed to parse telemetry_summary.json.");
        return false;
    }

    // Use the selected folder name as a safe default run ID.
    OutDetails.RunId = RunId;
    OutDetails.Status = TEXT("unknown");

    MetadataObject->TryGetStringField(
        TEXT("run_id"),
        OutDetails.RunId
    );

    MetadataObject->TryGetStringField(
        TEXT("status"),
        OutDetails.Status
    );

    double TotalVehiclesValue = 0.0;
    double SimulationDurationValue = 0.0;
    double EdgesUsedValue = 0.0;
    double AverageSpeedValue = 0.0;
    double TotalWaitValue = 0.0;
    double MaximumWaitValue = 0.0;

    // Read the main numeric values from telemetry_summary.json.
    SummaryObject->TryGetNumberField(
        TEXT("total_vehicles"),
        TotalVehiclesValue
    );

    SummaryObject->TryGetNumberField(
        TEXT("simulation_duration_s"),
        SimulationDurationValue
    );

    SummaryObject->TryGetNumberField(
        TEXT("edges_used"),
        EdgesUsedValue
    );

    SummaryObject->TryGetNumberField(
        TEXT("average_speed_mph"),
        AverageSpeedValue
    );

    SummaryObject->TryGetNumberField(
        TEXT("total_wait_added_s"),
        TotalWaitValue
    );

    SummaryObject->TryGetNumberField(
        TEXT("max_wait_time_s"),
        MaximumWaitValue
    );

    // Convert JSON numbers into the types used by the Blueprint struct.
    OutDetails.TotalVehicles =
        static_cast<int32>(TotalVehiclesValue);

    OutDetails.SimulationDurationSeconds =
        static_cast<float>(SimulationDurationValue);

    OutDetails.EdgesUsed =
        static_cast<int32>(EdgesUsedValue);

    OutDetails.AverageSpeedMph =
        static_cast<float>(AverageSpeedValue);

    OutDetails.TotalWaitAddedSeconds =
        static_cast<float>(TotalWaitValue);

    OutDetails.MaximumWaitSeconds =
        static_cast<float>(MaximumWaitValue);

    // Read the nested worst-bottleneck values when they are available.
    const TSharedPtr<FJsonObject>* BottleneckObject = nullptr;

    if (SummaryObject->TryGetObjectField(
            TEXT("worst_bottleneck"),
            BottleneckObject
        ) &&
        BottleneckObject != nullptr &&
        BottleneckObject->IsValid())
    {
        double BottleneckScoreValue = 0.0;

        (*BottleneckObject)->TryGetStringField(
            TEXT("road_label"),
            OutDetails.WorstBottleneckRoad
        );

        (*BottleneckObject)->TryGetNumberField(
            TEXT("bottleneck_score"),
            BottleneckScoreValue
        );

        OutDetails.WorstBottleneckScore =
            static_cast<float>(BottleneckScoreValue);
    }

    return true;
}


// Gets a generated heatmap path directly for use by the Unreal UI.
bool UTelemetryPanelBridge::GetGeneratedHeatmapPath(
    const FString& RunId,
    const FString& Metric,
    FString& OutHeatmapPath,
    FString& OutError
)
{
    // Reset the outputs before checking the selected run and metric.
    OutHeatmapPath.Empty();
    OutError.Empty();

    // Prevent invalid folder paths from being created from the run ID.
    if (RunId.IsEmpty() ||
        RunId.Contains(TEXT("..")) ||
        RunId.Contains(TEXT("/")) ||
        RunId.Contains(TEXT("\\")))
    {
        OutError = TEXT("The selected run ID is invalid.");
        return false;
    }

    // Prevent invalid file paths from being created from the metric.
    if (Metric.IsEmpty() ||
        Metric.Contains(TEXT("..")) ||
        Metric.Contains(TEXT("/")) ||
        Metric.Contains(TEXT("\\")))
    {
        OutError = TEXT("The selected heatmap metric is invalid.");
        return false;
    }

    const FString RunFolderPath = FindTelemetryRunFolder(GetTelemetryRunsPath(), RunId);

    const FString HeatmapPath = FPaths::Combine(
        RunFolderPath,
        TEXT("heatmaps"),
        FString::Printf(
            TEXT("heatmap_%s.png"),
            *Metric
        )
    );

    IPlatformFile& PlatformFile =
        FPlatformFileManager::Get().GetPlatformFile();

    // Make sure the selected saved run still exists.
    if (!PlatformFile.DirectoryExists(*RunFolderPath))
    {
        OutError = FString::Printf(
            TEXT("Saved run folder was not found: %s"),
            *RunId
        );

        return false;
    }

    // The user must generate this metric before it can be viewed.
    if (!PlatformFile.FileExists(*HeatmapPath))
    {
        OutError = TEXT(
            "This heatmap has not been generated yet. "
            "Select the metric and click Generate Heatmap first."
        );

        return false;
    }

    // Return the complete PNG path so Blueprint can load it as a texture.
    OutHeatmapPath = HeatmapPath;

    return true;
}

// Loads the small JSON sidecar that describes the native SVG viewer decorations.
bool UTelemetryPanelBridge::GetHeatmapDisplayInfo(
    const FString& HeatmapPath,
    FTelemetryHeatmapDisplayInfo& OutInfo,
    FString& OutError
)
{
    OutInfo = FTelemetryHeatmapDisplayInfo();
    OutError.Empty();

    const FString SidecarPath = FPaths::ChangeExtension(HeatmapPath, TEXT("json"));
    FString JsonText;
    if (!FFileHelper::LoadFileToString(JsonText, *SidecarPath))
    {
        OutError = TEXT("Heatmap display information was not found. Regenerate this heatmap first.");
        return false;
    }

    TSharedPtr<FJsonObject> RootObject;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
    if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
    {
        OutError = TEXT("Heatmap display information could not be read.");
        return false;
    }

    RootObject->TryGetStringField(TEXT("title"), OutInfo.Title);
    RootObject->TryGetStringField(TEXT("legend_label"), OutInfo.LegendLabel);

    const TArray<TSharedPtr<FJsonValue>>* TickValues = nullptr;
    if (RootObject->TryGetArrayField(TEXT("legend_ticks_top_to_bottom"), TickValues))
    {
        for (const TSharedPtr<FJsonValue>& TickValue : *TickValues)
        {
            FString Tick;
            if (TickValue.IsValid() && TickValue->TryGetString(Tick))
            {
                OutInfo.LegendTicksTopToBottom.Add(Tick);
            }
        }
    }

    const TArray<TSharedPtr<FJsonValue>>* ColorValues = nullptr;
    if (RootObject->TryGetArrayField(TEXT("legend_colors_top_to_bottom"), ColorValues))
    {
        for (const TSharedPtr<FJsonValue>& ColorValue : *ColorValues)
        {
            FString Color;
            if (ColorValue.IsValid() && ColorValue->TryGetString(Color))
            {
                OutInfo.LegendColorsTopToBottom.Add(Color);
            }
        }
    }

    const TArray<TSharedPtr<FJsonValue>>* RowValues = nullptr;
    if (RootObject->TryGetArrayField(TEXT("summary_rows"), RowValues))
    {
        for (const TSharedPtr<FJsonValue>& RowValue : *RowValues)
        {
            const TSharedPtr<FJsonObject> RowObject = RowValue.IsValid() ? RowValue->AsObject() : nullptr;
            if (!RowObject.IsValid())
            {
                continue;
            }

            FTelemetryHeatmapSummaryRow Row;
            RowObject->TryGetStringField(TEXT("label"), Row.Label);
            RowObject->TryGetStringField(TEXT("value"), Row.Value);
            OutInfo.SummaryRows.Add(Row);
        }
    }

    if (OutInfo.SummaryRows.IsEmpty() || OutInfo.LegendTicksTopToBottom.IsEmpty())
    {
        OutError = TEXT("Heatmap display information is incomplete. Regenerate this heatmap first.");
        return false;
    }

    return true;
}

// Loads a heatmap PNG file into a texture that an Unreal Image widget can display.
UTexture2D* UTelemetryPanelBridge::LoadHeatmapTexture(
    const FString& HeatmapPath,
    FString& OutError
)
{
    // Clear any previous error before attempting to load the image.
    OutError.Empty();

    if (HeatmapPath.IsEmpty())
    {
        OutError = TEXT("The heatmap file path is empty.");
        return nullptr;
    }

    IPlatformFile& PlatformFile =
        FPlatformFileManager::Get().GetPlatformFile();

    // Make sure the PNG still exists before Unreal tries to load it.
    if (!PlatformFile.FileExists(*HeatmapPath))
    {
        OutError = FString::Printf(
            TEXT("Heatmap image was not found: %s"),
            *HeatmapPath
        );

        return nullptr;
    }

    // Import the PNG file into a transient Unreal texture at runtime.
    UTexture2D* LoadedTexture =
        FImageUtils::ImportFileAsTexture2D(HeatmapPath);

    if (LoadedTexture == nullptr)
    {
        OutError = TEXT("Unreal could not load the heatmap PNG as a texture.");
        return nullptr;
    }

    // Treat runtime heatmaps as UI assets so Unreal keeps their full detail and color.
    LoadedTexture->LODGroup = TEXTUREGROUP_UI;
    LoadedTexture->NeverStream = true;
    LoadedTexture->Filter = TF_Bilinear;
    LoadedTexture->SRGB = true;
    LoadedTexture->UpdateResource();

    return LoadedTexture;
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
