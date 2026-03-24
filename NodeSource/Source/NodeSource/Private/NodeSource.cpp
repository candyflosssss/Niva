#include "NodeSource.h"
#include "NodeSourceSettings.h" 

// 核心依赖
#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#include "EdGraphUtilities.h"

// UI 依赖
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h" 
#include "Styling/AppStyle.h"
#include "Brushes/SlateRoundedBoxBrush.h" 

// Graph 依赖
#include "SGraphPanel.h" 

// 节点相关依赖
#include "K2Node.h"
#include "K2Node_CallFunction.h" 
#include "KismetNodes/SGraphNodeK2Base.h"

// 工具栏扩展依赖
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Commands/Commands.h"
#include "BlueprintEditorModule.h"
#include "ISettingsModule.h"

#define LOCTEXT_NAMESPACE "FNodeSourceModule"

// --------------------------------------------------------------------
// 0. 命令定义
// --------------------------------------------------------------------
class FNodeSourceCommands : public TCommands<FNodeSourceCommands>
{
public:
	FNodeSourceCommands()
		: TCommands<FNodeSourceCommands>(
			TEXT("NodeSource"),
			NSLOCTEXT("Contexts", "NodeSource", "Node Source Plugin"),
			NAME_None,
			FAppStyle::GetAppStyleSetName()
		)
	{
	}

	virtual void RegisterCommands() override
	{

		UI_COMMAND(ToggleShowSelected, "Show Only When Selected", "Only show labels when nodes are selected", EUserInterfaceActionType::ToggleButton, FInputChord());
		UI_COMMAND(ToggleShowEngine, "Engine Plugins", "Toggle visibility of Engine plugins", EUserInterfaceActionType::ToggleButton, FInputChord());
		UI_COMMAND(ToggleShowMarketplace, "Marketplace Plugins", "Toggle visibility of Marketplace plugins", EUserInterfaceActionType::ToggleButton, FInputChord());
		UI_COMMAND(ToggleShowProject, "Project Plugins", "Toggle visibility of Project plugins", EUserInterfaceActionType::ToggleButton, FInputChord());
		UI_COMMAND(ToggleShowLibrary, "Function Libraries", "Toggle visibility of Function Library names", EUserInterfaceActionType::ToggleButton, FInputChord());
	}

public:
	TSharedPtr<FUICommandInfo> ToggleShowSelected; // 新增
	TSharedPtr<FUICommandInfo> ToggleShowEngine;
	TSharedPtr<FUICommandInfo> ToggleShowMarketplace;
	TSharedPtr<FUICommandInfo> ToggleShowProject;
	TSharedPtr<FUICommandInfo> ToggleShowLibrary;
};

// --------------------------------------------------------------------
// 1. 自定义带有 Badge 的节点 Widget (优化版)
// --------------------------------------------------------------------
class SNodeSourceGraphNode : public SGraphNodeK2Base
{
public:
	SLATE_BEGIN_ARGS(SNodeSourceGraphNode) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UEdGraphNode* InNode, FString InPluginName, FString InPluginType, FString InLibraryName)
	{
		this->GraphNode = InNode;
		this->CachedPluginName = InPluginName;
		this->CachedPluginType = InPluginType;
		this->CachedLibraryName = InLibraryName;

		// --- 优化：初始化时进行一次性计算 ---
		PrecalculateData();

		this->SetCursor(EMouseCursor::CardinalCross);
		this->UpdateGraphNode();
	}

	// 覆写 UpdateGraphNode，以防节点刷新时需要重新计算
	virtual void UpdateGraphNode() override
	{
		SGraphNodeK2Base::UpdateGraphNode();
	}

	virtual void CreateBelowPinControls(TSharedPtr<SVerticalBox> MainBox) override
	{
		SGraphNodeK2Base::CreateBelowPinControls(MainBox);

		if (!CachedPluginName.IsEmpty() || !CachedLibraryName.IsEmpty())
		{
			MainBox->AddSlot()
				.AutoHeight()
				.HAlign(HAlign_Center)
				.Padding(FMargin(0.0f, 2.0f, 0.0f, 0.0f))
				[
					SNew(SBox)
						.Visibility(this, &SNodeSourceGraphNode::GetBadgeContainerVisibility)
						[
							CreateBadgeWidget()
						]
				];
		}
	}

private:
	FString CachedPluginName;
	FString CachedPluginType;
	FString CachedLibraryName;

	// --- 优化变量：缓存计算结果 ---
	FLinearColor CachedBaseColor; // 缓存RGB，不含Alpha
	bool bIsExcluded;             // 缓存是否被排除

	void PrecalculateData()
	{
		const UNodeSourceSettings* PluginSettings = GetDefault<UNodeSourceSettings>();

		// 1. 预计算排除状态
		bIsExcluded = false;
		if (PluginSettings->ExcludedNames.Num() > 0)
		{
			for (const FString& ExcludedName : PluginSettings->ExcludedNames)
			{
				if ((!CachedPluginName.IsEmpty() && CachedPluginName.Equals(ExcludedName, ESearchCase::IgnoreCase)) ||
					(!CachedLibraryName.IsEmpty() && CachedLibraryName.Equals(ExcludedName, ESearchCase::IgnoreCase)))
				{
					bIsExcluded = true;
					break;
				}
			}
		}

		// 2. 预计算颜色 (只计算 RGB，Alpha 留给运行时控制)
		// 如果插件名和库名都为空，给个默认灰
		if (CachedPluginName.IsEmpty() && CachedLibraryName.IsEmpty())
		{
			CachedBaseColor = FLinearColor(0.2f, 0.2f, 0.2f);
			return;
		}

		bool bFoundCustom = false;

		// --- 核心修改：优先匹配自定义颜色 ---
		// 逻辑：
		// 1. 尝试用「库名/类名」去匹配自定义列表 (例如用户配置了 "Actor" 或 "KismetSystemLibrary")
		// 2. 尝试用「插件名」去匹配自定义列表 (例如用户配置了 "Paper2D")

		for (const FNodeSourceColorDef& ColorDef : PluginSettings->CustomColors)
		{
			// 优先检查库名 (这样你可以单独给 Actor 设置颜色，即使它属于 Engine)
			if (!CachedLibraryName.IsEmpty() && ColorDef.PluginName.Equals(CachedLibraryName, ESearchCase::IgnoreCase))
			{
				CachedBaseColor = ColorDef.Color;
				bFoundCustom = true;
				break;
			}
			// 其次检查插件名
			if (!CachedPluginName.IsEmpty() && ColorDef.PluginName.Equals(CachedPluginName, ESearchCase::IgnoreCase))
			{
				CachedBaseColor = ColorDef.Color;
				bFoundCustom = true;
				break;
			}
		}
		// --- 自动生成颜色 ---
		if (!bFoundCustom)
		{
			// 如果有插件名，用插件名生成哈希 (保持同一插件颜色一致)
			// 如果没有插件名 (比如某些原生类)，用库名生成哈希
			FString HashString = !CachedPluginName.IsEmpty() ? CachedPluginName : CachedLibraryName;
			uint32 Hash = GetTypeHash(HashString);
			const double GoldenRatioConjugate = 0.618033988749895;
			double H = (double)(Hash % 1000) / 1000.0;
			H += GoldenRatioConjugate;
			H = fmod(H, 1.0);
			float S = 0.6f + (0.2f * (float)((Hash / 10) % 10) / 10.0f);
			float V = 0.75f + (0.15f * (float)((Hash / 100) % 10) / 10.0f);
			CachedBaseColor = FLinearColor::MakeFromHSV8(
				(uint8)(H * 255.0),
				(uint8)(S * 255.0),
				(uint8)(V * 255.0)
			);
		}
	}

	TSharedRef<SWidget> CreateSinglePill(const FString& Text, bool bIsLibrary)
	{
		// 修改：移除描边颜色导致的白边，只指定填充色和圆角
		static const FSlateRoundedBoxBrush RoundedBrush(
			FLinearColor::White, 4.0f
		);

		return SNew(SBorder)
			.BorderImage(&RoundedBrush)
			.BorderBackgroundColor(this, &SNodeSourceGraphNode::GetBadgeColor)
			.Padding(FMargin(6.0f, 1.0f))
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.Visibility(this, bIsLibrary ? &SNodeSourceGraphNode::GetLibraryVisibility : &SNodeSourceGraphNode::GetPluginVisibility)
			[
				SNew(STextBlock)
					.Text(FText::FromString(Text))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
					.ColorAndOpacity(FLinearColor::White)
					.ShadowOffset(FVector2D::ZeroVector)
					.Justification(ETextJustify::Center)
			];
	}


	TSharedRef<SWidget> CreateBadgeWidget()
	{
		TSharedRef<SHorizontalBox> HBox = SNew(SHorizontalBox);

		if (!CachedPluginName.IsEmpty())
		{
			HBox->AddSlot()
				.AutoWidth()
				[
					CreateSinglePill(CachedPluginName, false)
				];
		}

		if (!CachedLibraryName.IsEmpty())
		{
			HBox->AddSlot()
				.AutoWidth()
				.Padding(FMargin((!CachedPluginName.IsEmpty() ? 4.0f : 0.0f), 0.0f, 0.0f, 0.0f))
				[
					CreateSinglePill(CachedLibraryName, true)
				];
		}

		return HBox;
	}

	// --- 核心逻辑 (性能优化后) ---

	EVisibility GetBadgeContainerVisibility() const
	{
		// 1. 直接检查缓存的排除状态 (极快)
		if (bIsExcluded) return EVisibility::Collapsed;

		const UNodeSourceSettings* PluginSettings = GetDefault<UNodeSourceSettings>();

		// 2. 插件类型过滤 (布尔检查，快)
		if (!CachedPluginName.IsEmpty())
		{
			if (CachedPluginType == TEXT("Engine") && !PluginSettings->bShowEnginePlugins) return EVisibility::Collapsed;
			if (CachedPluginType == TEXT("Marketplace") && !PluginSettings->bShowMarketplacePlugins) return EVisibility::Collapsed;
			if (CachedPluginType == TEXT("Project") && !PluginSettings->bShowProjectPlugins) return EVisibility::Collapsed;
		}

		// 3. 选中状态检查 (这是唯一必须实时检查的)
		if (PluginSettings->bShowOnSelectionOnly)
		{
			if (!GraphNode) return EVisibility::Collapsed;

			// 只有开启了这个选项才去获取 OwnerPanel，节省开销
			TSharedPtr<SGraphPanel> OwnerPanel = GetOwnerPanel();
			if (OwnerPanel.IsValid())
			{
				if (!OwnerPanel->SelectionManager.IsNodeSelected(GraphNode)) return EVisibility::Collapsed;
			}
			else
			{
				return EVisibility::Collapsed;
			}
		}

		// 4. 空值检查
		bool bShowLib = PluginSettings->bShowFunctionLibrary && !CachedLibraryName.IsEmpty();
		bool bShowPlugin = !CachedPluginName.IsEmpty();

		if (!bShowLib && !bShowPlugin) return EVisibility::Collapsed;

		return EVisibility::Visible;
	}

	EVisibility GetLibraryVisibility() const
	{
		const UNodeSourceSettings* PluginSettings = GetDefault<UNodeSourceSettings>();
		return (PluginSettings->bShowFunctionLibrary && !CachedLibraryName.IsEmpty()) ? EVisibility::Visible : EVisibility::Collapsed;
	}

	EVisibility GetPluginVisibility() const
	{
		return !CachedPluginName.IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed;
	}

	FSlateColor GetBadgeColor() const
	{
		// --- 优化：直接使用缓存的 RGB，只动态计算 Alpha ---
		const UNodeSourceSettings* PluginSettings = GetDefault<UNodeSourceSettings>();

		FLinearColor FinalColor = CachedBaseColor;
		FinalColor.A = PluginSettings->DefaultOpacity;

		return FSlateColor(FinalColor);
	}
};

// --------------------------------------------------------------------
// 2. 工厂实现
// --------------------------------------------------------------------
class FNodeSourceNodeFactory : public FGraphPanelNodeFactory
{
public:
	virtual TSharedPtr<class SGraphNode> CreateNode(class UEdGraphNode* Node) const override
	{
		if (!Node || !Node->IsA<UK2Node>()) return nullptr;

		FString LibraryName = TEXT("");
		UPackage* NodePkg = Node->GetClass()->GetOuterUPackage();

		if (UK2Node_CallFunction* CallFuncNode = Cast<UK2Node_CallFunction>(Node))
		{
			if (UFunction* Func = CallFuncNode->GetTargetFunction())
			{
				if (UClass* OuterClass = Func->GetOuterUClass())
				{
					LibraryName = OuterClass->GetName();
					LibraryName.RemoveFromEnd(TEXT("_C"));
				}

				if (UPackage* FuncPkg = Func->GetOutermost())
				{
					NodePkg = FuncPkg;
				}
			}
		}

		if (!NodePkg) return nullptr;
		FString PkgName = NodePkg->GetName();

		if (PkgName.StartsWith("/Script/CoreUObject")) return nullptr;

		FString FoundPluginName = TEXT("");
		FString PluginType = TEXT("");

		auto& PluginManager = IPluginManager::Get();
		TArray<TSharedRef<IPlugin>> Plugins = PluginManager.GetEnabledPlugins();

		for (const TSharedRef<IPlugin>& Plugin : Plugins)
		{
			FString Name = Plugin->GetName();

			if (PkgName.StartsWith(TEXT("/Script/") + Name) || PkgName.Contains(TEXT("/") + Name + TEXT("/")))
			{
				FoundPluginName = Name;

				EPluginLoadedFrom LoadedFrom = Plugin->GetLoadedFrom();
				FString BaseDir = Plugin->GetBaseDir();

				if (LoadedFrom == EPluginLoadedFrom::Project) PluginType = TEXT("Project");
				else if (BaseDir.Contains(TEXT("/Marketplace/"))) PluginType = TEXT("Marketplace");
				else PluginType = TEXT("Engine");
				break;
			}
		}

		if (FoundPluginName.IsEmpty() && LibraryName.IsEmpty()) return nullptr;

		return SNew(SNodeSourceGraphNode, Node, FoundPluginName, PluginType, LibraryName);
	}
};

// --------------------------------------------------------------------
// 3. 模块生命周期
// --------------------------------------------------------------------
void FNodeSourceModule::StartupModule()
{
	// 1. 注册图表节点工厂
	NodeFactory = MakeShareable(new FNodeSourceNodeFactory());
	FEdGraphUtilities::RegisterVisualNodeFactory(NodeFactory);

	// 2. 注册命令
	FNodeSourceCommands::Register();
	PluginCommands = MakeShareable(new FUICommandList);

	// 3. 绑定命令动作
	const auto BindToggle = [this](TSharedPtr<FUICommandInfo> Command, bool& bSettingValue)
		{
			PluginCommands->MapAction(
				Command,
				FExecuteAction::CreateLambda([&bSettingValue]() {
					bSettingValue = !bSettingValue;
					GetMutableDefault<UNodeSourceSettings>()->SaveConfig();
					}),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([&bSettingValue]() {
					return bSettingValue;
					})
			);
		};

	UNodeSourceSettings* Settings = GetMutableDefault<UNodeSourceSettings>();

	// --- 新增绑定 ---
	BindToggle(FNodeSourceCommands::Get().ToggleShowSelected, Settings->bShowOnSelectionOnly);

	// 原有绑定
	BindToggle(FNodeSourceCommands::Get().ToggleShowEngine, Settings->bShowEnginePlugins);
	BindToggle(FNodeSourceCommands::Get().ToggleShowMarketplace, Settings->bShowMarketplacePlugins);
	BindToggle(FNodeSourceCommands::Get().ToggleShowProject, Settings->bShowProjectPlugins);
	BindToggle(FNodeSourceCommands::Get().ToggleShowLibrary, Settings->bShowFunctionLibrary);

	// 4. 注册工具栏扩展
	FBlueprintEditorModule& BlueprintEditorModule = FModuleManager::LoadModuleChecked<FBlueprintEditorModule>("Kismet");

	TSharedPtr<FExtender> ToolbarExtender = MakeShareable(new FExtender);

	ToolbarExtender->AddToolBarExtension(
		"Settings",
		EExtensionHook::After,
		PluginCommands,
		FToolBarExtensionDelegate::CreateRaw(this, &FNodeSourceModule::AddToolbarExtension)
	);

	BlueprintEditorModule.GetMenuExtensibilityManager()->AddExtender(ToolbarExtender);
}

void FNodeSourceModule::ShutdownModule()
{
	FNodeSourceCommands::Unregister();

	if (NodeFactory.IsValid())
	{
		FEdGraphUtilities::UnregisterVisualNodeFactory(NodeFactory);
		NodeFactory.Reset();
	}
}

// --- 工具栏扩展实现 ---

void FNodeSourceModule::AddToolbarExtension(FToolBarBuilder& Builder)
{
	Builder.AddComboButton(
		FUIAction(),
		FOnGetContent::CreateRaw(this, &FNodeSourceModule::GenerateMenuContent),
		LOCTEXT("NodeSourceBtn", "Node Source"),
		LOCTEXT("NodeSourceBtnTooltip", "Configure Node Source Labels"),
		// 图标
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Visible")
	);
}

TSharedRef<SWidget> FNodeSourceModule::GenerateMenuContent()
{
	FMenuBuilder MenuBuilder(true, PluginCommands);

	MenuBuilder.BeginSection("Visibility", LOCTEXT("VisibilityHeader", "Label Visibility"));
	{

		MenuBuilder.AddMenuEntry(FNodeSourceCommands::Get().ToggleShowSelected, NAME_None, TAttribute<FText>(), TAttribute<FText>(), FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.SelectMode"));
		MenuBuilder.AddSeparator();

		MenuBuilder.AddMenuEntry(FNodeSourceCommands::Get().ToggleShowProject, NAME_None, TAttribute<FText>(), TAttribute<FText>(), FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Visible"));
		MenuBuilder.AddMenuEntry(FNodeSourceCommands::Get().ToggleShowMarketplace, NAME_None, TAttribute<FText>(), TAttribute<FText>(), FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Visible"));
		MenuBuilder.AddMenuEntry(FNodeSourceCommands::Get().ToggleShowEngine, NAME_None, TAttribute<FText>(), TAttribute<FText>(), FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Visible"));
		MenuBuilder.AddSeparator();
		MenuBuilder.AddMenuEntry(FNodeSourceCommands::Get().ToggleShowLibrary, NAME_None, TAttribute<FText>(), TAttribute<FText>(), FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Search"));
	}
	MenuBuilder.EndSection();

	MenuBuilder.AddMenuEntry(
		LOCTEXT("OpenSettings", "Open Advanced Settings..."),
		LOCTEXT("OpenSettingsTooltip", "Open the full settings panel"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Settings"),
		FUIAction(FExecuteAction::CreateLambda([]() {
			FModuleManager::LoadModuleChecked<ISettingsModule>("Settings").ShowViewer("Editor", "Plugins", "NodeSource");
			}))
	);

	return MenuBuilder.MakeWidget();
}

#undef LOCTEXT_NAMESPACE
IMPLEMENT_MODULE(FNodeSourceModule, NodeSource)
