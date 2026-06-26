// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO; // REQUIRED: For Path.Combine

public class SD1test : ModuleRules
{
	public SD1test(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
	
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore" });
		PrivateDependencyModuleNames.AddRange(new string[] {  });

		// 1. ENABLE EXCEPTIONS AND RTTI
		// Your network_builder uses try/catch blocks which requires exceptions to be enabled.
		// Standard C++ maps and dynamic casts often require RTTI (Run-Time Type Information).
		bEnableExceptions = true;
		bUseRTTI = true;

		// 2. DEFINE THE PATH TO YOUR SIMULATOR CODE
		// Adjust this path depending on where your 'road_network' and 'common' folders are.
		// This example assumes they are inside a "Simulator" folder at the root of your project directory.
		string ThirdPartyPath = Path.GetFullPath(Path.Combine(ModuleDirectory, "../../../sim"));

		// 3. ADD SYSTEM INCLUDE PATHS
		// Using PublicSystemIncludePaths instead of PublicIncludePaths automatically flags
		// the headers as 3rd-party, suppressing strict Unreal compiler warnings.
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

		string LibDirectory = Path.Combine(ThirdPartyPath, "build", "CentralLibs", "Release");

        PublicAdditionalLibraries.Add(Path.Combine(LibDirectory, "heuristics.lib"));
		PublicAdditionalLibraries.Add(Path.Combine(LibDirectory, "road_state.lib"));
		PublicAdditionalLibraries.Add(Path.Combine(LibDirectory, "vehicle_state.lib"));
        PublicAdditionalLibraries.Add(Path.Combine(LibDirectory, "network.lib"));
        PublicAdditionalLibraries.Add(Path.Combine(LibDirectory, "physics.lib"));
        PublicAdditionalLibraries.Add(Path.Combine(LibDirectory, "pathfinding.lib"));
        PublicAdditionalLibraries.Add(Path.Combine(LibDirectory, "spatial_logic.lib"));


    }
}