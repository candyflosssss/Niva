#include "MicAudioCaptureModule.h"
#include "MicAudioCaptureCommands.h"
#include "MicAudioCaptureComponent.h"
#include "MicAudioCaptureSubsystem.h"
#include "MicAudioCaptureSettings.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/AppStyle.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Engine/GameInstance.h"
#include "LevelEditor.h"
#include "ISettingsModule.h"
#include "ISettingsSection.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "FMicAudioCaptureModule"

void FMicAudioCaptureModule::StartupModule()
{
	// 注册命令
	FMicAudioCaptureCommands::Register();
	PluginCommands = MakeShareable(new FUICommandList);

	// 绑定命令处理函数
	PluginCommands->MapAction(
		FMicAudioCaptureCommands::Get().OpenMicAudioCaptureSettings,
		FExecuteAction::CreateRaw(this, &FMicAudioCaptureModule::OpenMicAudioCaptureSettings),
		FCanExecuteAction());

	PluginCommands->MapAction(
		FMicAudioCaptureCommands::Get().StartMicCapture,
		FExecuteAction::CreateRaw(this, &FMicAudioCaptureModule::StartMicCapture),
		FCanExecuteAction());

	PluginCommands->MapAction(
		FMicAudioCaptureCommands::Get().StopMicCapture,
		FExecuteAction::CreateRaw(this, &FMicAudioCaptureModule::StopMicCapture),
		FCanExecuteAction());

	PluginCommands->MapAction(
		FMicAudioCaptureCommands::Get().ConnectToServer,
		FExecuteAction::CreateRaw(this, &FMicAudioCaptureModule::ConnectToServer),
		FCanExecuteAction());

	PluginCommands->MapAction(
		FMicAudioCaptureCommands::Get().DisconnectFromServer,
		FExecuteAction::CreateRaw(this, &FMicAudioCaptureModule::DisconnectFromServer),
		FCanExecuteAction());

	PluginCommands->MapAction(
		FMicAudioCaptureCommands::Get().RefreshMicDevices,
		FExecuteAction::CreateRaw(this, &FMicAudioCaptureModule::RefreshMicDevices),
		FCanExecuteAction());

	// 获取LevelEditor模块
	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");

	// 添加扩展到LevelEditor工具栏
	{
		TSharedPtr<FExtender> ToolbarExtender = MakeShareable(new FExtender);
		ToolbarExtender->AddToolBarExtension(
			"Settings",
			EExtensionHook::After,
			PluginCommands,
			FToolBarExtensionDelegate::CreateRaw(this, &FMicAudioCaptureModule::AddToolbarExtension));

		LevelEditorModule.GetToolBarExtensibilityManager()->AddExtender(ToolbarExtender);
	}

	// 添加扩展到LevelEditor菜单
	{
		TSharedPtr<FExtender> MenuExtender = MakeShareable(new FExtender);
		MenuExtender->AddMenuExtension(
			"WindowLayout",
			EExtensionHook::After,
			PluginCommands,
			FMenuExtensionDelegate::CreateRaw(this, &FMicAudioCaptureModule::AddMenuExtension));

		LevelEditorModule.GetMenuExtensibilityManager()->AddExtender(MenuExtender);
	}

	// 注册设置
	ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");
	if (SettingsModule != nullptr)
	{
		ISettingsSectionPtr SettingsSection = SettingsModule->RegisterSettings("Project", "Plugins", "MicAudioCapture",
			LOCTEXT("麦克风音频捕获", "MicAudioCapture"),
			LOCTEXT("麦克风音频捕获和WebSocket推流设置", "MicAudioCapture"),
			GetMutableDefault<UMicAudioCaptureSettings>());
	}
}

void FMicAudioCaptureModule::ShutdownModule()
{
	// 注销命令
	FMicAudioCaptureCommands::Unregister();

	// 注销设置
	ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");
	if (SettingsModule != nullptr)
	{
		SettingsModule->UnregisterSettings("Project", "Plugins", "MicAudioCapture");
	}
}

void FMicAudioCaptureModule::AddToolbarExtension(FToolBarBuilder& Builder)
{
	Builder.BeginSection("MicAudioCapture");
	{
		Builder.AddToolBarButton(
			FMicAudioCaptureCommands::Get().OpenMicAudioCaptureSettings,
			NAME_None,
			LOCTEXT("麦克风设置", "MicAudioCapture"),
			LOCTEXT("打开麦克风音频捕获设置", "MicAudioCapture"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.GameSettings"));

		// 添加分隔符
		Builder.AddSeparator();

		Builder.AddToolBarButton(
			FMicAudioCaptureCommands::Get().StartMicCapture,
			NAME_None,
			LOCTEXT("开始捕获", "MicAudioCapture"),
			LOCTEXT("开始麦克风音频捕获", "MicAudioCapture"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Record"));

		Builder.AddToolBarButton(
			FMicAudioCaptureCommands::Get().StopMicCapture,
			NAME_None,
			LOCTEXT("停止捕获", "MicAudioCapture"),
			LOCTEXT("停止麦克风音频捕获", "MicAudioCapture"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Stop"));

		// 添加分隔符
		Builder.AddSeparator();

		Builder.AddToolBarButton(
			FMicAudioCaptureCommands::Get().ConnectToServer,
			NAME_None,
			LOCTEXT("连接服务器", "MicAudioCapture"),
			LOCTEXT("连接到WebSocket服务器", "MicAudioCapture"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Connect"));

		Builder.AddToolBarButton(
			FMicAudioCaptureCommands::Get().DisconnectFromServer,
			NAME_None,
			LOCTEXT("断开连接", "MicAudioCapture"),
			LOCTEXT("断开与WebSocket服务器的连接", "MicAudioCapture"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Disconnect"));

		// 添加分隔符
		Builder.AddSeparator();

		Builder.AddToolBarButton(
			FMicAudioCaptureCommands::Get().RefreshMicDevices,
			NAME_None,
			LOCTEXT("刷新设备", "MicAudioCapture"),
			LOCTEXT("刷新可用的麦克风设备列表", "MicAudioCapture"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Refresh"));
	}
	Builder.EndSection();
}

void FMicAudioCaptureModule::AddMenuExtension(FMenuBuilder& Builder)
{
	Builder.BeginSection("MicAudioCapture", LOCTEXT("麦克风音频捕获", "麦克风音频捕获"));
	{
		Builder.AddMenuEntry(FMicAudioCaptureCommands::Get().OpenMicAudioCaptureSettings);
		Builder.AddMenuSeparator();
		Builder.AddMenuEntry(FMicAudioCaptureCommands::Get().StartMicCapture);
		Builder.AddMenuEntry(FMicAudioCaptureCommands::Get().StopMicCapture);
		Builder.AddMenuSeparator();
		Builder.AddMenuEntry(FMicAudioCaptureCommands::Get().ConnectToServer);
		Builder.AddMenuEntry(FMicAudioCaptureCommands::Get().DisconnectFromServer);
		Builder.AddMenuSeparator();
		Builder.AddMenuEntry(FMicAudioCaptureCommands::Get().RefreshMicDevices);
	}
	Builder.EndSection();
}

void FMicAudioCaptureModule::OpenMicAudioCaptureSettings()
{
	// 打开设置页面
	FModuleManager::LoadModuleChecked<ISettingsModule>("Settings")
		.ShowViewer("Project", "Plugins", "MicAudioCapture");
}

void FMicAudioCaptureModule::StartMicCapture()
{
	UMicAudioCaptureSubsystem* Subsystem = GetMicAudioCaptureSubsystem();
	if (Subsystem)
	{
		// 默认使用第一个设备
		Subsystem->StartCapture(0);
	}
}

void FMicAudioCaptureModule::StopMicCapture()
{
	UMicAudioCaptureSubsystem* Subsystem = GetMicAudioCaptureSubsystem();
	if (Subsystem)
	{
		Subsystem->StopCapture();
	}
}

void FMicAudioCaptureModule::ConnectToServer()
{
	UMicAudioCaptureSubsystem* Subsystem = GetMicAudioCaptureSubsystem();
	if (Subsystem)
	{
		const UMicAudioCaptureSettings* Settings = UMicAudioCaptureSettings::Get();
		if (Settings)
		{
			Subsystem->ConnectToServer(Settings->DefaultServerUrl);
		}
	}
}

void FMicAudioCaptureModule::DisconnectFromServer()
{
	UMicAudioCaptureSubsystem* Subsystem = GetMicAudioCaptureSubsystem();
	if (Subsystem)
	{
		Subsystem->DisconnectFromServer();
	}
}

void FMicAudioCaptureModule::RefreshMicDevices()
{
	UMicAudioCaptureSubsystem* Subsystem = GetMicAudioCaptureSubsystem();
	if (Subsystem)
	{
		Subsystem->RefreshMicDevices();
	}
}

UMicAudioCaptureSubsystem* FMicAudioCaptureModule::GetMicAudioCaptureSubsystem() const
{
	UGameInstance* GameInstance = nullptr;

	// 尝试从编辑器世界中获取GameInstance
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		if (Context.WorldType == EWorldType::PIE || Context.WorldType == EWorldType::Game)
		{
			GameInstance = Context.OwningGameInstance;
			break;
		}
		else if (Context.WorldType == EWorldType::Editor && !GameInstance)
		{
			GameInstance = Context.OwningGameInstance;
		}
	}

	if (GameInstance)
	{
		return GameInstance->GetSubsystem<UMicAudioCaptureSubsystem>();
	}

	return nullptr;
}

#undef LOCTEXT_NAMESPACE
