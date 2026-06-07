// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class SD1testTarget : TargetRules
{
	public SD1testTarget( TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V6;
		WindowsPlatform.CompilerVersion = "14.44.35207";
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_7;
		ExtraModuleNames.AddRange( new string[] { "SD1test" } );

		PreBuildSteps.Add("cmd.exe /c \"cd /D $(ProjectDir)\\..\\sim && cmake -B build -S . && cmake --build build --config Release && cmake --install build --config Release\"");
	}
}
