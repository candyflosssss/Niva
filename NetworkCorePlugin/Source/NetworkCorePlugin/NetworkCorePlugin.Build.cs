// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class NetworkCorePlugin : ModuleRules
{
	public NetworkCorePlugin(ReadOnlyTargetRules Target) : base(Target)
	{
        // ���������Ŀ¼
        string CivetWebPath = Path.Combine(ModuleDirectory, "ThirdParty", "CivetWeb");

        // ����ͷ�ļ�
        PublicIncludePaths.Add(Path.Combine(CivetWebPath, "include"));
        PrivateIncludePaths.Add(Path.Combine(CivetWebPath, "include"));
        PublicIncludePaths.Add(Path.Combine(CivetWebPath, "src"));
        PrivateIncludePaths.Add(Path.Combine(CivetWebPath, "src"));
        // PublicDefinitions.Add("USE_ZLIB=0");
        PublicDefinitions.Add("NO_SSL");

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
                "HTTPServer",
				"Networking",
				"Sockets",
                "DeveloperSettings",
				"HTTP",
                "Json",
                "JsonUtilities",
				"AudioMixer",
				"WebSockets"
				// ... add private dependencies that you statically link with here ...	
			}
			);

        DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);
	}

}