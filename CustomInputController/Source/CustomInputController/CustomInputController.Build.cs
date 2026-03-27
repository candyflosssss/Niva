// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class CustomInputController : ModuleRules
{
		public CustomInputController(ReadOnlyTargetRules Target) : base(Target)
		{
				PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

				PublicDependencyModuleNames.AddRange(
						new[]
						{
								"Core",
								"CoreUObject",
								"Engine",
								"InputCore",
								"InputDevice",
								"ApplicationCore",
								"Networking",
								"Sockets",
								"NetworkCorePlugin",
								"AudioCaptureCore",
								"AudioCapture",
								"WebSockets",
								"UMG",
								"libOpus",
								"HTTPServer",
						}
				);

				PrivateDependencyModuleNames.AddRange(
						new[]
						{
								"Slate",
								"SlateCore",
								"DeveloperSettings",
								"Json",
								"JsonUtilities",
								"Projects",
								"Settings",
								"OpenColorIOLib",
								"HTTP",
								"AudioExtensions",
								"AudioMixer",
								"CoreManager",
						}
				);

				if (Target.bBuildEditor)
				{
						PrivateDependencyModuleNames.AddRange(
								new[]
								{
										"UnrealEd",
										"EditorStyle",
										"LevelEditor"
								}
						);
				}

				PublicDefinitions.Add("CUSTOMINPUT_USE_OPUS=1");
		}
}