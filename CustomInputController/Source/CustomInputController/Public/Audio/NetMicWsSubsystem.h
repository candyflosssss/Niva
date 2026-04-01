#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "NetMicWsSubsystem.generated.h"

class UNetMicWsComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnNetMicAudioBinary, const TArray<uint8>&, Data);

// 兼容门面：保留历史 Subsystem API，但实时 WebSocket 连接已收敛到 UNetMicWsComponent。
// 该子系统仅负责：
// - 兼容旧蓝图调用（转发到已注册的组件）
// - HTTP POST 获取 wsUrl 后委托组件连接
// - 维护一个被动音频环形缓存，供旧查询接口继续使用
UCLASS()
class CUSTOMINPUTCONTROLLER_API UNetMicWsSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// 连接：通过 HTTP POST 获取 wsUrl 后再委托已注册组件连接；每次调用都会重置暂存区
	UFUNCTION(BlueprintCallable, Category="NetMic", meta=(DeprecatedFunction, DeprecationMessage="UNetMicWsSubsystem 已降级为兼容层。请优先使用 UNetMicWsComponent 持有实时连接。"))
	void StartByPost(const FString& HttpUrl, const FString& JsonBody = TEXT("{}"));

	// 直接连接指定 WebSocket（有时外部已拿到 wsUrl）；每次调用都会重置暂存区
	UFUNCTION(BlueprintCallable, Category="NetMic", meta=(DeprecatedFunction, DeprecationMessage="UNetMicWsSubsystem 已降级为兼容层。请优先使用 UNetMicWsComponent 持有实时连接。"))
	void StartDirect(const FString& WsUrl);

	// 关闭麦克风（断开 WebSocket 并清空暂存）
	UFUNCTION(BlueprintCallable, Category="NetMic", meta=(DeprecatedFunction, DeprecationMessage="UNetMicWsSubsystem 已降级为兼容层。请优先使用 UNetMicWsComponent 持有实时连接。"))
	void StopMic();

	// 暂存上限（秒），运行时可调整
	UFUNCTION(BlueprintCallable, Category="NetMic")
	void SetMaxBufferSeconds(float InSeconds) { MaxBufferSeconds = FMath::Max(0.f, InSeconds); }

	UFUNCTION(BlueprintCallable, Category="NetMic")
	float GetMaxBufferSeconds() const { return MaxBufferSeconds; }

	UFUNCTION(BlueprintCallable, Category="NetMic")
	float GetBufferedSeconds() const;

	// 转发开关与目标（占位，暂不实际发送）
	UFUNCTION(BlueprintCallable, Category="NetMic|Forward")
	void EnableForward(bool bEnable) { bForwardEnabled = bEnable; }

	UFUNCTION(BlueprintCallable, Category="NetMic|Forward")
	void SetForwardTargets(const TArray<FString>& Targets) { ForwardTargets = Targets; }

	// 最新一帧到达事件（蓝图）
	UPROPERTY(BlueprintAssignable, Category="NetMic")
	FOnNetMicAudioBinary OnAudioBinary;

private:
	void ResetBuffer();
	void WarnCompatibilityUse(const TCHAR* FunctionName) const;
	UNetMicWsComponent* GetCompatibilityComponent() const;

public:
	void RegisterCompatibilityComponent(UNetMicWsComponent* InComponent);
	void UnregisterCompatibilityComponent(UNetMicWsComponent* InComponent);
	void MirrorAudioFrame(const TArray<uint8>& Data);

private:
	TWeakObjectPtr<UNetMicWsComponent> CompatibilityComponent;
	struct FPacket { double TimeSec = 0.0; TArray<uint8> Bytes; };
	mutable FCriticalSection BufferCS;
	TArray<FPacket> Ring;
	float MaxBufferSeconds = 15.f; // 默认15秒
	bool bForwardEnabled = false;
	TArray<FString> ForwardTargets;
};
