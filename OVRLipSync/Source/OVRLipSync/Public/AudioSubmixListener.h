#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "AudioDevice.h"
#include "Sound/SoundSubmix.h"
#include "Engine/Engine.h"
#include "ISubmixBufferListener.h"
#include "AudioSubmixListener.generated.h"

// 修改委托参数为uint8数组，直接匹配FeedAudio需要的格式
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAudioDataCaptured, const TArray<uint8>&, AudioData);

// 创建一个纯接口实现类，不继承UObject
class FSubmixBufferListenerImpl : public ISubmixBufferListener
{
public:
    FSubmixBufferListenerImpl(class UAudioSubmixListener* InOwner);
    virtual ~FSubmixBufferListenerImpl() = default;

    // ISubmixBufferListener interface
    virtual void OnNewSubmixBuffer(const USoundSubmix* OwningSubmix, float* AudioData, int32 NumSamples, int32 NumChannels, const int32 SampleRate, double AudioClock) override;
    virtual const FString& GetListenerName() const override { return ListenerName; }

private:
    class UAudioSubmixListener* Owner;
    FString ListenerName = TEXT("AudioSubmixListener");
};

UCLASS(BlueprintType, Blueprintable)
class OVRLIPSYNC_API UAudioSubmixListener : public UObject
{
    GENERATED_BODY()

public:
    UAudioSubmixListener();

    // 开始监听音频子混音
    UFUNCTION(BlueprintCallable, Category = "Audio")
    void StartListening(USoundSubmix* Submix);

    // 停止监听音频子混音
    UFUNCTION(BlueprintCallable, Category = "Audio")
    void StopListening();

    // 音频数据捕获事件 - 输出uint8格式以匹配FeedAudio
    UPROPERTY(BlueprintAssignable)
    FOnAudioDataCaptured OnAudioDataCaptured;

    // 目标采样率
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio Settings")
    int32 TargetSampleRate = 48000;

    // 处理音频数据的回调函数
    void HandleAudioData(const USoundSubmix* OwningSubmix, float* AudioData, int32 NumSamples, int32 NumChannels, const int32 SampleRate, double AudioClock);

protected:
    virtual void BeginDestroy() override;

private:
    // 重采样音频数据 - 修改为输出uint8格式（16-bit PCM打包为字节数组）
    void ResampleAudio(const float* InputData, int32 InputSamples, int32 InputSampleRate, TArray<uint8>& OutputData);

    // 转换为单声道
    void ConvertToMono(const float* InputData, int32 NumSamples, int32 NumChannels, TArray<float>& OutputData);

    // 当前监听的子混音
    UPROPERTY()
    USoundSubmix* CurrentSubmix;

    // 共享引用的监听器实现
    TSharedPtr<FSubmixBufferListenerImpl> ListenerImpl;
};