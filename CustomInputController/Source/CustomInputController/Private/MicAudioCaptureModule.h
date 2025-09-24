#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "MicAudioCaptureCommands.h"

class FToolBarBuilder;
class FMenuBuilder;
class UMicAudioCaptureSubsystem;

/**
 * 麦克风音频捕获模块
 */
class FMicAudioCaptureModule : public IModuleInterface
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** 添加工具栏按钮 */
	void AddToolbarExtension(FToolBarBuilder& Builder);

	/** 添加菜单扩展 */
	void AddMenuExtension(FMenuBuilder& Builder);

private:
	/** 命令列表 */
	TSharedPtr<FUICommandList> PluginCommands;

	/** 命令处理函数 */
	void OpenMicAudioCaptureSettings();
	void StartMicCapture();
	void StopMicCapture();
	void ConnectToServer();
	void DisconnectFromServer();
	void RefreshMicDevices();

	/** 获取子系统实例 */
	UMicAudioCaptureSubsystem* GetMicAudioCaptureSubsystem() const;
};
