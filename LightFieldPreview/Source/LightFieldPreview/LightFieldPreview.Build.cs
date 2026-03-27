// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class LightFieldPreview : ModuleRules
{
		public LightFieldPreview(ReadOnlyTargetRules Target) : base(Target)
		{
				PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

				PublicDependencyModuleNames.AddRange(
						new[]
						{
								"Core",
						}
				);

				PrivateDependencyModuleNames.AddRange(
						new[]
						{
								"CoreUObject",
								"Engine",
								"Slate",
								"SlateCore",
						}
				);
		}
}
