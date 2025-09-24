#include "MicAudioCaptureCommands.h"

void FMicAudioCaptureCommands::RegisterCommands()
{
	UI_COMMAND(
		OpenMicAudioCaptureSettings,
		"麦克风音频捕获设置",
		"打开麦克风音频捕获设置",
		EUserInterfaceActionType::Button,
		FInputChord()
	);

	UI_COMMAND(
		StartMicCapture,
		"开始麦克风捕获",
		"开始麦克风音频捕获",
		EUserInterfaceActionType::Button,
		FInputChord()
	);

	UI_COMMAND(
		StopMicCapture,
		"停止麦克风捕获",
		"停止麦克风音频捕获",
		EUserInterfaceActionType::Button,
		FInputChord()
	);

	UI_COMMAND(
		ConnectToServer,
		"连接到服务器",
		"连接到WebSocket服务器",
		EUserInterfaceActionType::Button,
		FInputChord()
	);

	UI_COMMAND(
		DisconnectFromServer,
		"断开服务器连接",
		"断开与WebSocket服务器的连接",
		EUserInterfaceActionType::Button,
		FInputChord()
	);

	UI_COMMAND(
		RefreshMicDevices,
		"刷新麦克风设备",
		"刷新可用的麦克风设备列表",
		EUserInterfaceActionType::Button,
		FInputChord()
	);
}
