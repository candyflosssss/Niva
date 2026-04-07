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
                                                                "OpenColorIOLib",
                                                                "HTTP",
                                                                "AudioExtensions",
                                                                "AudioMixer"
                                                }
                                );
                                
                                string PluginsDir = System.IO.Path.GetFullPath(System.IO.Path.Combine(ModuleDirectory, "../../.."));
                                bool bHasCoreManager = System.IO.Directory.Exists(System.IO.Path.Combine(PluginsDir, "CoreManager"));
                                
                                if (bHasCoreManager)
                                {
                                                PrivateDependencyModuleNames.Add("CoreManager");
                                                PublicDefinitions.Add("HAS_CORE_MANAGER=1");
                                }
                                else
                                {
                                                PublicDefinitions.Add("HAS_CORE_MANAGER=0");
                                }

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