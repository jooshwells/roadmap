// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class SD1test : ModuleRules
{
	public SD1test(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore" });

        //PrivateDependencyModuleNames.AddRange(new string[] {  });

        //      PublicIncludePaths.AddRange(
        //          new string[] {
        //              System.IO.Path.Combine(ModuleDirectory, "../../../sim"),
        //              System.IO.Path.Combine(ModuleDirectory, "../../../sim/road_network/include"),
        //              System.IO.Path.Combine(ModuleDirectory, "../../../sim/road_network/src"),
        //              System.IO.Path.Combine(ModuleDirectory, "../../../sim/common/road_state/include"), 
        //              System.IO.Path.Combine(ModuleDirectory, "../../../sim/common/road_state/src"),
        //              System.IO.Path.Combine(ModuleDirectory, "../../../sim/external/json/include"),
        //              System.IO.Path.Combine(ModuleDirectory, "../../../sim/physics/include"),
        //              System.IO.Path.Combine(ModuleDirectory, "../../../sim/physics/src")

        //          }
        //      );


        // Get the absolute path to the top-level 'sim' folder
        string SimRootPath = Path.GetFullPath(Path.Combine(ModuleDirectory, "../../../sim"));

        if (Directory.Exists(SimRootPath))
        {
            // Automatically harvest EVERY subfolder inside 'sim' (physics, include, src, etc.)
            string[] AllSimFolders = Directory.GetDirectories(SimRootPath, "*", SearchOption.AllDirectories);

            foreach (string Folder in AllSimFolders)
            {
                PublicIncludePaths.Add(Folder);
            }

            // Also include the root 'sim' folder itself just in case
            PublicIncludePaths.Add(SimRootPath);
        }


        // Uncomment if you are using Slate UI
        // PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

        // Uncomment if you are using online features
        // PrivateDependencyModuleNames.Add("OnlineSubsystem");

        // To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
    }
}
