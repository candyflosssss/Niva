#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "NodeSourceSettings.generated.h"

USTRUCT()
struct FNodeSourceColorDef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Plugin Colors")
	FString PluginName;

	UPROPERTY(EditAnywhere, Category = "Plugin Colors")
	FLinearColor Color;

	FNodeSourceColorDef()
		: PluginName(TEXT(""))
		, Color(FLinearColor::White)
	{
	}

	// 重载 == 操作符，确保重置箭头（Reset Arrow）能正确判断是否为默认值
	bool operator==(const FNodeSourceColorDef& Other) const
	{
		return PluginName == Other.PluginName && Color == Other.Color;
	}
};

/**
 * Configures visibility, styling, and custom colors for plugin badges on graph nodes.
 */

UCLASS(Config = EditorPerProjectUserSettings, meta = (DisplayName = "Node Source"))
class NODESOURCE_API UNodeSourceSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UNodeSourceSettings()
	{
		// 初始化默认值
		bShowEnginePlugins = false;
		bShowMarketplacePlugins = true;
		bShowProjectPlugins = true;
		bShowFunctionLibrary = true;
		bShowOnSelectionOnly = false;
		DefaultOpacity = 0.45f;


		// 1. KismetMathLibrary
		FNodeSourceColorDef MathColor;
		MathColor.PluginName = TEXT("KismetMathLibrary");
		MathColor.Color = FLinearColor(0.0f, 0.8f, 0.1f); 
		CustomColors.Add(MathColor);

		// 2. Actor 
		FNodeSourceColorDef ActorColor;
		ActorColor.PluginName = TEXT("Actor");
		ActorColor.Color = FLinearColor(0.0f, 0.4f, 1.0f); 
		CustomColors.Add(ActorColor);

		// 3. KismetSystemLibrary
		FNodeSourceColorDef SysColor;
		SysColor.PluginName = TEXT("KismetSystemLibrary");
		SysColor.Color = FLinearColor(0.6f, 0.1f, 0.8f);
		CustomColors.Add(SysColor);
	}

	// --- 插件可见性设置 ---

	/**
	 * If true, displays badges for nodes that belong to built-in Engine plugins.
	 * These are typically utility plugins provided by Epic Games.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Visibility", meta = (DisplayName = "Show Engine Plugins"))
	bool bShowEnginePlugins;

	/**
	 * If true, displays badges for nodes that belong to plugins installed from the Unreal Marketplace.
	 * Useful for identifying third-party assets in your graph.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Visibility", meta = (DisplayName = "Show Marketplace Plugins"))
	bool bShowMarketplacePlugins;

	/**
	 * If true, displays badges for nodes that belong to plugins located in your project's local Plugins folder.
	 * This helps distinguish your own project-specific plugin nodes.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Visibility", meta = (DisplayName = "Show Project Plugins"))
	bool bShowProjectPlugins;

	// --- 库设置 ---

	/**
	 * If true, displays a secondary badge showing the name of the Function Library (e.g., "KismetSystemLibrary").
	 * This badge appears next to the plugin name badge.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Library Settings", meta = (DisplayName = "Show Function Library Name"))
	bool bShowFunctionLibrary;

	// --- 行为设置 ---

	/**
	 * If true, badges will only appear when a node is selected.
	 * Keep this disabled to see badges at all times.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Behavior", meta = (DisplayName = "Show Only When Selected"))
	bool bShowOnSelectionOnly;

	// --- 样式设置 ---

	/**
	 * Controls the background opacity of the badges.
	 * A value of 1.0 is fully opaque, while lower values make the badges more transparent.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Style", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DefaultOpacity = 0.45f;

	// --- 过滤与颜色 ---

	/**
	 * A list of plugin or library names to exclude.
	 * Nodes matching these names will not display any badges. Case insensitive.
	 * Note: Changes require a graph refresh or restart to take effect on existing nodes.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Filtering")
	TArray<FString> ExcludedNames;

	// --- Color Settings ---

	/**
	 * Define specific colors for specific plugins.
	 * If a plugin is not listed here, a color will be automatically generated based on its name.
	 * The name of the Plugin OR the Library/Class (e.g., "Paper2D" or "Actor"). 
	 * Note: Changes require a graph refresh or restart to take effect on existing nodes.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Colors")
	TArray<FNodeSourceColorDef> CustomColors;


	virtual FName GetContainerName() const override { return TEXT("Editor"); }
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
	virtual FName GetSectionName() const override { return TEXT("NodeSource"); }
};
