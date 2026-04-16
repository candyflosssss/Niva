// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Input/InputPlusSubsystem.h"
#include "HandDataListenerComponent.generated.h"

// 保留旧版本兼容性委托
DECLARE_DYNAMIC_MULTICAST_SPARSE_DELEGATE_TwoParams(FOnHands, UHandDataListenerComponent, OnHands, const TArray<FString>&, Keys, const TArray<FVector>&, Values);

// 新的双手数据委托
DECLARE_DYNAMIC_MULTICAST_SPARSE_DELEGATE_FourParams(FOnBothHands, UHandDataListenerComponent, OnBothHands, 
    const TArray<FString>&, LeftHandKeys, const TArray<FVector>&, LeftHandValues,
    const TArray<FString>&, RightHandKeys, const TArray<FVector>&, RightHandValues);

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class CUSTOMINPUTCONTROLLER_API UHandDataListenerComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UHandDataListenerComponent();

    /**
     * 单手数据接收委托（保留兼容性）- 只会接收到最后处理的手部数据
     */
    UPROPERTY(BlueprintAssignable, Category = "Hand Data")
    FOnHands OnHands;

    /**
     * 双手数据接收委托 - 同时接收左手和右手数据
     */
    UPROPERTY(BlueprintAssignable, Category = "Hand Data")
    FOnBothHands OnBothHands;

    /**
     * 获取最新的双手数据
     */
    UFUNCTION(BlueprintCallable, Category = "Hand Data")
    void GetLatestHandData(FHandLandmarkData& OutLeftHand, FHandLandmarkData& OutRightHand);

    UPROPERTY(BlueprintReadWrite, Category = "Hand Data")
    TMap<int32, FRotator> LeftRotRelativeMap;
    UPROPERTY(BlueprintReadWrite, Category = "Hand Data")
    TMap<int32, FRotator> RightRotRelativeMap;
    
    UPROPERTY(BlueprintReadWrite, Category = "Hand Data")
    TMap<int32, FRotator> LeftRotWorldMap;
    UPROPERTY(BlueprintReadWrite, Category = "Hand Data")
    TMap<int32, FRotator> RightRotWorldMap;

    // --- 平滑（缓动）与垃圾数据过滤设置 ---
    /** 是否启用位置平滑（指数平滑） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Data|Smoothing")
    bool bEnableSmoothing = true;

    /** 平滑因子 alpha，范围 (0,1]，越小越平滑，默认 0.5 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.0", ClampMax="1.0"), Category = "Hand Data|Smoothing")
    float SmoothingAlpha = 0.5f;

    /** 是否启用垃圾数据过滤（离群点抑制） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Data|Filtering")
    bool bEnableOutlierFilter = true;

    /**
     * 离群跳变阈值的尺度系数，最终以手掌参考尺寸（如 0->5/9/13/17 的均值）乘以该系数作为每点允许的最大位移。
     * 典型范围 [0.5, 3]，默认 1.5。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.1", ClampMax="10.0"), Category = "Hand Data|Filtering")
    float OutlierJumpScale = 1.5f;

    /** 当超过该比例的点被判定为离群时，整帧丢弃（保持上一帧结果） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.0", ClampMax="1.0"), Category = "Hand Data|Filtering")
    float DropFrameBadPointRatio = 0.6f;

    /** 是否在离群过多时丢弃整帧 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Data|Filtering")
    bool bDropFrameOnTooManyOutliers = true;

    /** 预热：前 N 帧不过滤（或尽量放宽过滤），避免初始被误杀 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0"), Category = "Hand Data|Filtering")
    int32 WarmupAcceptedFrames = 2;

    /** 丢帧自适应：阈值随帧间隔增大而放宽 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Data|Filtering")
    bool bAdaptiveJumpThreshold = true;

    /** 目标帧率，用于自适应阈值（例如 30 或 60） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="1.0", ClampMax="240.0"), Category = "Hand Data|Filtering")
    float TargetFrameRate = 30.0f;

    /** 自适应阈值的最大放大量（倍数上限） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="1.0", ClampMax="20.0"), Category = "Hand Data|Filtering")
    float MaxAdaptiveThresholdScale = 4.0f;

    /** 速度限幅：限制每帧最大位移（按手掌尺寸比例/秒） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Data|Filtering")
    bool bVelocityClampEnabled = true;

    /** 每秒最大位移（以手掌参考尺寸为单位的倍数/秒），例如 12 表示每秒可移动 12 倍手掌尺寸 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.0", ClampMax="100.0"), Category = "Hand Data|Filtering")
    float MaxSpeedScalePerSecond = 12.0f;

    /** 重获跟踪时将当前帧作为基线（不做跳变判定） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Data|Filtering")
    bool bTreatReacquireAsBaseline = true;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    /**
     * 缓存的 InputPlusSubsystem 引用
     */
    UPROPERTY()
    UInputPlusSubsystem* InputPlusSubsystem;
    
    /**
     * 处理从 InputPlusSubsystem 接收到的双手数据
     */
    UFUNCTION()
    void OnBothHandsDataReceived(const TArray<FString>& LeftHandKeys, const TArray<FVector>& LeftHandValues,
                               const TArray<FString>& RightHandKeys, const TArray<FVector>& RightHandValues);

    /**
     * 绑定到 InputPlusSubsystem 的委托
     */
    void BindToInputPlusSubsystem();

    /**
     * 从 InputPlusSubsystem 解绑委托
     */
    void UnbindFromInputPlusSubsystem();

    // --- 手部运动学工具 ---
    UFUNCTION(BlueprintCallable, Category="HandKinematics")
    static void getHandDir(
    const TArray<FVector>& Points21,
    FVector& Forward,
    FVector& Right,
    FVector& Up,
    bool bIsLeft = true
    );

    UFUNCTION(BlueprintCallable, Category="HandKinematics")
    void TransformToRotMap(
    const TArray<FVector>& Points21,
    UPARAM(ref) TMap<int32, FRotator>& OutRotRelativeMap,
    UPARAM(ref) TMap<int32, FRotator>& OutRotParentMap,
    bool isLeft
    );

    // --- 平滑与过滤内部缓存 ---
    /** 上一帧左手 21 点 */
    TArray<FVector> PrevLeftPoints;
    /** 上一帧右手 21 点 */
    TArray<FVector> PrevRightPoints;
    /** 上一帧左、右手掌参考尺寸 */
    float PrevLeftPalmSize = 0.f;
    float PrevRightPalmSize = 0.f;

    /** 计算手掌参考尺寸（用于自适应阈值） */
    static float ComputePalmReferenceSize(const TArray<FVector>& Points21);

    /** 对输入的 21 点应用平滑与离群过滤；返回是否使用了新数据（未整体丢帧） */
    bool SmoothAndFilterOneHand(const TArray<FString>& Keys,
                                const TArray<FVector>& InValues,
                                TArray<FVector>& InOutPrevPoints,
                                float& InOutPrevPalmSize,
                                TArray<FVector>& OutValues);

    /** 增强版：加入时间/预热/自适应阈值/速度限幅；bIsLeft 仅供将来差异化使用 */
    bool SmoothAndFilterOneHandEx(const TArray<FString>& Keys,
                                  const TArray<FVector>& InValues,
                                  TArray<FVector>& InOutPrevPoints,
                                  float& InOutPrevPalmSize,
                                  bool bIsLeft,
                                  float DeltaTime,
                                  bool bJustReacquired,
                                  int32 AcceptedCount,
                                  TArray<FVector>& OutValues);

    // 每只手的运行时状态
    int32 LeftAcceptedFrames = 0;
    int32 RightAcceptedFrames = 0;
    double LastLeftTimeSec = 0.0;
    double LastRightTimeSec = 0.0;
    bool bPrevLeftHadRawData = false;
    bool bPrevRightHadRawData = false;
};

