#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
// 使用 NetworkCorePlugin 的类型
#include "NivaNetworkCoreSettings.h"
#include "NetworkCoreSubsystem.h"
#include "HAL/CriticalSection.h"
#include "AudioStreamHttpWsSubsystem.generated.h"

class UAudioStreamHttpWsComponent; // 前置声明，避免循环包含

/**
 * 极简：HTTP 端点接收任务信息 -> 连接 WebSocket -> 接收二进制音频并播放
 * 假设音频为 PCM S16LE（默认 16000Hz, 1ch），POST JSON 可覆盖：
 * { "ws_url": "wss://...", "sample_rate": 16000, "channels": 1 }
 */
UCLASS()
class CUSTOMINPUTCONTROLLER_API UAudioStreamHttpWsSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // 启动统一HTTP监听（端口由NetworkCorePlugin设置）。
    UFUNCTION(BlueprintCallable, Category="AudioStream")
    bool StartHttpListener(int32 Port = 0);

    UFUNCTION(BlueprintCallable, Category="AudioStream")
    void StopHttpListener();

    // 组件注册/注销（ BeginPlay/EndPlay 时调用 ）
    bool RegisterComponent(UAudioStreamHttpWsComponent* Comp, FString& OutKey, const FString& PreferredKey = TEXT(""));
    void UnregisterComponent(UAudioStreamHttpWsComponent* Comp);

    // 手动停止所有连接/播放（可选）
    UFUNCTION(BlueprintCallable, Category="AudioStream")
    void StopStreaming();

    // 统计：重置与获取
    UFUNCTION(BlueprintCallable, Category="AudioStream|Stats")
    void ResetAudioStats();

    UFUNCTION(BlueprintCallable, Category="AudioStream|Stats")
    void GetAudioStats(int64& OutTotalBytes, int64& OutTotalFrames, double& OutTotalSeconds) const;

    // 扩展：包含 viseme 统计
    UFUNCTION(BlueprintCallable, Category="AudioStream|Stats")
    void GetAudioStatsEx(int64& OutTotalBytes, int64& OutTotalFrames, double& OutTotalSeconds, int64& OutTotalVisemes) const;

    // 直接打印到日志，便于查看“最终数据”
    UFUNCTION(BlueprintCallable, Category="AudioStream|Stats")
    void PrintAudioStatsToLog(const FString& Reason = TEXT("Manual"));

    // 开关：启用“每次接收后打印当前统计”（默认关闭，使用 Verbose）
    UFUNCTION(BlueprintCallable, Category="AudioStream|Stats")
    void SetStatsLiveLog(bool bEnable);

    // ============== 测试节点相关接口 ==============
    
    // 启动模拟WebSocket测试推流
    UFUNCTION(BlueprintCallable, Category="AudioStream|Test", meta=(CallInEditor="true"))
    bool StartTestStream(const FString& TargetKey, int32 SampleRate = 16000, int32 Channels = 1, float FrequencyHz = 440.0f, float DurationSeconds = 5.0f);
    
    // 停止测试推流
    UFUNCTION(BlueprintCallable, Category="AudioStream|Test", meta=(CallInEditor="true"))
    void StopTestStream();
    
    // 推送一段测试音频数据（正弦波）
    UFUNCTION(BlueprintCallable, Category="AudioStream|Test")
    void PushTestAudioChunk(const FString& TargetKey, int32 SampleRate = 16000, int32 Channels = 1, float FrequencyHz = 440.0f, float ChunkDurationMs = 100.0f);
    
    // 推送测试文本
    UFUNCTION(BlueprintCallable, Category="AudioStream|Test")
    void PushTestText(const FString& TargetKey, const FString& Text);
    
    // 推送测试Viseme数据
    UFUNCTION(BlueprintCallable, Category="AudioStream|Test")
    void PushTestViseme(const FString& TargetKey, const TArray<int32>& VisemeIndices);
    
    // 生成正弦波测试音频数据
    UFUNCTION(BlueprintCallable, Category="AudioStream|Test")
    TArray<uint8> GenerateTestSineWave(int32 SampleRate, int32 Channels, float FrequencyHz, float DurationSeconds);

private:
    bool bHttpStarted = false;

    // key -> Component 弱引用表
    TMap<FString, TWeakObjectPtr<UAudioStreamHttpWsComponent>> ComponentMap;
    
    // ============== 测试节点相关私有成员 ==============
    
    // 测试推流状态
    bool bTestStreamActive = false;
    FString TestTargetKey;
    int32 TestSampleRate = 16000;
    int32 TestChannels = 1;
    float TestFrequency = 440.0f;
    float TestDuration = 5.0f;
    float TestCurrentTime = 0.0f;
    
    // 测试推流定时器
    FTimerHandle TestStreamTimer;
    
    // 测试推流回调
    void TestStreamTick();

    // 路由处理：接收base64分段并转发到组件
    UFUNCTION()
    FNivaHttpResponse HandleAudioPush_NCP(FNivaHttpRequest Request);

    // 复用：如仍需通过HTTP启动WS，可保留现有接口
    UFUNCTION()
    FNivaHttpResponse HandleTaskStart_NCP(FNivaHttpRequest Request);

    // 新增：统计查询路由
    UFUNCTION()
    FNivaHttpResponse HandleAudioStats_NCP(FNivaHttpRequest Request);

    // WebSocket 状态（面向单路任务）
    TSharedPtr<class IWebSocket> WebSocket;
    FString ActiveWsTargetKey;
    int32 ActiveWsSampleRate = 16000;
    int32 ActiveWsChannels = 1;

    // 新增：二进制分片缓冲
    TArray<uint8> WsBinaryBuffer;

    void ConnectWebSocket(const FString& Url);
    void CloseWebSocket();

    // 统计：线程安全累加
    void UpdateStats(int32 PcmBytes, int32 SampleRate, int32 Channels);
    void UpdateVisemeStats(int32 Count);

    void LogFinalStats(const TCHAR* Reason) const;
    void LogCurrentStats(const TCHAR* Reason) const;

    mutable FCriticalSection StatsCS;
    int64 TotalPcmBytes = 0;   // 已接收并通过对齐的PCM字节数
    int64 TotalFrames   = 0;   // 按帧（一次包含所有声道的样本组）计数
    double TotalSeconds = 0.0; // 直接累加每次 push 的秒数（兼容不同采样率）
    int64 TotalVisemes  = 0;   // viseme 总数

    bool bStatsLiveLog = true; // 打开后，将使用 Log 级别打印每次接收的当前统计
};
