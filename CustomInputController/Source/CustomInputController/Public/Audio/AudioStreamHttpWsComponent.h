#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AudioStreamHttpWsComponent.generated.h"

class UAudioStreamHttpWsSubsystem;

/**
 * 组件版：每个挂载它的角色都可独立播放自己的流式音频。
 * 默认使用 PCM S16LE，16kHz，单声道；POST可覆盖。
 * POST JSON: { "ws_url": "wss://...", "sample_rate": 16000, "channels": 1 }
 */
UCLASS(ClassGroup=(Audio), meta=(BlueprintSpawnableComponent))
class CUSTOMINPUTCONTROLLER_API UAudioStreamHttpWsComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UAudioStreamHttpWsComponent();

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    // 可选：预期注册用Key（为空则由子系统分配或生成）
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AudioStream|Routing")
    FString PreferredKey;

    // 注册后由子系统分配并保持的唯一识别码（UUID）
    UPROPERTY(BlueprintReadOnly, Category="AudioStream|Routing")
    FString RegisteredUuid;

    // 查询此组件的注册 UUID
    UFUNCTION(BlueprintPure, Category="AudioStream|Routing")
    FString GetComponentUuid() const { return RegisteredUuid; }

private:
    // 注册态
    bool bRegistered = false;

    // 便捷：注册/注销到子系统
    void RegisterToSubsystem();
    void UnregisterFromSubsystem();
};