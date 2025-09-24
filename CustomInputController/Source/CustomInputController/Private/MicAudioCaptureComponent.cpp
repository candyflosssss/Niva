#include "MicAudioCaptureComponent.h"
#include "MediaStreamPacket.h"
#include "Async/Async.h"
#include "WebSocketsModule.h"
#include "Components/AudioComponent.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY(LogMicAudioCapture);

UMicAudioCaptureComponent::UMicAudioCaptureComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.033f; // 约30fps的更新频率

	// 初始化默认值
	SampleRate = 16000;
	NumChannels = 1;
	BufferSizeMs = 100;
	VolumeMultiplier = 1.0f;
	AudioLevelUpdateInterval = 0.1f;
	ReconnectAttempts = 3;
	ReconnectInterval = 2.0f;
	bEnableDebugLogs = false;
	bShowAudioLevelInEditor = true;
	ChunkSizeBytes = 4096;

	CurrentAudioLevel = 0.0f;
	LastAudioLevelUpdateTime = 0.0f;
	bIsCapturing = false;
	CurrentDeviceIndex = 0;
	CurrentReconnectAttempts = 0;
	CurrentServerUrl = FString(TEXT(""));
}

void UMicAudioCaptureComponent::BeginPlay()
{
	Super::BeginPlay();

	// 初始化WebSockets模块
	if (!FModuleManager::Get().IsModuleLoaded("WebSockets"))
	{
		FModuleManager::Get().LoadModule("WebSockets");
	}

	// 刷新可用的麦克风设备列表
	RefreshMicDevices();
}

void UMicAudioCaptureComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 确保在组件销毁时停止捕获和断开连接
	StopCapture();
	DisconnectFromServer();

	// 清除计时器
	GetWorld()->GetTimerManager().ClearTimer(ReconnectTimerHandle);

	Super::EndPlay(EndPlayReason);
}

void UMicAudioCaptureComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// 在编辑器中显示音频电平指示器
	if (bShowAudioLevelInEditor && bIsCapturing)
	{
		float TimeSinceLastUpdate = GetWorld()->GetTimeSeconds() - LastAudioLevelUpdateTime;
		if (TimeSinceLastUpdate >= AudioLevelUpdateInterval)
		{
			// 绘制调试信息
			if (GEngine && GetOwner())
			{
				FString DebugText = FString::Printf(TEXT("Mic Level: %.2f"), CurrentAudioLevel);
				GEngine->AddOnScreenDebugMessage(-1, AudioLevelUpdateInterval, FColor::Green, DebugText);

				// 更新时间戳
				LastAudioLevelUpdateTime = GetWorld()->GetTimeSeconds();
			}
		}
	}
}

bool UMicAudioCaptureComponent::StartCapture(int32 DeviceIndex)
{
	// 如果已经在捕获，先停止
	if (bIsCapturing)
	{
		StopCapture();
	}

	// 检查设备索引是否有效
	if (AvailableMicDevices.Num() <= 0)
	{
		UE_LOG(LogMicAudioCapture, Error, TEXT("没有可用的麦克风设备"));
		return false;
	}

	DeviceIndex = FMath::Clamp(DeviceIndex, 0, AvailableMicDevices.Num() - 1);
	CurrentDeviceIndex = DeviceIndex;

	// 计算缓冲区大小
	int32 BufferSize = (SampleRate * NumChannels * sizeof(float) * BufferSizeMs) / 1000;

	// 初始化音频捕获参数
	FAudioCaptureDeviceParams CaptureParams;
	CaptureParams.DeviceIndex = DeviceIndex;
	CaptureParams.bUseHardwareAEC = false; // 如果需要回声消除，可以设置为true

	// 打开音频捕获设备
	if (!AudioCapture.OpenCaptureDevice(CaptureParams))
	{
		UE_LOG(LogMicAudioCapture, Error, TEXT("无法打开音频捕获设备: %s"), *AvailableMicDevices[DeviceIndex]);
		return false;
	}

	// 获取设备信息
	AudioCapture.GetCaptureDeviceInfo(AudioCaptureDeviceInfo);

	// 设置捕获参数
	AudioCapture.StartCapture([this](const float* AudioData, int32 NumFrames, int32 NumChannels, double StreamTime) {
		this->OnAudioCaptured(AudioData, NumFrames, NumChannels, StreamTime);
	}, SampleRate, NumChannels);

	bIsCapturing = true;

	// 发送事件通知
	OnMicCaptureStarted.Broadcast();

	if (bEnableDebugLogs)
	{
		UE_LOG(LogMicAudioCapture, Display, TEXT("麦克风捕获已开始: %s"), *AvailableMicDevices[DeviceIndex]);
		UE_LOG(LogMicAudioCapture, Display, TEXT("  采样率: %d"), SampleRate);
		UE_LOG(LogMicAudioCapture, Display, TEXT("  声道数: %d"), NumChannels);
		UE_LOG(LogMicAudioCapture, Display, TEXT("  缓冲区大小: %d ms"), BufferSizeMs);
	}

	return true;
}

void UMicAudioCaptureComponent::StopCapture()
{
	if (!bIsCapturing)
	{
		return;
	}

	AudioCapture.StopCapture();
	AudioCapture.CloseCaptureDevice();

	bIsCapturing = false;
	CurrentAudioLevel = 0.0f;

	// 发送事件通知
	OnMicCaptureStopped.Broadcast();

	if (bEnableDebugLogs)
	{
		UE_LOG(LogMicAudioCapture, Display, TEXT("麦克风捕获已停止"));
	}
}

void UMicAudioCaptureComponent::RefreshMicDevices()
{
	// 清空当前设备列表
	AvailableMicDevices.Empty();

	// 获取可用设备数量
	int32 NumDevices = FAudioCapture::GetCaptureDeviceCount();
	if (NumDevices <= 0)
	{
		UE_LOG(LogMicAudioCapture, Warning, TEXT("未检测到麦克风设备"));
		OnMicDevicesUpdated.Broadcast(AvailableMicDevices);
		return;
	}

	// 获取所有设备信息
	for (int32 DeviceIndex = 0; DeviceIndex < NumDevices; ++DeviceIndex)
	{
		FString DeviceName;
		FAudioCapture::GetCaptureDeviceInfo(DeviceIndex, DeviceName);
		AvailableMicDevices.Add(DeviceName);

		if (bEnableDebugLogs)
		{
			UE_LOG(LogMicAudioCapture, Display, TEXT("检测到麦克风设备 %d: %s"), DeviceIndex, *DeviceName);
		}
	}

	// 发送事件通知
	OnMicDevicesUpdated.Broadcast(AvailableMicDevices);
}

TArray<FString> UMicAudioCaptureComponent::GetAvailableMicDevices() const
{
	return AvailableMicDevices;
}

bool UMicAudioCaptureComponent::ConnectToServer(const FString& ServerUrl)
{
	// 检查是否已连接
	if (WebSocket.IsValid() && WebSocket->IsConnected())
	{
		// 如果已经连接到相同的URL，直接返回
		if (CurrentServerUrl == ServerUrl)
		{
			return true;
		}

		// 如果连接到不同的URL，先断开当前连接
		DisconnectFromServer();
	}

	// 保存当前服务器URL
	CurrentServerUrl = ServerUrl;
	CurrentReconnectAttempts = 0;

	// 创建WebSocket连接
	WebSocket = FWebSocketsModule::Get().CreateWebSocket(ServerUrl, TEXT(""));

	// 绑定WebSocket事件
	WebSocket->OnConnected().AddUObject(this, &UMicAudioCaptureComponent::OnWebSocketConnected);
	WebSocket->OnConnectionError().AddUObject(this, &UMicAudioCaptureComponent::OnWebSocketConnectionError);
	WebSocket->OnClosed().AddUObject(this, &UMicAudioCaptureComponent::OnWebSocketClosed);
	WebSocket->OnMessage().AddUObject(this, &UMicAudioCaptureComponent::OnWebSocketMessageReceived);

	// 连接到服务器
	WebSocket->Connect();

	if (bEnableDebugLogs)
	{
		UE_LOG(LogMicAudioCapture, Display, TEXT("正在连接到WebSocket服务器: %s"), *ServerUrl);
	}

	return true;
}

void UMicAudioCaptureComponent::DisconnectFromServer()
{
	// 清除重连计时器
	GetWorld()->GetTimerManager().ClearTimer(ReconnectTimerHandle);
	CurrentReconnectAttempts = 0;

	if (WebSocket.IsValid())
	{
		if (WebSocket->IsConnected())
		{
			WebSocket->Close();

			if (bEnableDebugLogs)
			{
				UE_LOG(LogMicAudioCapture, Display, TEXT("已断开与WebSocket服务器的连接: %s"), *CurrentServerUrl);
			}
		}

		// 解绑所有事件
		WebSocket->OnConnected().Clear();
		WebSocket->OnConnectionError().Clear();
		WebSocket->OnClosed().Clear();
		WebSocket->OnMessage().Clear();

		WebSocket = nullptr;
	}

	CurrentServerUrl.Empty();
}

bool UMicAudioCaptureComponent::IsConnectedToServer() const
{
	return WebSocket.IsValid() && WebSocket->IsConnected();
}

void UMicAudioCaptureComponent::OnAudioCaptured(const float* AudioData, int32 NumFrames, int32 NumChannels, double StreamTime)
{
	// 计算音频级别
	CurrentAudioLevel = CalculateAudioLevel(AudioData, NumFrames, NumChannels);

	// 在游戏线程上广播音频级别更新事件
	AsyncTask(ENamedThreads::GameThread, [this]() {
		OnAudioLevelUpdated.Broadcast(CurrentAudioLevel);
	});

	// 处理音频数据
	TArray<uint8> ProcessedData = ProcessAudioData(AudioData, NumFrames, NumChannels);

	// 如果WebSocket已连接，发送音频数据
	if (WebSocket.IsValid() && WebSocket->IsConnected() && ProcessedData.Num() > 0)
	{
		// 创建媒体包头
		FMediaPacketHeader Header;

		// 填充媒体包头
		uint32 Seq = 0; // 这里应该使用递增的序列号
		uint64 PtsUs = MSP_NowMicroseconds();
		uint16 StreamId = 0; // 使用默认流ID
		uint16 Flags = 0; // 无特殊标志

		// 分包发送，避免单个包过大
		int32 Offset = 0;
		while (Offset < ProcessedData.Num())
		{
			int32 ChunkSize = FMath::Min(ChunkSizeBytes, ProcessedData.Num() - Offset);

			// 填充媒体包头
			MSP_FillHeader(Header, EMediaPacketType::Audio, StreamId, Seq++, PtsUs, Flags, ChunkSize);

			// 创建发送数据缓冲区，包含头和负载
			TArray<uint8> SendData;
			SendData.SetNumUninitialized(sizeof(FMediaPacketHeader) + ChunkSize);

			// 复制头部
			FMemory::Memcpy(SendData.GetData(), &Header, sizeof(FMediaPacketHeader));

			// 复制负载
			FMemory::Memcpy(SendData.GetData() + sizeof(FMediaPacketHeader), ProcessedData.GetData() + Offset, ChunkSize);

			// 发送数据
			WebSocket->Send(SendData.GetData(), SendData.Num(), true);

			// 更新偏移
			Offset += ChunkSize;
		}
	}
}

float UMicAudioCaptureComponent::CalculateAudioLevel(const float* AudioData, int32 NumFrames, int32 NumChannels)
{
	if (!AudioData || NumFrames <= 0 || NumChannels <= 0)
	{
		return 0.0f;
	}

	// 计算RMS音量
	float SumSquared = 0.0f;
	int32 TotalSamples = NumFrames * NumChannels;

	for (int32 i = 0; i < TotalSamples; ++i)
	{
		float Sample = AudioData[i];
		SumSquared += Sample * Sample;
	}

	// 计算平均平方
	float MeanSquared = SumSquared / TotalSamples;

	// 计算RMS
	float Rms = FMath::Sqrt(MeanSquared);

	// 应用放大系数
	float Level = Rms * VolumeMultiplier;

	// 限制在0.0-1.0范围内
	return FMath::Clamp(Level, 0.0f, 1.0f);
}

TArray<uint8> UMicAudioCaptureComponent::ProcessAudioData(const float* AudioData, int32 NumFrames, int32 NumChannels)
{
	if (!AudioData || NumFrames <= 0 || NumChannels <= 0)
	{
		return TArray<uint8>();
	}

	// 创建PCM数据缓冲区(16位有符号整数格式)
	int32 BytesPerSample = 2; // 16-bit PCM = 2 bytes per sample
	int32 TotalBytes = NumFrames * NumChannels * BytesPerSample;
	TArray<uint8> PcmData;
	PcmData.SetNumUninitialized(TotalBytes);

	// 将浮点音频数据(范围为-1.0到1.0)转换为16位有符号整数(-32768到32767)
	int16* PcmSamples = reinterpret_cast<int16*>(PcmData.GetData());
	int32 TotalSamples = NumFrames * NumChannels;

	for (int32 i = 0; i < TotalSamples; ++i)
	{
		// 应用音量乘数
		float Sample = AudioData[i] * VolumeMultiplier;

		// 限制范围到[-1.0, 1.0]
		Sample = FMath::Clamp(Sample, -1.0f, 1.0f);

		// 转换为16位整数
		PcmSamples[i] = static_cast<int16>(Sample * 32767.0f);
	}

	return PcmData;
}

void UMicAudioCaptureComponent::TryReconnect()
{
	if (!WebSocket.IsValid() || WebSocket->IsConnected() || CurrentServerUrl.IsEmpty())
	{
		return;
	}

	if (CurrentReconnectAttempts < ReconnectAttempts)
	{
		CurrentReconnectAttempts++;

		if (bEnableDebugLogs)
		{
			UE_LOG(LogMicAudioCapture, Display, TEXT("尝试重新连接到服务器(%d/%d): %s"), 
				CurrentReconnectAttempts, ReconnectAttempts, *CurrentServerUrl);
		}

		// 创建新的WebSocket连接
		WebSocket = FWebSocketsModule::Get().CreateWebSocket(CurrentServerUrl, TEXT(""));

		// 重新绑定事件
		WebSocket->OnConnected().AddUObject(this, &UMicAudioCaptureComponent::OnWebSocketConnected);
		WebSocket->OnConnectionError().AddUObject(this, &UMicAudioCaptureComponent::OnWebSocketConnectionError);
		WebSocket->OnClosed().AddUObject(this, &UMicAudioCaptureComponent::OnWebSocketClosed);
		WebSocket->OnMessage().AddUObject(this, &UMicAudioCaptureComponent::OnWebSocketMessageReceived);

		// 连接到服务器
		WebSocket->Connect();
	}
	else
	{
		if (bEnableDebugLogs)
		{
			UE_LOG(LogMicAudioCapture, Warning, TEXT("重连失败，已达到最大重试次数: %d"), ReconnectAttempts);
		}

		// 清除计时器
		GetWorld()->GetTimerManager().ClearTimer(ReconnectTimerHandle);
		CurrentReconnectAttempts = 0;
	}
}

void UMicAudioCaptureComponent::OnWebSocketConnected()
{
	CurrentReconnectAttempts = 0;

	// 清除重连计时器
	GetWorld()->GetTimerManager().ClearTimer(ReconnectTimerHandle);

	if (bEnableDebugLogs)
	{
		UE_LOG(LogMicAudioCapture, Display, TEXT("已成功连接到WebSocket服务器: %s"), *CurrentServerUrl);
	}

	// 在游戏线程上广播连接成功事件
	AsyncTask(ENamedThreads::GameThread, [this]() {
		OnWebSocketConnected.Broadcast(CurrentServerUrl);
	});
}

void UMicAudioCaptureComponent::OnWebSocketConnectionError(const FString& Error)
{
	if (bEnableDebugLogs)
	{
		UE_LOG(LogMicAudioCapture, Error, TEXT("WebSocket连接错误: %s"), *Error);
	}

	// 在游戏线程上广播错误事件
	AsyncTask(ENamedThreads::GameThread, [this, Error]() {
		OnWebSocketError.Broadcast(Error, -1);
	});

	// 如果启用了重连机制，设置重连计时器
	if (ReconnectAttempts > 0)
	{
		GetWorld()->GetTimerManager().SetTimer(
			ReconnectTimerHandle, 
			this, 
			&UMicAudioCaptureComponent::TryReconnect, 
			ReconnectInterval, 
			false);
	}
}

void UMicAudioCaptureComponent::OnWebSocketClosed(int32 StatusCode, const FString& Reason, bool bWasClean)
{
	if (bEnableDebugLogs)
	{
		if (bWasClean)
		{
			UE_LOG(LogMicAudioCapture, Display, TEXT("WebSocket连接已正常关闭: %d %s"), StatusCode, *Reason);
		}
		else
		{
			UE_LOG(LogMicAudioCapture, Warning, TEXT("WebSocket连接异常关闭: %d %s"), StatusCode, *Reason);
		}
	}

	// 如果不是正常关闭且启用了重连机制，尝试重连
	if (!bWasClean && ReconnectAttempts > 0)
	{
		GetWorld()->GetTimerManager().SetTimer(
			ReconnectTimerHandle, 
			this, 
			&UMicAudioCaptureComponent::TryReconnect, 
			ReconnectInterval, 
			false);
	}
}

void UMicAudioCaptureComponent::OnWebSocketMessageReceived(const FString& Message)
{
	// 可以处理服务器响应
	if (bEnableDebugLogs)
	{
		UE_LOG(LogMicAudioCapture, Verbose, TEXT("收到WebSocket消息: %s"), *Message);
	}
}
