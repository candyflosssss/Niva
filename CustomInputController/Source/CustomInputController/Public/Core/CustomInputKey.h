#pragma once

#include "CoreMinimal.h"
#include "IInputDeviceModule.h"
#include "InputCoreTypes.h"
#include "CoreMinimal.h"
#include "IInputDevice.h"
#include "GenericPlatform/IInputInterface.h"
#include "Input/UUDPHandler.h"
#include "CustomInputKey.h"

/**
 * 自定义UDP输入键定义
 * 这个类管理所有自定义的输入键，包括3D向量轴和按钮
 */
struct CUSTOMINPUTCONTROLLER_API FMyCustomInputKeys
{
	static const FKey GazeXYZ;
	// 3D向量轴键
	static const FKey Gaze_X;
	static const FKey Gaze_Y; 
	static const FKey Gaze_Z;
    
	// 按钮键
	static const FKey UDPButton1;
	static const FKey UDPButton2;
    
	// 键管理方法
	static void AddKeys();
	static void RemoveKeys();
    
	// 调试方法
	static void LogAllKeys();
	static bool ValidateKeysRegistered();

private:
	// 存储当前3D向量值
	static FVector CurrentUDPVector;
};

class FUDPInputDevice : public IInputDevice
{
public:
	FUDPInputDevice(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler);
	virtual ~FUDPInputDevice();

	// IInputDevice interface
	virtual void Tick(float DeltaTime) override;
	virtual void SendControllerEvents() override;
	virtual void SetMessageHandler(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler) override;
	virtual bool Exec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar) override { return false; }
	virtual void SetChannelValue(int32 ControllerId, FForceFeedbackChannelType ChannelType, float Value) override {}
	virtual void SetChannelValues(int32 ControllerId, const FForceFeedbackValues& Values) override {}

private:
	/** UDP处理器 */
	UUDPHandler* UDPHandler;

	/** 处理接收到的UDP数据 */
	UFUNCTION()
	void OnUDPDataReceived(const FString& ReceivedData);

	/** 解析文本为XYZ坐标 */
	void ParseTextToXYZ(const FString& InText, float& OutX, float& OutY, float& OutZ);

	// 输入路由
	TSharedRef<FGenericApplicationMessageHandler> MessageHandler;
	
	// 当前值，使用原子操作保证线程安全
	std::atomic<float> CurrentX;
	std::atomic<float> CurrentY;
	std::atomic<float> CurrentZ;

	FInputDeviceId DeviceId;
};