#include "MicAudioCaptureDebugWidget.h"
#include "MicAudioCaptureSubsystem.h"
#include "MicAudioCaptureSettings.h"
#include "Components/TextBlock.h"
#include "Components/ProgressBar.h"
#include "Components/ComboBoxString.h"
#include "Components/Button.h"
#include "Components/VerticalBox.h"
#include "Components/CheckBox.h"
#include "Components/Slider.h"

UMicAudioCaptureDebugWidget::UMicAudioCaptureDebugWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 启用Tick以定期更新UI
	bIsFocusable = true;
}

void UMicAudioCaptureDebugWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 获取子系统和设置
	MicSubsystem = GetGameInstance()->GetSubsystem<UMicAudioCaptureSubsystem>();
	SettingsInstance = GetMutableDefault<UMicAudioCaptureSettings>();

	// 绑定按钮事件
	if (RefreshDevicesButton)
	{
		RefreshDevicesButton->OnClicked.AddDynamic(this, &UMicAudioCaptureDebugWidget::OnRefreshDevicesClicked);
	}

	if (StartCaptureButton)
	{
		StartCaptureButton->OnClicked.AddDynamic(this, &UMicAudioCaptureDebugWidget::OnStartCaptureClicked);
	}

	if (StopCaptureButton)
	{
		StopCaptureButton->OnClicked.AddDynamic(this, &UMicAudioCaptureDebugWidget::OnStopCaptureClicked);
	}

	if (ConnectButton)
	{
		ConnectButton->OnClicked.AddDynamic(this, &UMicAudioCaptureDebugWidget::OnConnectClicked);
	}

	if (DisconnectButton)
	{
		DisconnectButton->OnClicked.AddDynamic(this, &UMicAudioCaptureDebugWidget::OnDisconnectClicked);
	}

	// 绑定滑块事件
	if (VolumeMultiplierSlider)
	{
		VolumeMultiplierSlider->OnValueChanged.AddDynamic(this, &UMicAudioCaptureDebugWidget::OnVolumeMultiplierChanged);
	}

	// 绑定下拉框事件
	if (SampleRateComboBox)
	{
		SampleRateComboBox->OnSelectionChanged.AddDynamic(this, &UMicAudioCaptureDebugWidget::OnSampleRateChanged);
	}

	if (ChannelsComboBox)
	{
		ChannelsComboBox->OnSelectionChanged.AddDynamic(this, &UMicAudioCaptureDebugWidget::OnChannelsChanged);
	}

	if (DeviceComboBox)
	{
		DeviceComboBox->OnSelectionChanged.AddDynamic(this, &UMicAudioCaptureDebugWidget::OnDeviceSelectionChanged);
	}

	// 绑定复选框事件
	if (EnableDebugLogsCheckBox)
	{
		EnableDebugLogsCheckBox->OnCheckStateChanged.AddDynamic(this, &UMicAudioCaptureDebugWidget::OnEnableDebugLogsChanged);
	}

	// 绑定子系统事件
	if (MicSubsystem)
	{
		MicSubsystem->OnAudioLevelUpdated.AddDynamic(this, &UMicAudioCaptureDebugWidget::OnAudioLevelUpdated);
		MicSubsystem->OnMicDevicesUpdated.AddDynamic(this, &UMicAudioCaptureDebugWidget::OnMicDevicesUpdated);
		MicSubsystem->OnWebSocketConnected.AddDynamic(this, &UMicAudioCaptureDebugWidget::OnWebSocketConnected);
		MicSubsystem->OnWebSocketError.AddDynamic(this, &UMicAudioCaptureDebugWidget::OnWebSocketError);
	}

	// 初始化UI
	InitializeUI();
}

void UMicAudioCaptureDebugWidget::NativeDestruct()
{
	// 解绑事件
	if (MicSubsystem)
	{
		MicSubsystem->OnAudioLevelUpdated.RemoveAll(this);
		MicSubsystem->OnMicDevicesUpdated.RemoveAll(this);
		MicSubsystem->OnWebSocketConnected.RemoveAll(this);
		MicSubsystem->OnWebSocketError.RemoveAll(this);
	}

	Super::NativeDestruct();
}

void UMicAudioCaptureDebugWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// 更新UI状态
	UpdateUI();
}

void UMicAudioCaptureDebugWidget::InitializeUI()
{
	// 初始化采样率下拉框
	if (SampleRateComboBox)
	{
		SampleRateComboBox->ClearOptions();
		SampleRateComboBox->AddOption(TEXT("8000 Hz"));
		SampleRateComboBox->AddOption(TEXT("11025 Hz"));
		SampleRateComboBox->AddOption(TEXT("16000 Hz"));
		SampleRateComboBox->AddOption(TEXT("22050 Hz"));
		SampleRateComboBox->AddOption(TEXT("32000 Hz"));
		SampleRateComboBox->AddOption(TEXT("44100 Hz"));
		SampleRateComboBox->AddOption(TEXT("48000 Hz"));

		// 设置当前值
		int32 CurrentSampleRate = MicSubsystem ? MicSubsystem->GetSampleRate() : SettingsInstance->DefaultSampleRate;
		FString CurrentSampleRateStr = FString::Printf(TEXT("%d Hz"), CurrentSampleRate);
		SampleRateComboBox->SetSelectedOption(CurrentSampleRateStr);
	}

	// 初始化声道数下拉框
	if (ChannelsComboBox)
	{
		ChannelsComboBox->ClearOptions();
		ChannelsComboBox->AddOption(TEXT("1 (单声道)"));
		ChannelsComboBox->AddOption(TEXT("2 (立体声)"));

		// 设置当前值
		int32 CurrentChannels = MicSubsystem ? MicSubsystem->GetNumChannels() : SettingsInstance->DefaultNumChannels;
		FString CurrentChannelsStr = FString::Printf(TEXT("%d %s"), CurrentChannels, CurrentChannels == 1 ? TEXT("(单声道)") : TEXT("(立体声)"));
		ChannelsComboBox->SetSelectedOption(CurrentChannelsStr);
	}

	// 设置音量倍增器滑块
	if (VolumeMultiplierSlider)
	{
		float CurrentVolumeMultiplier = MicSubsystem ? MicSubsystem->GetVolumeMultiplier() : SettingsInstance->DefaultVolumeMultiplier;
		VolumeMultiplierSlider->SetMinValue(0.1f);
		VolumeMultiplierSlider->SetMaxValue(5.0f);
		VolumeMultiplierSlider->SetValue(CurrentVolumeMultiplier);
	}

	// 设置调试日志复选框
	if (EnableDebugLogsCheckBox)
	{
		bool bDebugLogs = MicSubsystem ? MicSubsystem->GetEnableDebugLogs() : SettingsInstance->bEnableDebugLogs;
		EnableDebugLogsCheckBox->SetCheckedState(bDebugLogs ? ECheckBoxState::Checked : ECheckBoxState::Unchecked);
	}

	// 刷新麦克风设备列表
	OnRefreshDevicesClicked();

	// 更新音量乘数文本
	if (VolumeMultiplierText)
	{
		float CurrentVolumeMultiplier = MicSubsystem ? MicSubsystem->GetVolumeMultiplier() : SettingsInstance->DefaultVolumeMultiplier;
		VolumeMultiplierText->SetText(FText::FromString(FString::Printf(TEXT("音量倍增: %.1fx"), CurrentVolumeMultiplier)));
	}
}

void UMicAudioCaptureDebugWidget::UpdateUI()
{
	if (!MicSubsystem)
	{
		return;
	}

	// 更新状态文本
	if (StatusText)
	{
		FString StatusStr;
		if (MicSubsystem->IsCapturing())
		{
			StatusStr = TEXT("状态: 正在捕获");
			StatusText->SetColorAndOpacity(FSlateColor(FLinearColor(0.0f, 1.0f, 0.0f, 1.0f)));
		}
		else
		{
			StatusStr = TEXT("状态: 已停止");
			StatusText->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.3f, 0.3f, 1.0f)));
		}

		StatusText->SetText(FText::FromString(StatusStr));
	}

	// 更新服务器状态
	if (ServerStatusText)
	{
		FString ServerStatusStr;
		if (MicSubsystem->IsConnectedToServer())
		{
			ServerStatusStr = TEXT("服务器: 已连接");
			ServerStatusText->SetColorAndOpacity(FSlateColor(FLinearColor(0.0f, 1.0f, 0.0f, 1.0f)));
		}
		else
		{
			ServerStatusStr = TEXT("服务器: 未连接");
			ServerStatusText->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.3f, 0.3f, 1.0f)));
		}

		ServerStatusText->SetText(FText::FromString(ServerStatusStr));
	}

	// 更新麦克风状态
	if (MicStatusText)
	{
		FString MicStatusStr;
		if (MicSubsystem->IsCapturing())
		{
			int32 DeviceIndex = 0; // 当前设备索引，这里应该从组件获取
			TArray<FString> Devices = MicSubsystem->GetAvailableMicDevices();
			if (Devices.IsValidIndex(DeviceIndex))
			{
				MicStatusStr = FString::Printf(TEXT("麦克风: %s"), *Devices[DeviceIndex]);
			}
			else
			{
				MicStatusStr = TEXT("麦克风: 未知设备");
			}
		}
		else
		{
			MicStatusStr = TEXT("麦克风: 未激活");
		}

		MicStatusText->SetText(FText::FromString(MicStatusStr));
	}

	// 更新当前音量文本
	if (CurrentVolumeText)
	{
		float AudioLevel = MicSubsystem->GetCurrentAudioLevel();
		CurrentVolumeText->SetText(FText::FromString(FString::Printf(TEXT("当前音量: %.2f"), AudioLevel)));
	}

	// 更新设备计数文本
	if (DeviceCountText)
	{
		int32 DeviceCount = MicSubsystem->GetAvailableMicDevices().Num();
		DeviceCountText->SetText(FText::FromString(FString::Printf(TEXT("可用设备数: %d"), DeviceCount)));
	}

	// 更新当前设备文本
	if (CurrentDeviceText && DeviceComboBox)
	{
		FString CurrentDevice = DeviceComboBox->GetSelectedOption();
		if (CurrentDevice.IsEmpty())
		{
			CurrentDevice = TEXT("无");
		}
		CurrentDeviceText->SetText(FText::FromString(FString::Printf(TEXT("当前设备: %s"), *CurrentDevice)));
	}

	// 更新按钮状态
	if (StartCaptureButton)
	{
		StartCaptureButton->SetIsEnabled(!MicSubsystem->IsCapturing());
	}

	if (StopCaptureButton)
	{
		StopCaptureButton->SetIsEnabled(MicSubsystem->IsCapturing());
	}

	if (ConnectButton)
	{
		ConnectButton->SetIsEnabled(!MicSubsystem->IsConnectedToServer());
	}

	if (DisconnectButton)
	{
		DisconnectButton->SetIsEnabled(MicSubsystem->IsConnectedToServer());
	}
}

void UMicAudioCaptureDebugWidget::OnRefreshDevicesClicked()
{
	if (MicSubsystem)
	{
		MicSubsystem->RefreshMicDevices();
	}
}

void UMicAudioCaptureDebugWidget::OnStartCaptureClicked()
{
	if (MicSubsystem)
	{
		int32 DeviceIndex = DeviceComboBox ? DeviceComboBox->GetSelectedIndex() : 0;
		MicSubsystem->StartCapture(DeviceIndex);
	}
}

void UMicAudioCaptureDebugWidget::OnStopCaptureClicked()
{
	if (MicSubsystem)
	{
		MicSubsystem->StopCapture();
	}
}

void UMicAudioCaptureDebugWidget::OnConnectClicked()
{
	if (MicSubsystem && SettingsInstance)
	{
		MicSubsystem->ConnectToServer(SettingsInstance->DefaultServerUrl);
	}
}

void UMicAudioCaptureDebugWidget::OnDisconnectClicked()
{
	if (MicSubsystem)
	{
		MicSubsystem->DisconnectFromServer();
	}
}

void UMicAudioCaptureDebugWidget::OnVolumeMultiplierChanged(float Value)
{
	if (MicSubsystem)
	{
		MicSubsystem->SetVolumeMultiplier(Value);
	}

	if (VolumeMultiplierText)
	{
		VolumeMultiplierText->SetText(FText::FromString(FString::Printf(TEXT("音量倍增: %.1fx"), Value)));
	}
}

void UMicAudioCaptureDebugWidget::OnSampleRateChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	if (MicSubsystem && !SelectedItem.IsEmpty())
	{
		// 解析采样率值
		int32 SampleRate = FCString::Atoi(*SelectedItem);
		if (SampleRate > 0)
		{
			MicSubsystem->SetSampleRate(SampleRate);
		}
	}
}

void UMicAudioCaptureDebugWidget::OnChannelsChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	if (MicSubsystem && !SelectedItem.IsEmpty())
	{
		// 解析声道数
		int32 Channels = SelectedItem.StartsWith(TEXT("1")) ? 1 : 2;
		MicSubsystem->SetNumChannels(Channels);
	}
}

void UMicAudioCaptureDebugWidget::OnEnableDebugLogsChanged(bool bIsChecked)
{
	if (MicSubsystem)
	{
		MicSubsystem->SetEnableDebugLogs(bIsChecked);
	}
}

void UMicAudioCaptureDebugWidget::OnDeviceSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	// 保存设备选择，但不立即应用，等待用户点击StartCapture按钮
}

void UMicAudioCaptureDebugWidget::OnAudioLevelUpdated(float Level)
{
	// 更新音量进度条
	if (VolumeProgressBar)
	{
		VolumeProgressBar->SetPercent(Level);

		// 根据音量级别设置颜色
		FLinearColor BarColor;
		if (Level < 0.3f)
		{
			BarColor = FLinearColor(0.0f, 1.0f, 0.0f, 1.0f); // 绿色表示低音量
		}
		else if (Level < 0.7f)
		{
			BarColor = FLinearColor(1.0f, 1.0f, 0.0f, 1.0f); // 黄色表示中等音量
		}
		else
		{
			BarColor = FLinearColor(1.0f, 0.0f, 0.0f, 1.0f); // 红色表示高音量
		}

		VolumeProgressBar->SetFillColorAndOpacity(BarColor);
	}

	// 更新音量文本
	if (CurrentVolumeText)
	{
		CurrentVolumeText->SetText(FText::FromString(FString::Printf(TEXT("当前音量: %.2f"), Level)));
	}
}

void UMicAudioCaptureDebugWidget::OnMicDevicesUpdated(const TArray<FString>& DeviceNames)
{
	// 更新设备下拉框
	if (DeviceComboBox)
	{
		DeviceComboBox->ClearOptions();

		for (const FString& DeviceName : DeviceNames)
		{
			DeviceComboBox->AddOption(DeviceName);
		}

		// 如果有设备，默认选择第一个
		if (DeviceNames.Num() > 0)
		{
			DeviceComboBox->SetSelectedIndex(0);
		}
	}

	// 更新设备计数文本
	if (DeviceCountText)
	{
		DeviceCountText->SetText(FText::FromString(FString::Printf(TEXT("可用设备数: %d"), DeviceNames.Num())));
	}
}

void UMicAudioCaptureDebugWidget::OnWebSocketConnected(const FString& ServerAddress)
{
	if (ServerStatusText)
	{
		FString StatusStr = FString::Printf(TEXT("服务器: 已连接到 %s"), *ServerAddress);
		ServerStatusText->SetText(FText::FromString(StatusStr));
		ServerStatusText->SetColorAndOpacity(FSlateColor(FLinearColor(0.0f, 1.0f, 0.0f, 1.0f)));
	}
}

void UMicAudioCaptureDebugWidget::OnWebSocketError(const FString& ErrorMsg, int32 ErrorCode)
{
	if (ServerStatusText)
	{
		FString StatusStr = FString::Printf(TEXT("服务器错误: %s (%d)"), *ErrorMsg, ErrorCode);
		ServerStatusText->SetText(FText::FromString(StatusStr));
		ServerStatusText->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.0f, 0.0f, 1.0f)));
	}
}
