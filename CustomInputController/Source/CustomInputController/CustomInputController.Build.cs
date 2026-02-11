// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class CustomInputController : ModuleRules
{
	public CustomInputController(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicIncludePaths.AddRange(
			new string[] {
				// ... add public include paths required here ...
			}
			);
				
		
		PrivateIncludePaths.AddRange(
			new string[] {
				// ... add other private include paths required here ...
			}
			);
			
		
		PublicDependencyModuleNames.AddRange(
			new string[]
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
				"CoreManager",
				"libOpus",
				"HTTPServer",
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
			    "DeveloperSettings",
			    "InputCore",
			    "Json",
			    "JsonUtilities",
			    "Projects",
			    "Settings",
			    "OpenColorIOLib", 
			    "WebSockets",
			    "HTTP",
			    "AudioExtensions",
			    "AudioMixer",
			    "DeveloperSettings",
			    "CoreManager",
    // ... add private dependencies that you statically link with here ... 
}
);

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"UnrealEd",
					"EditorStyle", // keep for compatibility if referenced indirectly
					"LevelEditor"
				}
			);
		}

		// Enable Opus support using Engine's built-in libopus module
		PublicDefinitions.Add("CUSTOMINPUT_USE_OPUS=1");

		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);
	}
}