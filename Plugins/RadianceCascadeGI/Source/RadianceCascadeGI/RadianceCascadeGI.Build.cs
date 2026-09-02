// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class RadianceCascadeGI : ModuleRules
{
	public RadianceCascadeGI(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicIncludePaths.AddRange(
			new string[] {
				// ... add public include paths required here ...
			}
			);
				
		
		PrivateIncludePaths.AddRange(
			new string[] {
				"RadianceCascadeGI/Private"
            }

            );
            if(Target.bBuildEditor == true)
            {
                PrivateDependencyModuleNames.Add("TargetPlatform");
            }

        PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"Engine",
				"MaterialShaderQualitySettings"
				// ... add other public dependencies that you statically link with here ...
			}
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				"Renderer",
				"RenderCore",
				"RHI",
				"Projects"
				// ... add private dependencies that you statically link with here ...	
			}
			);
			if(Target.bBuildEditor == true)
			{

				PrivateDependencyModuleNames.AddRange(
					new string[] {
						"UnrealEd",
						"MaterialUtilities",
						"SlateCore",
						"Slate"
					}
				);

				CircularlyReferencedDependentModules.AddRange(
					new string[] {
						"UnrealEd",
						"MaterialUtilities",
					}
				);
			}

        DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);
	}
}
