using System.IO;
using UnrealBuildTool;

public class RoadMapSimLibrary : ModuleRules
{
    public RoadMapSimLibrary(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.External;

        // 1. Resolve the path back to the roadmap root
        // ModuleDirectory is frontend/Plugins/RoadMapSim/Source/ThirdParty/RoadMapSimLibrary/
        // Stepping back 6 directories puts us at the roadmap/ root.
        string RootDirectory = Path.GetFullPath(Path.Combine(ModuleDirectory, "../../../../../.."));
        string InstallDirectory = Path.Combine(RootDirectory, "sim", "INSTALL");

        // 2. Include the headers
        string IncludePath = Path.Combine(InstallDirectory, "include/include");
        
        // Use PublicSystemIncludePaths instead of PublicIncludePaths
        PublicSystemIncludePaths.Add(IncludePath);

        // 3. Link the libraries
        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            string LibDirectory = Path.Combine(InstallDirectory, "lib");

            // Check if the directory exists to avoid UBT crashes if CMake hasn't run yet
            if (Directory.Exists(LibDirectory))
            {
                // Automatically find and link every .lib file in the directory
                string[] LibFiles = Directory.GetFiles(LibDirectory, "*.lib");
                foreach (string Lib in LibFiles)
                {
                    PublicAdditionalLibraries.Add(Lib);
                }
            }
        }
    }
}