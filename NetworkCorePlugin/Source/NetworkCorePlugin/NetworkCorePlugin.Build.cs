// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class NetworkCorePlugin : ModuleRules
{
        public NetworkCorePlugin(ReadOnlyTargetRules Target) : base(Target)
        {
                string CivetWebPath = Path.Combine(ModuleDirectory, "ThirdParty", "CivetWeb");

                PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

                PublicIncludePaths.Add(Path.Combine(CivetWebPath, "include"));
                PrivateIncludePaths.Add(Path.Combine(CivetWebPath, "include"));
                PublicIncludePaths.Add(Path.Combine(CivetWebPath, "src"));
                PrivateIncludePaths.Add(Path.Combine(CivetWebPath, "src"));

                PublicDefinitions.Add("NO_SSL");

                PublicDependencyModuleNames.AddRange(
                        new[]
                        {
                                "Core",
                                "HTTPServer",
                                "HTTP",
                        }
                );

                PrivateDependencyModuleNames.AddRange(
                        new[]
                        {
                                "CoreUObject",
                                "Engine",
                                "Slate",
                                "SlateCore",
                                "Networking",
                                "Sockets",
                                "DeveloperSettings",
                                "Json",
                                "JsonUtilities",
                                "AudioMixer",
                                "WebSockets",
                                "CoreManager",
                                "OnlineSubsystem",
                                "OnlineSubsystemUtils",
                                "OnlineSubsystemEOS"
                        }
                );
        }
}