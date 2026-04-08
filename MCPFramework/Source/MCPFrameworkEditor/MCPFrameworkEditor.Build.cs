using UnrealBuildTool;

public class MCPFrameworkEditor : ModuleRules
{
    public MCPFrameworkEditor(ReadOnlyTargetRules Target) : base(Target)
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
            "MCPFramework"
        });
    }
}

