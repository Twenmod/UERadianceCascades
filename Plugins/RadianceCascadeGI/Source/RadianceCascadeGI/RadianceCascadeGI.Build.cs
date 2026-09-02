// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class RadianceCascadeGI : ModuleRules
{
	public RadianceCascadeGI(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"RenderCore",
			"Renderer",
			"RHI",
			"Projects",
		});

        var EngineDir = Path.GetFullPath(Target.RelativeEnginePath);

        PrivateIncludePaths.AddRange(
			new string[] {
				"RadianceCascadeGI/Private",
                Path.Combine(EngineDir, "Source/Runtime/Renderer/Private"),
				Path.Combine(EngineDir, "Source/Runtime/Renderer/Internal")
            }

            );
	}
}
