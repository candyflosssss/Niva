using UnrealBuildTool;

public class NetworkCorePluginEditor : ModuleRules
{
    public NetworkCorePluginEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "Slate",
            "SlateCore"
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "UnrealEd",
            "InputCore",
            "Projects",
            "ApplicationCore",
            "EditorFramework",
            "PropertyEditor",
            "NetworkCorePlugin"
        });
    }
}
