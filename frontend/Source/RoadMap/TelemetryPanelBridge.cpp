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
    FString FindLastJsonLine(const FString& ProcessOutput)
    {
        FString JsonLine = ProcessOutput.TrimStartAndEnd();
        TArray<FString> OutputLines;
        ProcessOutput.ParseIntoArrayLines(OutputLines, true);

        for (int32 LineIndex = OutputLines.Num() - 1; LineIndex >= 0; --LineIndex)
        {
            const FString Candidate = OutputLines[LineIndex].TrimStartAndEnd();
            if (Candidate.StartsWith(TEXT("{")) && Candidate.EndsWith(TEXT("}")))
            {
                return Candidate;
            }
        }

        return JsonLine;
    }

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

bool UTelemetryPanelBridge::RunTelemetryCommand(const FString& Command, const TArray<FString>& Arguments, FString& OutJson)
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

    if (Command != TEXT("generate-heatmap")
        && Command != TEXT("compare-fdot")
        && Command != TEXT("compare-runs"))
    {
        OutJson = FString::Printf(
            TEXT("{\"success\":false,\"error\":\"Unknown telemetry panel command.\",\"command\":\"%s\"}"),
            *Command
        );
        return false;
    }

    FString Params = FString::Printf(TEXT("--panel-command %s"), *Command);

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

    OutJson = StdOut.TrimStartAndEnd();
    if (ReturnCode != 0)
    {
        // The pipeline normally prints a structured error to stdout. Keep it so
        // the panel can show the real problem instead of a generic failure.
        if (OutJson.IsEmpty())
        {
            OutJson = FString::Printf(
                TEXT("{\"success\":false,\"error\":\"Telemetry command failed.\",\"return_code\":%d,\"stderr\":\"%s\"}"),
                ReturnCode,
                *StdErr.ReplaceCharWithEscapedChar()
            );
        }

        return false;
    }

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
    const FString& Focus,
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

    if (Focus != TEXT("all") && Focus != TEXT("worst_25") &&
        Focus != TEXT("worst_10") && Focus != TEXT("worst_5"))
    {
        OutError = TEXT("The selected road focus is invalid.");
        return false;
    }

    const FString RunFolderPath = FindTelemetryRunFolder(GetTelemetryRunsPath(), RunId);

    const FString FocusSuffix = Focus.IsEmpty() || Focus == TEXT("all")
        ? FString()
        : FString::Printf(TEXT("_%s"), *Focus);
    const FString HeatmapPath = FPaths::Combine(
        RunFolderPath,
        TEXT("heatmaps"),
        FString::Printf(
            TEXT("heatmap_%s%s.png"),
            *Metric,
            *FocusSuffix
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

    const TSharedPtr<FJsonObject>* MapRectObject = nullptr;
    if (RootObject->TryGetObjectField(TEXT("map_rect"), MapRectObject) &&
        MapRectObject && MapRectObject->IsValid())
    {
        double Left = 0.0;
        double Top = 0.0;
        double Width = 1.0;
        double Height = 1.0;
        (*MapRectObject)->TryGetNumberField(TEXT("left"), Left);
        (*MapRectObject)->TryGetNumberField(TEXT("top"), Top);
        (*MapRectObject)->TryGetNumberField(TEXT("width"), Width);
        (*MapRectObject)->TryGetNumberField(TEXT("height"), Height);
        OutInfo.MapRect = FVector4(Left, Top, Width, Height);
    }

    // Road interaction data is optional so heatmaps generated before this feature still open.
    const TArray<TSharedPtr<FJsonValue>>* RoadValues = nullptr;
    if (RootObject->TryGetArrayField(TEXT("roads"), RoadValues))
    {
        for (const TSharedPtr<FJsonValue>& RoadValue : *RoadValues)
        {
            const TSharedPtr<FJsonObject> RoadObject = RoadValue.IsValid()
                ? RoadValue->AsObject()
                : nullptr;
            if (!RoadObject.IsValid())
            {
                continue;
            }

            FTelemetryHeatmapRoad Road;
            double EdgeId = INDEX_NONE;
            double MetricValue = 0.0;
            RoadObject->TryGetNumberField(TEXT("edge_id"), EdgeId);
            RoadObject->TryGetStringField(TEXT("name"), Road.RoadName);
            RoadObject->TryGetStringField(TEXT("route_ref"), Road.RouteRef);
            RoadObject->TryGetStringField(TEXT("highway"), Road.HighwayType);
            RoadObject->TryGetNumberField(TEXT("value"), MetricValue);
            Road.EdgeId = FMath::RoundToInt(EdgeId);
            Road.MetricValue = static_cast<float>(MetricValue);

            const TArray<TSharedPtr<FJsonValue>>* PointValues = nullptr;
            if (RoadObject->TryGetArrayField(TEXT("points"), PointValues))
            {
                for (const TSharedPtr<FJsonValue>& PointValue : *PointValues)
                {
                    const TArray<TSharedPtr<FJsonValue>>* Coordinates = nullptr;
                    if (!PointValue.IsValid() || !PointValue->TryGetArray(Coordinates) ||
                        !Coordinates || Coordinates->Num() < 2)
                    {
                        continue;
                    }

                    double X = 0.0;
                    double Y = 0.0;
                    if ((*Coordinates)[0]->TryGetNumber(X) && (*Coordinates)[1]->TryGetNumber(Y))
                    {
                        Road.Points.Add(FVector2D(X, Y));
                    }
                }
            }

            if (Road.Points.Num() >= 2)
            {
                OutInfo.Roads.Add(MoveTemp(Road));
            }
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

bool UTelemetryPanelBridge::GenerateSelectedHeatmap(
    const FString& RunId,
    const FString& Metric,
    const FString& Focus,
    FString& OutJson
)
{
    return RunTelemetryCommand(
        TEXT("generate-heatmap"),
        {
            FString::Printf(TEXT("--run-id \"%s\""), *RunId),
            FString::Printf(TEXT("--metric \"%s\""), *Metric),
            FString::Printf(TEXT("--focus \"%s\""), *Focus)
        },
        OutJson
    );
}

bool UTelemetryPanelBridge::CompareSavedRuns(
    const FString& BaselineRunId,
    const FString& ComparisonRunId,
    FTelemetryRunComparisonResult& OutResult,
    FString& OutError
)
{
    OutResult = FTelemetryRunComparisonResult();
    OutError.Empty();

    auto IsInvalidRunId = [](const FString& RunId)
    {
        return RunId.IsEmpty() || RunId.Contains(TEXT("..")) ||
            RunId.Contains(TEXT("/")) || RunId.Contains(TEXT("\\"));
    };
    if (IsInvalidRunId(BaselineRunId) || IsInvalidRunId(ComparisonRunId))
    {
        OutError = TEXT("One of the selected run IDs is invalid.");
        return false;
    }

    FString ResultJson;
    if (!RunTelemetryCommand(
            TEXT("compare-runs"),
            {
                TEXT("--baseline-run-id"), BaselineRunId,
                TEXT("--comparison-run-id"), ComparisonRunId
            },
            ResultJson
        ))
    {
        TSharedPtr<FJsonObject> ErrorObject;
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(FindLastJsonLine(ResultJson));
        if (!FJsonSerializer::Deserialize(Reader, ErrorObject) || !ErrorObject.IsValid() ||
            !ErrorObject->TryGetStringField(TEXT("error"), OutError))
        {
            OutError = TEXT("Run comparison failed.");
        }
        return false;
    }

    TSharedPtr<FJsonObject> Root;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(FindLastJsonLine(ResultJson));
    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
    {
        OutError = TEXT("Run comparison returned an unreadable response.");
        return false;
    }

    bool bSuccess = false;
    Root->TryGetBoolField(TEXT("success"), bSuccess);
    if (!bSuccess)
    {
        if (!Root->TryGetStringField(TEXT("error"), OutError))
        {
            OutError = TEXT("Run comparison did not complete.");
        }
        return false;
    }

    const TSharedPtr<FJsonObject>* BaselineObject = nullptr;
    const TSharedPtr<FJsonObject>* ComparisonObject = nullptr;
    if (Root->TryGetObjectField(TEXT("baseline"), BaselineObject) && BaselineObject && BaselineObject->IsValid())
    {
        (*BaselineObject)->TryGetStringField(TEXT("run_id"), OutResult.BaselineRunId);
        (*BaselineObject)->TryGetStringField(TEXT("created_at"), OutResult.BaselineCreatedAt);
    }
    if (Root->TryGetObjectField(TEXT("comparison"), ComparisonObject) && ComparisonObject && ComparisonObject->IsValid())
    {
        (*ComparisonObject)->TryGetStringField(TEXT("run_id"), OutResult.ComparisonRunId);
        (*ComparisonObject)->TryGetStringField(TEXT("created_at"), OutResult.ComparisonCreatedAt);
    }

    Root->TryGetBoolField(TEXT("preliminary"), OutResult.bPreliminary);
    const TSharedPtr<FJsonObject>* CoverageObject = nullptr;
    if (Root->TryGetObjectField(TEXT("road_coverage"), CoverageObject) && CoverageObject && CoverageObject->IsValid())
    {
        double SharedRoads = 0.0;
        double CoveragePercent = 0.0;
        (*CoverageObject)->TryGetNumberField(TEXT("shared_roads"), SharedRoads);
        (*CoverageObject)->TryGetNumberField(TEXT("shared_coverage_percent"), CoveragePercent);
        OutResult.SharedRoads = static_cast<int32>(SharedRoads);
        OutResult.SharedCoveragePercent = static_cast<float>(CoveragePercent);
    }

    const TArray<TSharedPtr<FJsonValue>>* WarningValues = nullptr;
    if (Root->TryGetArrayField(TEXT("warnings"), WarningValues) && WarningValues)
    {
        for (const TSharedPtr<FJsonValue>& Value : *WarningValues)
        {
            FString Warning;
            if (Value.IsValid() && Value->TryGetString(Warning))
            {
                OutResult.Warnings.Add(Warning);
            }
        }
    }

    const TArray<TSharedPtr<FJsonValue>>* MetricValues = nullptr;
    if (Root->TryGetArrayField(TEXT("metrics"), MetricValues) && MetricValues)
    {
        for (const TSharedPtr<FJsonValue>& Value : *MetricValues)
        {
            const TSharedPtr<FJsonObject>* Object = nullptr;
            if (!Value.IsValid() || !Value->TryGetObject(Object) || !Object || !Object->IsValid())
            {
                continue;
            }
            FTelemetryRunComparisonMetric Metric;
            double Baseline = 0.0, Comparison = 0.0, Delta = 0.0, Percent = 0.0;
            (*Object)->TryGetStringField(TEXT("label"), Metric.Label);
            (*Object)->TryGetStringField(TEXT("unit"), Metric.Unit);
            (*Object)->TryGetStringField(TEXT("status"), Metric.Status);
            (*Object)->TryGetStringField(TEXT("explanation"), Metric.Explanation);
            (*Object)->TryGetNumberField(TEXT("baseline"), Baseline);
            (*Object)->TryGetNumberField(TEXT("comparison"), Comparison);
            (*Object)->TryGetNumberField(TEXT("delta"), Delta);
            Metric.bHasPercentChange = (*Object)->TryGetNumberField(TEXT("percent_change"), Percent);
            Metric.BaselineValue = static_cast<float>(Baseline);
            Metric.ComparisonValue = static_cast<float>(Comparison);
            Metric.Delta = static_cast<float>(Delta);
            Metric.PercentChange = static_cast<float>(Percent);
            OutResult.Metrics.Add(Metric);
        }
    }

    const TArray<TSharedPtr<FJsonValue>>* RoadValues = nullptr;
    if (Root->TryGetArrayField(TEXT("top_road_changes"), RoadValues) && RoadValues)
    {
        for (const TSharedPtr<FJsonValue>& Value : *RoadValues)
        {
            const TSharedPtr<FJsonObject>* Object = nullptr;
            if (!Value.IsValid() || !Value->TryGetObject(Object) || !Object || !Object->IsValid())
            {
                continue;
            }
            FTelemetryRunRoadChange Road;
            double EdgeId = 0.0, BaselineSpeed = 0.0, ComparisonSpeed = 0.0;
            double SpeedDelta = 0.0, BaselineWait = 0.0, ComparisonWait = 0.0;
            double WaitDelta = 0.0, BottleneckDelta = 0.0;
            (*Object)->TryGetNumberField(TEXT("edge_id"), EdgeId);
            (*Object)->TryGetStringField(TEXT("road_name"), Road.RoadName);
            (*Object)->TryGetStringField(TEXT("status"), Road.Status);
            (*Object)->TryGetNumberField(TEXT("baseline_speed_mph"), BaselineSpeed);
            (*Object)->TryGetNumberField(TEXT("comparison_speed_mph"), ComparisonSpeed);
            (*Object)->TryGetNumberField(TEXT("speed_delta_mph"), SpeedDelta);
            (*Object)->TryGetNumberField(TEXT("baseline_wait_s"), BaselineWait);
            (*Object)->TryGetNumberField(TEXT("comparison_wait_s"), ComparisonWait);
            (*Object)->TryGetNumberField(TEXT("wait_delta_s"), WaitDelta);
            (*Object)->TryGetNumberField(TEXT("bottleneck_delta"), BottleneckDelta);
            Road.EdgeId = static_cast<int32>(EdgeId);
            Road.BaselineSpeedMph = static_cast<float>(BaselineSpeed);
            Road.ComparisonSpeedMph = static_cast<float>(ComparisonSpeed);
            Road.SpeedDeltaMph = static_cast<float>(SpeedDelta);
            Road.BaselineWaitSeconds = static_cast<float>(BaselineWait);
            Road.ComparisonWaitSeconds = static_cast<float>(ComparisonWait);
            Road.WaitDeltaSeconds = static_cast<float>(WaitDelta);
            Road.BottleneckDelta = static_cast<float>(BottleneckDelta);
            OutResult.TopRoadChanges.Add(Road);
        }
    }

    return true;
}

// Runs the packaged FDOT comparison and converts its JSON response for the native UI.
bool UTelemetryPanelBridge::CompareSelectedRunWithFDOT(
    const FString& RunId,
    FTelemetryFDOTValidationSummary& OutSummary,
    FString& OutError
)
{
    OutSummary = FTelemetryFDOTValidationSummary();
    OutError.Empty();

    if (RunId.IsEmpty() ||
        RunId.Contains(TEXT("..")) ||
        RunId.Contains(TEXT("/")) ||
        RunId.Contains(TEXT("\\")))
    {
        OutError = TEXT("The selected run ID is invalid.");
        return false;
    }

    FString ResultJson;
    if (!RunTelemetryCommand(
            TEXT("compare-fdot"),
            {TEXT("--run-id"), RunId},
            ResultJson
        ))
    {
        TSharedPtr<FJsonObject> ErrorObject;
        const TSharedRef<TJsonReader<>> ErrorReader = TJsonReaderFactory<>::Create(FindLastJsonLine(ResultJson));
        if (!FJsonSerializer::Deserialize(ErrorReader, ErrorObject) ||
            !ErrorObject.IsValid() ||
            !ErrorObject->TryGetStringField(TEXT("error"), OutError))
        {
            OutError = TEXT("FDOT comparison failed. Check that this map has an FDOT edge mapping.");
        }
        return false;
    }

    // Heatmap rendering writes human-readable progress lines before the panel
    // command prints its final JSON response.
    const FString JsonPayload = FindLastJsonLine(ResultJson);

    TSharedPtr<FJsonObject> ResultObject;
    const TSharedRef<TJsonReader<>> ResultReader = TJsonReaderFactory<>::Create(JsonPayload);
    if (!FJsonSerializer::Deserialize(ResultReader, ResultObject) || !ResultObject.IsValid())
    {
        OutError = TEXT("FDOT comparison returned an unreadable response.");
        return false;
    }

    bool bSuccess = false;
    ResultObject->TryGetBoolField(TEXT("success"), bSuccess);
    if (!bSuccess)
    {
        if (!ResultObject->TryGetStringField(TEXT("error"), OutError))
        {
            OutError = TEXT("FDOT comparison did not complete.");
        }
        return false;
    }

    const TSharedPtr<FJsonObject>* SummaryObject = nullptr;
    if (!ResultObject->TryGetObjectField(TEXT("summary"), SummaryObject) ||
        SummaryObject == nullptr || !SummaryObject->IsValid())
    {
        OutError = TEXT("FDOT comparison response did not include a summary.");
        return false;
    }

    double MatchedEdges = 0.0;
    double GoodEdges = 0.0;
    double ReviewEdges = 0.0;
    double PoorEdges = 0.0;
    double GoodPercent = 0.0;
    double MeanGEH = 0.0;
    double DurationSeconds = 0.0;
    double MinimumDurationSeconds = 0.0;
    double TotalRoadDirections = 0.0;
    double UnmatchedRoadDirections = 0.0;
    double CoveragePercent = 0.0;

    (*SummaryObject)->TryGetNumberField(TEXT("matched_edges"), MatchedEdges);
    (*SummaryObject)->TryGetNumberField(TEXT("good_edges"), GoodEdges);
    (*SummaryObject)->TryGetNumberField(TEXT("review_edges"), ReviewEdges);
    (*SummaryObject)->TryGetNumberField(TEXT("poor_edges"), PoorEdges);
    (*SummaryObject)->TryGetNumberField(TEXT("good_percent"), GoodPercent);
    (*SummaryObject)->TryGetNumberField(TEXT("mean_geh"), MeanGEH);
    (*SummaryObject)->TryGetNumberField(TEXT("simulation_duration_s"), DurationSeconds);
    (*SummaryObject)->TryGetNumberField(TEXT("minimum_recommended_duration_s"), MinimumDurationSeconds);
    (*SummaryObject)->TryGetNumberField(TEXT("total_road_directions"), TotalRoadDirections);
    (*SummaryObject)->TryGetNumberField(TEXT("unmatched_road_directions"), UnmatchedRoadDirections);
    (*SummaryObject)->TryGetNumberField(TEXT("coverage_percent"), CoveragePercent);

    OutSummary.MatchedEdges = static_cast<int32>(MatchedEdges);
    OutSummary.GoodEdges = static_cast<int32>(GoodEdges);
    OutSummary.ReviewEdges = static_cast<int32>(ReviewEdges);
    OutSummary.PoorEdges = static_cast<int32>(PoorEdges);
    OutSummary.GoodPercent = static_cast<float>(GoodPercent);
    OutSummary.MeanGEH = static_cast<float>(MeanGEH);
    OutSummary.SimulationDurationSeconds = static_cast<float>(DurationSeconds);
    OutSummary.TotalRoadDirections = static_cast<int32>(TotalRoadDirections);
    OutSummary.UnmatchedRoadDirections = static_cast<int32>(UnmatchedRoadDirections);
    OutSummary.CoveragePercent = static_cast<float>(CoveragePercent);
    OutSummary.bPreliminary = MinimumDurationSeconds > 0.0 && DurationSeconds < MinimumDurationSeconds;

    const TArray<TSharedPtr<FJsonValue>>* WarningValues = nullptr;
    if ((*SummaryObject)->TryGetArrayField(TEXT("validation_warnings"), WarningValues) && WarningValues)
    {
        TArray<FString> Warnings;
        for (const TSharedPtr<FJsonValue>& WarningValue : *WarningValues)
        {
            FString Warning;
            if (WarningValue.IsValid() && WarningValue->TryGetString(Warning) && !Warning.IsEmpty())
            {
                Warnings.Add(Warning);
            }
        }
        OutSummary.Warning = FString::Join(Warnings, TEXT("\n"));
    }

    const TArray<TSharedPtr<FJsonValue>>* DifferenceValues = nullptr;
    if ((*SummaryObject)->TryGetArrayField(TEXT("top_road_differences"), DifferenceValues) && DifferenceValues)
    {
        for (const TSharedPtr<FJsonValue>& DifferenceValue : *DifferenceValues)
        {
            const TSharedPtr<FJsonObject>* DifferenceObject = nullptr;
            if (!DifferenceValue.IsValid() ||
                !DifferenceValue->TryGetObject(DifferenceObject) ||
                DifferenceObject == nullptr || !DifferenceObject->IsValid())
            {
                continue;
            }

            FTelemetryFDOTRoadDifference Difference;
            double SimulationFlow = 0.0;
            double FDOTFlow = 0.0;
            double PercentDifference = 0.0;
            double GEHScore = 0.0;
            (*DifferenceObject)->TryGetStringField(TEXT("road_name"), Difference.RoadName);
            (*DifferenceObject)->TryGetStringField(TEXT("result"), Difference.Result);
            (*DifferenceObject)->TryGetNumberField(TEXT("simulation_flow_veh_per_hr"), SimulationFlow);
            (*DifferenceObject)->TryGetNumberField(TEXT("fdot_flow_veh_per_hr"), FDOTFlow);
            (*DifferenceObject)->TryGetNumberField(TEXT("percent_difference"), PercentDifference);
            (*DifferenceObject)->TryGetNumberField(TEXT("geh_score"), GEHScore);
            Difference.SimulationFlowVehPerHour = static_cast<float>(SimulationFlow);
            Difference.FDOTFlowVehPerHour = static_cast<float>(FDOTFlow);
            Difference.PercentDifference = static_cast<float>(PercentDifference);
            Difference.GEHScore = static_cast<float>(GEHScore);
            OutSummary.TopRoadDifferences.Add(Difference);
        }
    }

    return true;
}
