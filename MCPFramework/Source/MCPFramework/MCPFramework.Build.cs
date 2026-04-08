using System.IO;
using UnrealBuildTool;

public class MCPFramework : ModuleRules
{
    public MCPFramework(ReadOnlyTargetRules Target) : base(Target)
    {
        string CivetWebPath = Path.Combine(ModuleDirectory, "ThirdParty", "CivetWeb");

        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new[]
            {
                "Core",
                "CoreUObject",
                "Engine",
                "HTTP",
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new[]
            {
                "DeveloperSettings",
                "Json",
                "JsonUtilities",
                "HTTPServer",
                "Networking",
                "Sockets",
                "Slate",
                "SlateCore",
            }
        );

        PublicIncludePaths.Add(Path.Combine(CivetWebPath, "include"));
        PrivateIncludePaths.Add(Path.Combine(CivetWebPath, "include"));
        PublicIncludePaths.Add(Path.Combine(CivetWebPath, "src"));
        PrivateIncludePaths.Add(Path.Combine(CivetWebPath, "src"));

        PublicDefinitions.Add("NO_SSL");

        string PluginsDir = Path.GetFullPath(Path.Combine(ModuleDirectory, "../../.."));
        bool bHasCoreManager = Directory.Exists(Path.Combine(PluginsDir, "CoreManager"));

        if (bHasCoreManager)
        {
            PrivateDependencyModuleNames.Add("CoreManager");
            PublicDefinitions.Add("HAS_CORE_MANAGER=1");
        }
        else
        {
            PublicDefinitions.Add("HAS_CORE_MANAGER=0");
        }
    }
}

