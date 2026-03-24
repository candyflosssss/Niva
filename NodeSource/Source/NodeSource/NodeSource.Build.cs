using UnrealBuildTool;

public class NodeSource : ModuleRules
{
    public NodeSource(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        // [UE 5.1+] 强制使用最新的头文件包含规则
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;

        // 1. 公共依赖 (最基础的模块)
        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
            }
        );

        // 2. 私有依赖 (按功能分类)
        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                // --- 核心引擎与对象系统 ---
                "CoreUObject",
                "Engine",
                "InputCore",         // 用于 FInputChord (快捷键绑定)

                // --- 插件管理与配置设置 ---
                "Projects",          // 用于 IPluginManager (获取已加载插件的信息)
                "DeveloperSettings", // 用于 UDeveloperSettings (自动注册到项目设置)
                "Settings",          // 用于 ISettingsModule (手动打开设置面板)

                // --- Slate UI 界面系统 ---
                "Slate",             // Slate 核心框架
                "SlateCore",         // Slate 核心类型

                // --- 编辑器框架 ---
                "UnrealEd",          // 编辑器核心功能
                "ToolMenus",         // 用于扩展工具栏和菜单 (UE4.24+ 新标准)

                // --- 蓝图与图表编辑器特定功能 ---
                "BlueprintGraph",    // 蓝图节点定义 (K2Node, UK2Node_CallFunction 等)
                "GraphEditor",       // 图表编辑器 UI (FGraphPanelNodeFactory, SGraphNode)
                "Kismet",            // 蓝图编辑器模块 (FBlueprintEditorModule)
            }
        );
    }
}
