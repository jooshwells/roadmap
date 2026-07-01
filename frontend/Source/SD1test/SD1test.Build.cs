// Copyright Epic Games, Inc. All Rights Reserved.
using UnrealBuildTool;
using System.IO; // REQUIRED: For Path.Combine

public class SD1test : ModuleRules
{
    public SD1test(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "UMG", "Slate", "SlateCore" });
        PrivateDependencyModuleNames.AddRange(new string[] { });

        // 1. ENABLE EXCEPTIONS AND RTTI
        bEnableExceptions = true;
        bUseRTTI = true;

        // 2. DEFINE THE PATH TO YOUR SIMULATOR CODE
        string ThirdPartyPath = Path.GetFullPath(Path.Combine(ModuleDirectory, "../../../sim"));

        // 3. ADD SYSTEM INCLUDE PATHS
        PublicSystemIncludePaths.AddRange(
            new string[] {
                Path.Combine(ThirdPartyPath, "road_network/include"),
                Path.Combine(ThirdPartyPath, "common/road_state/include"),
                Path.Combine(ThirdPartyPath, "common/vehicle_state/include"),
                Path.Combine(ThirdPartyPath, "common/pathfinding_utils/heuristics/include"),
                Path.Combine(ThirdPartyPath, "common/pathfinding_utils/idm_profiles/include"),
                Path.Combine(ThirdPartyPath, "physics/include"),
                Path.Combine(ThirdPartyPath, "spatial_logic/include"),
                Path.Combine(ThirdPartyPath, "diagnostics/include"),
                Path.Combine(ThirdPartyPath, "driver_logic/pathfinding/include")
            }
        );

        // 4. DETERMINE BUILD CONFIGURATION
        bool bIsDebug = (Target.Configuration == UnrealTargetConfiguration.Debug || Target.Configuration == UnrealTargetConfiguration.DebugGame);
        string ConfigFolder = bIsDebug ? "Debug" : "Release";
        string LibDirectory = Path.Combine(ThirdPartyPath, "build", "CentralLibs", ConfigFolder);

        // 5. LINK STATIC LIBRARIES AND DEBUG SYMBOLS (.PDB)
        string[] LibraryNames = {
            "heuristics",
            "road_state",
            "vehicle_state",
            "network",
            "physics",
            "pathfinding",
            "spatial_logic"
        };

        foreach (string Lib in LibraryNames)
        {
            // Link the static library (Always happens)
            PublicAdditionalLibraries.Add(Path.Combine(LibDirectory, Lib + ".lib"));

            // Register PDB files ONLY if we are in a debug configuration AND on Windows
            if (bIsDebug && Target.Platform == UnrealTargetPlatform.Win64)
            {
                RuntimeDependencies.Add(Path.Combine(LibDirectory, Lib + ".pdb"), StagedFileType.DebugNonUFS);
            }
        }
    }
}