// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class TaskWeaver : ModuleRules
{
		public TaskWeaver(ReadOnlyTargetRules Target) : base(Target)
		{
				PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

				PublicDependencyModuleNames.AddRange(
						new[]
						{
								"Core",
								"CoreUObject",
								"Engine",
								"AIModule",
								"MCPFramework",
								"AssetRegistry"
						}
				);

				PrivateDependencyModuleNames.AddRange(
						new[]
						{
								"Slate",
								"SlateCore",
						}
				);
		}
}
