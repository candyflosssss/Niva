#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/TextBlock.h"
#include "Components/ProgressBar.h"
#include "Components/ComboBoxString.h"
#include "Components/Button.h"
#include "Components/VerticalBox.h"
#include "Components/CheckBox.h"
#include "Components/Slider.h"
#include "MicAudioCaptureDebugWidget.generated.h"

/**
 * 麦克风音频捕获调试界面小部件
 */
UCLASS()
class CUSTOMINPUTCONTROLLER_API UMicAudioCaptureDebugWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UMicAudioCaptureDebugWidget(const FObjectInitializer& ObjectInitializer);

	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// UI组件
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	UTextBlock* StatusText;

	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	UProgressBar* VolumeProgressBar;

	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	UComboBoxString* DeviceComboBox;

	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	UButton* RefreshDevicesButton;

	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	UButton* StartCaptureButton;

	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	UButton* StopCaptureButton;

	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	UButton* ConnectButton;

	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	UButton* DisconnectButton;

	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	USlider* VolumeMultiplierSlider;

	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	UTextBlock* VolumeMultiplierText;

	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	UComboBoxString* SampleRateComboBox;

	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	UComboBoxString* ChannelsComboBox;

	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	UCheckBox* EnableDebugLogsCheckBox;

	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	UTextBlock* ServerStatusText;

	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	UTextBlock* MicStatusText;

	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	UTextBlock* CurrentVolumeText;

	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	UTextBlock* DeviceCountText;

	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	UTextBlock* CurrentDeviceText;

private:
	// 事件处理函数
	UFUNCTION()
	void OnRefreshDevicesClicked();

	UFUNCTION()
	void OnStartCaptureClicked();

	UFUNCTION()
	void OnStopCaptureClicked();

	UFUNCTION()
	void OnConnectClicked();

	UFUNCTION()
	void OnDisconnectClicked();

	UFUNCTION()
	void OnVolumeMultiplierChanged(float Value);

	UFUNCTION()
	void OnSampleRateChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

	UFUNCTION()
	void OnChannelsChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

	UFUNCTION()
	void OnEnableDebugLogsChanged(bool bIsChecked);

	UFUNCTION()
	void OnDeviceSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

	// 事件接收函数
	UFUNCTION()
	void OnAudioLevelUpdated(float Level);

	UFUNCTION()
	void OnMicDevicesUpdated(const TArray<FString>& DeviceNames);

	UFUNCTION()
	void OnWebSocketConnected(const FString& ServerAddress);

	UFUNCTION()
	void OnWebSocketError(const FString& ErrorMsg, int32 ErrorCode);

	// 子系统和设置
	class UMicAudioCaptureSubsystem* MicSubsystem;
	class UMicAudioCaptureSettings* SettingsInstance;

	// 更新UI
	void UpdateUI();
	void InitializeUI();
};
