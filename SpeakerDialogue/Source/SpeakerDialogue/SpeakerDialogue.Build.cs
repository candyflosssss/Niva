// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class SpeakerDialogue : ModuleRules
{
		public SpeakerDialogue(ReadOnlyTargetRules Target) : base(Target)
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
