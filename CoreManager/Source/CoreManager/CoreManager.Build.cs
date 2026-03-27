// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class CoreManager : ModuleRules
{
		public CoreManager(ReadOnlyTargetRules Target) : base(Target)
		{
				PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

				PublicDependencyModuleNames.AddRange(
						new[]
						{
								"Core",
								"CoreUObject",
								"Engine",
								"UMG",
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
