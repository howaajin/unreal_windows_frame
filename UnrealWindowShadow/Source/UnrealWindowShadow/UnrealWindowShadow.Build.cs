// Copyright 2023 Howaajin All rights reserved.

using UnrealBuildTool;

public class UnrealWindowShadow : ModuleRules
{
	public UnrealWindowShadow(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
			}
			);

        PrivateIncludePathModuleNames.AddRange(
            new string[]
			{
				"MainFrame",
			}
            );


        PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Projects",
				"InputCore",
				"EditorFramework",
				"UnrealEd",
				"ToolMenus",
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				"ApplicationCore",
			}
			);
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				"MainFrame",
			}
			);
	}
}
