#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
// --- 新增头文件 ---
#include "Framework/Commands/Commands.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

class FNodeSourceModule : public IModuleInterface
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	// 用于存储已注册的图表节点工厂的指针
	TSharedPtr<class FNodeSourceNodeFactory> NodeFactory;

	// --- 工具栏扩展相关成员变量 ---
	TSharedPtr<class FUICommandList> PluginCommands;

	// --- 内部函数声明 ---
	void AddToolbarExtension(FToolBarBuilder& Builder);
	TSharedRef<SWidget> GenerateMenuContent();
};
