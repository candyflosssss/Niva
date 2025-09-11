#include "AudioSubmixListener.h"
#include "Engine/Engine.h"
#include "AudioDevice.h"
#include "Sound/SoundSubmix.h"
#include "ISubmixBufferListener.h"
#include "HAL/PlatformFilemanager.h"

FSubmixBufferListenerImpl::FSubmixBufferListenerImpl(UAudioSubmixListener* InOwner)
    : Owner(InOwner)
{
}

void FSubmixBufferListenerImpl::OnNewSubmixBuffer(const USoundSubmix* OwningSubmix, float* AudioData, int32 NumSamples, int32 NumChannels, const int32 SampleRate, double AudioClock)
{
    if (IsValid(Owner))
    {
        Owner->HandleAudioData(OwningSubmix, AudioData, NumSamples, NumChannels, SampleRate, AudioClock);
    }
}

UAudioSubmixListener::UAudioSubmixListener()
{
    CurrentSubmix = nullptr;
    ListenerImpl = MakeShared<FSubmixBufferListenerImpl>(this);
}

// 在StartListening之前添加调试信息
void UAudioSubmixListener::StartListening(USoundSubmix* Submix)
{
    if (!Submix)
    {
        UE_LOG(LogTemp, Warning, TEXT("Invalid submix provided"));
        return;
    }
    
    // 添加submix信息日志
    UE_LOG(LogTemp, Warning, TEXT("Starting to listen to submix: %s"), *Submix->GetName());
    
    if (CurrentSubmix)
    {
        StopListening();
    }
    
    CurrentSubmix = Submix;
    
    // 确保ListenerImpl已创建
    if (!ListenerImpl.IsValid())
    {
        ListenerImpl = MakeShared<FSubmixBufferListenerImpl>(this);
    }
    
    // 获取AudioDevice并使用新的API注册监听器
    FAudioDeviceHandle AudioDeviceHandle = GEngine->GetMainAudioDevice();
    if (FAudioDevice* AudioDevice = AudioDeviceHandle.GetAudioDevice())
    {
    	// 使用新的API，需要TSharedRef和显式的submix引用
    	TSharedRef<ISubmixBufferListener, ESPMode::ThreadSafe> ListenerRef = ListenerImpl.ToSharedRef();
    	AudioDevice->RegisterSubmixBufferListener(ListenerRef, *CurrentSubmix);
    	UE_LOG(LogTemp, Log, TEXT("Started listening to submix: %s"), *Submix->GetName());
    }
}

void UAudioSubmixListener::StopListening()
{
    if (CurrentSubmix && ListenerImpl.IsValid())
    {
        // 获取AudioDevice并使用新的API取消注册监听器
        FAudioDeviceHandle AudioDeviceHandle = GEngine->GetMainAudioDevice();
        if (FAudioDevice* AudioDevice = AudioDeviceHandle.GetAudioDevice())
        {
        	// 使用新的API取消注册
        	TSharedRef<ISubmixBufferListener, ESPMode::ThreadSafe> ListenerRef = ListenerImpl.ToSharedRef();
        	AudioDevice->UnregisterSubmixBufferListener(ListenerRef, *CurrentSubmix);
        }
        CurrentSubmix = nullptr;
        UE_LOG(LogTemp, Log, TEXT("Stopped listening to submix"));
    }
}

void UAudioSubmixListener::BeginDestroy()
{
    StopListening();
    Super::BeginDestroy();
}

void UAudioSubmixListener::HandleAudioData(const USoundSubmix* OwningSubmix, float* AudioData, int32 NumSamples, int32 NumChannels, const int32 SampleRate, double AudioClock)
{
    if (!AudioData || NumSamples <= 0)
    {
        return;
    }

    // 分析原始音频数据
    float MinValue = FLT_MAX;
    float MaxValue = -FLT_MAX;
    float RMSSum = 0.0f;
    int32 NonZeroSamples = 0;
    
    for (int32 i = 0; i < NumSamples; ++i)
    {
        float Sample = AudioData[i];
        MinValue = FMath::Min(MinValue, Sample);
        MaxValue = FMath::Max(MaxValue, Sample);
        RMSSum += Sample * Sample;
        
        if (FMath::Abs(Sample) > 0.001f) // 考虑浮点精度
        {
            NonZeroSamples++;
        }
    }
    
    float RMS = FMath::Sqrt(RMSSum / NumSamples);
    float NonZeroPercentage = (float)NonZeroSamples / NumSamples * 100.0f;
    
    UE_LOG(LogTemp, Warning, TEXT("Raw Audio Analysis:"));
    UE_LOG(LogTemp, Warning, TEXT("  Samples: %d, Channels: %d, SampleRate: %d Hz"), 
           NumSamples, NumChannels, SampleRate);
    UE_LOG(LogTemp, Warning, TEXT("  Range: [%.6f, %.6f], RMS: %.6f"), 
           MinValue, MaxValue, RMS);
    UE_LOG(LogTemp, Warning, TEXT("  Non-zero samples: %d (%.1f%%)"), 
           NonZeroSamples, NonZeroPercentage);

    // 只处理有意义的音频数据
    if (NonZeroSamples == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("Skipping silent audio data"));
        return;
    }

    // 转换为单声道
    TArray<float> MonoData;
    ConvertToMono(AudioData, NumSamples, NumChannels, MonoData);

    // 验证单声道转换结果
    float MonoRMS = 0.0f;
    for (float Sample : MonoData)
    {
        MonoRMS += Sample * Sample;
    }
    MonoRMS = FMath::Sqrt(MonoRMS / MonoData.Num());
    UE_LOG(LogTemp, Warning, TEXT("  Mono RMS: %.6f"), MonoRMS);

    // 重采样并转换为uint8格式
    TArray<uint8> OutputData;
    ResampleAudio(MonoData.GetData(), MonoData.Num(), SampleRate, OutputData);

    // 验证最终输出数据
    if (OutputData.Num() > 0)
    {
        // 检查前几个样本的值
        UE_LOG(LogTemp, Warning, TEXT("First few output bytes: %d, %d, %d, %d"), 
               OutputData.Num() > 0 ? OutputData[0] : 0,
               OutputData.Num() > 1 ? OutputData[1] : 0,
               OutputData.Num() > 2 ? OutputData[2] : 0,
               OutputData.Num() > 3 ? OutputData[3] : 0);
        
        OnAudioDataCaptured.Broadcast(OutputData);
    }
}


void UAudioSubmixListener::ConvertToMono(const float* InputData, int32 NumSamples, int32 NumChannels, TArray<float>& OutputData)
{
    if (NumChannels == 1)
    {
        // 已经是单声道
        OutputData.SetNum(NumSamples);
        FMemory::Memcpy(OutputData.GetData(), InputData, NumSamples * sizeof(float));
    }
    else
    {
        // 多声道转单声道：取平均值
        int32 MonoSamples = NumSamples / NumChannels;
        OutputData.SetNum(MonoSamples);

        for (int32 i = 0; i < MonoSamples; ++i)
        {
            float Sum = 0.0f;
            for (int32 Channel = 0; Channel < NumChannels; ++Channel)
            {
                Sum += InputData[i * NumChannels + Channel];
            }
            OutputData[i] = Sum / NumChannels;
        }
    }
}

void UAudioSubmixListener::ResampleAudio(const float* InputData, int32 InputSamples, int32 InputSampleRate, TArray<uint8>& OutputData)
{
	// 建议：如果输入采样率是48000，直接使用不要重采样
	if (InputSampleRate == 48000)
	{
		// 直接使用48000Hz，不进行重采样
		TArray<int16> PCMData;
		PCMData.SetNum(InputSamples);
        
		for (int32 i = 0; i < InputSamples; ++i)
		{
			float ClampedValue = FMath::Clamp(InputData[i], -1.0f, 1.0f);
			PCMData[i] = (int16)(ClampedValue * 32767.0f);
		}
        
		// 转换为字节数组
		OutputData.SetNum(PCMData.Num() * 2);
		for (int32 i = 0; i < PCMData.Num(); ++i)
		{
			int16 Sample = PCMData[i];
			OutputData[i * 2] = (uint8)(Sample & 0xFF);
			OutputData[i * 2 + 1] = (uint8)((Sample >> 8) & 0xFF);
		}
        
		UE_LOG(LogTemp, Log, TEXT("Audio: %d samples at %dHz -> %d bytes"), 
			   InputSamples, InputSampleRate, OutputData.Num());
		return;
	}

    // 检查输入数据范围
    float MinVal = FLT_MAX, MaxVal = -FLT_MAX;
    for (int32 i = 0; i < InputSamples; ++i)
    {
        MinVal = FMath::Min(MinVal, InputData[i]);
        MaxVal = FMath::Max(MaxVal, InputData[i]);
    }
    UE_LOG(LogTemp, VeryVerbose, TEXT("Input audio range: %.6f to %.6f"), MinVal, MaxVal);

    // 先转换为16-bit PCM格式
    TArray<int16> PCMData;
    
    if (InputSampleRate == TargetSampleRate)
    {
        // 不需要重采样，直接转换格式
        PCMData.SetNum(InputSamples);
        for (int32 i = 0; i < InputSamples; ++i)
        {
            // 将浮点数 [-1.0, 1.0] 转换为 int16 [-32768, 32767]
            float ClampedValue = FMath::Clamp(InputData[i], -1.0f, 1.0f);
            PCMData[i] = (int16)(ClampedValue * 32767.0f);
        }
        UE_LOG(LogTemp, VeryVerbose, TEXT("No resampling needed"));
    }
    else
    {
        // 需要重采样
        float ResampleRatio = (float)TargetSampleRate / InputSampleRate;
        int32 OutputSamples = FMath::RoundToInt(InputSamples * ResampleRatio);
        PCMData.SetNum(OutputSamples);

        UE_LOG(LogTemp, Warning, TEXT("Resampling: %d Hz -> %d Hz, %d -> %d samples (ratio: %.3f)"), 
               InputSampleRate, TargetSampleRate, InputSamples, OutputSamples, ResampleRatio);

        // 简单的线性插值重采样
        for (int32 i = 0; i < OutputSamples; ++i)
        {
            float InputIndex = i / ResampleRatio;
            int32 Index0 = FMath::FloorToInt(InputIndex);
            int32 Index1 = FMath::Min(Index0 + 1, InputSamples - 1);
            
            float Fraction = InputIndex - Index0;
            float InterpolatedValue = FMath::Lerp(InputData[Index0], InputData[Index1], Fraction);
            
            // 将浮点数 [-1.0, 1.0] 转换为 int16 [-32768, 32767]
            float ClampedValue = FMath::Clamp(InterpolatedValue, -1.0f, 1.0f);
            PCMData[i] = (int16)(ClampedValue * 32767.0f);
        }
    }

    // 将16-bit PCM数据打包为uint8字节数组 (Little-endian)
    OutputData.SetNum(PCMData.Num() * 2); // 每个int16需要2个字节
    
    for (int32 i = 0; i < PCMData.Num(); ++i)
    {
        int16 Sample = PCMData[i];
        OutputData[i * 2] = (uint8)(Sample & 0xFF);         // 低字节
        OutputData[i * 2 + 1] = (uint8)((Sample >> 8) & 0xFF); // 高字节
    }

    // 验证转换后的前几个样本
    if (PCMData.Num() > 0)
    {
        UE_LOG(LogTemp, VeryVerbose, TEXT("First PCM samples: %d, %d, %d"), 
               PCMData.Num() > 0 ? PCMData[0] : 0,
               PCMData.Num() > 1 ? PCMData[1] : 0,
               PCMData.Num() > 2 ? PCMData[2] : 0);
    }
}