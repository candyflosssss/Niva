// Fill out your copyright notice in the Description page of Project Settings.

#include "HandTracking/HandDataListenerComponent.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Async/Async.h"

UHandDataListenerComponent::UHandDataListenerComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    InputPlusSubsystem = nullptr;
}

void UHandDataListenerComponent::BeginPlay()
{
    Super::BeginPlay();
    
    BindToInputPlusSubsystem();
}

void UHandDataListenerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    UnbindFromInputPlusSubsystem();

    // 清理平滑与过滤缓存
    PrevLeftPoints.Empty();
    PrevRightPoints.Empty();
    PrevLeftPalmSize = 0.f;
    PrevRightPalmSize = 0.f;
    LeftAcceptedFrames = 0;
    RightAcceptedFrames = 0;
    LastLeftTimeSec = 0.0;
    LastRightTimeSec = 0.0;
    bPrevLeftHadRawData = false;
    bPrevRightHadRawData = false;
    
    Super::EndPlay(EndPlayReason);
}

void UHandDataListenerComponent::BindToInputPlusSubsystem()
{
    // 获取 GameInstance 和 InputPlusSubsystem
    if (UWorld* World = GetWorld())
    {
        if (UGameInstance* GameInstance = World->GetGameInstance())
        {
            InputPlusSubsystem = GameInstance->GetSubsystem<UInputPlusSubsystem>();
            
            if (InputPlusSubsystem)
            {
                // 绑定到新的双手数据动态委托
                InputPlusSubsystem->OnHandDataReceivedDynamic.AddDynamic(this, &UHandDataListenerComponent::OnBothHandsDataReceived);
                
                UE_LOG(LogTemp, Log, TEXT("HandDataListenerComponent: Successfully bound to InputPlusSubsystem for both hands data"));
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("HandDataListenerComponent: Failed to get InputPlusSubsystem"));
            }
        }
    }
}

void UHandDataListenerComponent::UnbindFromInputPlusSubsystem()
{
    if (InputPlusSubsystem)
    {
        // 从双手数据动态委托解绑
        InputPlusSubsystem->OnHandDataReceivedDynamic.RemoveDynamic(this, &UHandDataListenerComponent::OnBothHandsDataReceived);
        
        InputPlusSubsystem = nullptr;
        
        UE_LOG(LogTemp, Log, TEXT("HandDataListenerComponent: Unbound from InputPlusSubsystem"));
    }
}

void UHandDataListenerComponent::getHandDir(const TArray<FVector>& Points21, FVector& Forward, FVector& Right,
    FVector& Up, bool bIsLeft)
{
    if (Points21.Num()< 21)
    {
        UE_LOG(LogTemp, Warning, TEXT("getHandDir: Points21 array does not contain enough points (expected 21, got %d)"), Points21.Num());
        return;
    }
    FVector v1 = Points21[bIsLeft? 9 : 17 ];
    FVector v2 = Points21[bIsLeft? 0 : 0 ];
    FVector v3 = Points21[bIsLeft? 17 : 9 ];
    Forward = (Points21[9] - v2).GetSafeNormal();
    Up = FVector::CrossProduct(v3 - v2, v1 - v2).GetSafeNormal();
    Right = FVector::CrossProduct(Forward, Up).GetSafeNormal();
}

void UHandDataListenerComponent::TransformToRotMap(const TArray<FVector>& Points21,
    TMap<int32, FRotator>& OutRotRelativeMap, TMap<int32, FRotator>& OutRotParentMap,bool isLeft)
{
    if (Points21.Num()< 21)
    {
        UE_LOG(LogTemp, Warning, TEXT("getHandDir: Points21 array does not contain enough points (expected 21, got %d)"), Points21.Num());
        return;
    }
    
    TArray<FVector> UEPoints = Points21;
    for (int i =0; i < 21; i++)
    {
        FVector locV = FVector(Points21[i].Z, Points21[i].X, Points21[i].Y * (-1));
        UEPoints[i] = locV;
    }
    FVector Forward, Right, Up;
    getHandDir(UEPoints, Forward, Right, Up);
    // ((X=0,Y=5,Z=6),(X=5,Y=6,Z=7),(X=6,Y=7,Z=8),(X=0,Y=9,Z=10),(X=9,Y=10,Z=11),(X=10,Y=11,Z=12),(X=0,Y=13,Z=14),(X=13,Y=14,Z=15),(X=14,Y=15,Z=16),(X=0,Y=17,Z=18),(X=17,Y=18,Z=19),(X=18,Y=19,Z=20),(X=0,Y=1,Z=2),(X=1,Y=2,Z=3),(X=2,Y=3,Z=4),(X=0,Y=0,Z=0))
    TArray<FIntVector> Rot = {
        FIntVector(0, 5, 6), FIntVector(5, 6, 7), FIntVector(6, 7, 8),
        FIntVector(0, 9, 10), FIntVector(9, 10, 11), FIntVector(10, 11, 12),
        FIntVector(0, 13, 14), FIntVector(13, 14, 15),FIntVector(14, 15, 16),
        FIntVector(0, 17, 18), FIntVector(17, 18, 19), FIntVector(18, 19, 20),
        FIntVector(0, 1, 2), FIntVector(1, 2, 3), FIntVector(2, 3, 4)};

    // 手掌基础旋转
    FRotator BaseRot = FRotationMatrix::MakeFromXZ(Forward, Up).Rotator();
    OutRotParentMap.Add(0, BaseRot);
    
    // 计算每个关节的绝对旋转和相对旋转
    for (auto i : Rot)
    {
        FVector A = UEPoints[i.X];
        FVector B = UEPoints[i.Y];
        FVector C = UEPoints[i.Z];

        if (i.X == 0)
        {
            FVector ParForward = (B - A).GetSafeNormal();
            FVector UpOrtho = (Up - FVector::DotProduct(Up, ParForward) * ParForward).GetSafeNormal();
            // Parent orientation for root can be derived if needed; currently not used further.
            FRotationMatrix::MakeFromXZ(ParForward, UpOrtho);
        }
        // self

        FVector BoneDirection = (C - B).GetSafeNormal();

        FVector BoneUp = FVector::CrossProduct(Right,BoneDirection).GetSafeNormal();
        FVector BoneUpOrtho = (BoneUp - FVector::DotProduct(BoneUp, BoneDirection) * BoneDirection).GetSafeNormal();
        
        FRotator BoneRot = FRotationMatrix::MakeFromXZ(BoneDirection, BoneUpOrtho).Rotator();
        OutRotParentMap.Add(i.Y, BoneRot - FRotator(0,0,(isLeft ? 90 : -90)));

        //计算子骨骼在父骨骼坐标系下的相对旋转
        // A、B 都是世���旋转
        //FQuat Aq = ParentRot.Quaternion();
        FQuat Aq = FRotationMatrix::MakeFromXZ(Forward, Up).ToQuat();
        FQuat Bq = BoneRot.Quaternion();

        // B 相对 A 的局部旋转
        FQuat RelQ = Aq.Inverse() * Bq ;   // ← 关键公式
        FRotator Rel = RelQ.Rotator();

        OutRotRelativeMap.Add(i.Y, Rel- FRotator(0,0,(isLeft ? 90 : -90)));
        OutRotRelativeMap.Add(i.Y, FRotator(Rel.Pitch,Rel.Yaw * (isLeft ? 1: -1),Rel.Roll - (isLeft ? 90 : -90)));
            
    }
}

// 计算手掌参考尺寸：使用 0->(5,9,13,17) 的距离均值作为尺度
float UHandDataListenerComponent::ComputePalmReferenceSize(const TArray<FVector>& Points21)
{
    if (Points21.Num() == 0)
    {
        return 0.f;
    }
    const int32 Anchor = 0;
    const int32 Candidates[4] = {5, 9, 13, 17};
    float Sum = 0.f;
    int32 Cnt = 0;
    for (int i = 0; i < 4; ++i)
    {
        int32 Idx = Candidates[i];
        if (Points21.IsValidIndex(Anchor) && Points21.IsValidIndex(Idx))
        {
            Sum += FVector::Distance(Points21[Anchor], Points21[Idx]);
            ++Cnt;
        }
    }
    if (Cnt == 0)
    {
        return 0.f;
    }
    return Sum / static_cast<float>(Cnt);
}

// 平滑与离群过滤；OutValues 输出用于后续处理/广播的数据。
bool UHandDataListenerComponent::SmoothAndFilterOneHand(const TArray<FString>& Keys,
                                const TArray<FVector>& InValues,
                                TArray<FVector>& InOutPrevPoints,
                                float& InOutPrevPalmSize,
                                TArray<FVector>& OutValues)
{
    OutValues = InValues; // 默认直接透传

    const int32 NumPts = InValues.Num();
    if (NumPts == 0)
    {
        return false; // 无新数据
    }

    // 如果第一帧没有缓存，初始化并直接输出
    if (InOutPrevPoints.Num() != NumPts)
    {
        InOutPrevPoints = InValues;
        InOutPrevPalmSize = ComputePalmReferenceSize(InValues);
        return true;
    }

    // 自适应阈值
    float PalmRef = ComputePalmReferenceSize(InValues);
    if (PalmRef <= KINDA_SMALL_NUMBER)
    {
        PalmRef = InOutPrevPalmSize > KINDA_SMALL_NUMBER ? InOutPrevPalmSize : 1.f;
    }
    InOutPrevPalmSize = PalmRef; // 更新参考

    // 逐点离群检测并过滤
    int32 OutlierCount = 0;
    const float JumpThreshold = OutlierJumpScale * PalmRef;
    for (int32 i = 0; i < NumPts; ++i)
    {
        const FVector& Prev = InOutPrevPoints[i];
        const FVector& Cur = InValues[i];
        const float Delta = FVector::Dist(Prev, Cur);
        if (bEnableOutlierFilter && Delta > JumpThreshold)
        {
            // 视为离群：保持上一帧值
            ++OutlierCount;
            OutValues[i] = Prev;
        }
        else
        {
            OutValues[i] = Cur;
        }
    }

    // 判定是否丢弃整帧
    if (bEnableOutlierFilter && bDropFrameOnTooManyOutliers && NumPts > 0)
    {
        const float Ratio = static_cast<float>(OutlierCount) / static_cast<float>(NumPts);
        if (Ratio >= DropFrameBadPointRatio)
        {
            // 整帧丢弃：保持上一帧
            OutValues = InOutPrevPoints;
            return false; // 表示未采用新帧
        }
    }

    // 指数平滑
    if (bEnableSmoothing)
    {
        const float Alpha = FMath::Clamp(SmoothingAlpha, 0.f, 1.f);
        for (int32 i = 0; i < NumPts; ++i)
        {
            const FVector& Prev = InOutPrevPoints[i];
            const FVector& Target = OutValues[i];
            InOutPrevPoints[i] = FMath::Lerp(Prev, Target, Alpha);
        }
        OutValues = InOutPrevPoints; // 输出为平滑后
    }
    else
    {
        // 不平滑则直接更新缓存为当前（已过滤）
        InOutPrevPoints = OutValues;
    }

    return true;
}

// 增强版平滑与过滤
bool UHandDataListenerComponent::SmoothAndFilterOneHandEx(
    const TArray<FString>& Keys,
    const TArray<FVector>& InValues,
    TArray<FVector>& InOutPrevPoints,
    float& InOutPrevPalmSize,
    bool bIsLeft,
    float DeltaTime,
    bool bJustReacquired,
    int32 AcceptedCount,
    TArray<FVector>& OutValues)
{
    OutValues = InValues;
    const int32 NumPts = InValues.Num();
    if (NumPts == 0)
    {
        return false;
    }

    // 无有效上一帧或重获跟踪作为基线
    const bool bNoPrev = (InOutPrevPoints.Num() != NumPts);
    if ((bJustReacquired && bTreatReacquireAsBaseline) || bNoPrev)
    {
        InOutPrevPoints = InValues;
        InOutPrevPalmSize = ComputePalmReferenceSize(InValues);
        OutValues = InOutPrevPoints;
        return true;
    }

    // 计算参考尺寸
    float PalmRef = ComputePalmReferenceSize(InValues);
    if (PalmRef <= KINDA_SMALL_NUMBER)
    {
        PalmRef = InOutPrevPalmSize > KINDA_SMALL_NUMBER ? InOutPrevPalmSize : 1.f;
    }
    InOutPrevPalmSize = PalmRef;

    // 基础跳变阈值
    float Threshold = OutlierJumpScale * PalmRef;

    // 自适应阈值（随丢帧放宽）
    if (bAdaptiveJumpThreshold)
    {
        const float TargetDt = 1.0f / FMath::Max(1.0f, TargetFrameRate);
        const float Scale = FMath::Clamp(DeltaTime / TargetDt, 1.0f, MaxAdaptiveThresholdScale);
        Threshold *= Scale;
    }

    // 预热阶段：放宽或跳过离群��定
    const bool bInWarmup = AcceptedCount < WarmupAcceptedFrames;

    int32 OutlierCount = 0;
    TArray<FVector> Filtered = InValues;
    if (bEnableOutlierFilter && !bInWarmup)
    {
        for (int32 i = 0; i < NumPts; ++i)
        {
            const FVector& Prev = InOutPrevPoints[i];
            const FVector& Cur = InValues[i];
            const float Delta = FVector::Dist(Prev, Cur);
            if (Delta > Threshold)
            {
                ++OutlierCount;
                Filtered[i] = Prev; // 抑制离群
            }
        }

        if (bDropFrameOnTooManyOutliers && NumPts > 0)
        {
            const float Ratio = static_cast<float>(OutlierCount) / static_cast<float>(NumPts);
            if (Ratio >= DropFrameBadPointRatio)
            {
                // 丢弃：维持上一帧
                OutValues = InOutPrevPoints;
                return false;
            }
        }
    }

    // 速度限幅（按 dt）
    if (bVelocityClampEnabled)
    {
        const float MaxDisp = MaxSpeedScalePerSecond * PalmRef * FMath::Max(0.f, DeltaTime);
        if (MaxDisp > 0.f)
        {
            for (int32 i = 0; i < NumPts; ++i)
            {
                const FVector& Prev = InOutPrevPoints[i];
                const FVector& Target = Filtered[i];
                const FVector Offset = Target - Prev;
                const float Len = Offset.Size();
                if (Len > MaxDisp)
                {
                    Filtered[i] = Prev + Offset * (MaxDisp / Len);
                }
            }
        }
    }

    // 平滑（指数）
    if (bEnableSmoothing)
    {
        const float Alpha = FMath::Clamp(SmoothingAlpha, 0.f, 1.f);
        for (int32 i = 0; i < NumPts; ++i)
        {
            const FVector& Prev = InOutPrevPoints[i];
            const FVector& Target = Filtered[i];
            InOutPrevPoints[i] = FMath::Lerp(Prev, Target, Alpha);
        }
        OutValues = InOutPrevPoints;
    }
    else
    {
        InOutPrevPoints = Filtered;
        OutValues = Filtered;
    }

    return true;
}



void UHandDataListenerComponent::OnBothHandsDataReceived(const TArray<FString>& LeftHandKeys, const TArray<FVector>& LeftHandValues,
                                                       const TArray<FString>& RightHandKeys, const TArray<FVector>& RightHandValues)
{
    // 复制数据，因为我们需要跨线程传递
    TArray<FString> LeftKeysCopy = LeftHandKeys;
    TArray<FVector> LeftValuesCopy = LeftHandValues;
    TArray<FString> RightKeysCopy = RightHandKeys;
    TArray<FVector> RightValuesCopy = RightHandValues;
    
    // 使用 AsyncTask 确保在游戏线程执行广播
    AsyncTask(ENamedThreads::GameThread, [this, LeftKeysCopy, LeftValuesCopy, RightKeysCopy, RightValuesCopy]()
    {
        if (!IsValid(this))
        {
            return;
        }

        UWorld* World = GetWorld();
        const double Now = World ? World->GetTimeSeconds() : 0.0;

        // 左手时间与状态
        const bool bLeftHasRaw = LeftValuesCopy.Num() >= 21; // 期望 21 点
        const bool bLeftJustReacquired = bLeftHasRaw && !bPrevLeftHadRawData;
        const float LeftDt = (LastLeftTimeSec > 0.0 && World) ? static_cast<float>(Now - LastLeftTimeSec) : (1.0f / FMath::Max(1.0f, TargetFrameRate));

        // 右手时间与状态
        const bool bRightHasRaw = RightValuesCopy.Num() >= 21;
        const bool bRightJustReacquired = bRightHasRaw && !bPrevRightHadRawData;
        const float RightDt = (LastRightTimeSec > 0.0 && World) ? static_cast<float>(Now - LastRightTimeSec) : (1.0f / FMath::Max(1.0f, TargetFrameRate));

        // 处理
        TArray<FVector> LeftProcessed = LeftValuesCopy;
        TArray<FVector> RightProcessed = RightValuesCopy;

        bool bLeftAccepted = false;
        bool bRightAccepted = false;

        if (bLeftHasRaw)
        {
            bLeftAccepted = SmoothAndFilterOneHandEx(LeftKeysCopy, LeftValuesCopy, PrevLeftPoints, PrevLeftPalmSize, true, LeftDt, bLeftJustReacquired, LeftAcceptedFrames, LeftProcessed);
            if (bLeftAccepted) { ++LeftAcceptedFrames; }
            LastLeftTimeSec = Now;
        }

        if (bRightHasRaw)
        {
            bRightAccepted = SmoothAndFilterOneHandEx(RightKeysCopy, RightValuesCopy, PrevRightPoints, PrevRightPalmSize, false, RightDt, bRightJustReacquired, RightAcceptedFrames, RightProcessed);
            if (bRightAccepted) { ++RightAcceptedFrames; }
            LastRightTimeSec = Now;
        }

        // 更新“是否有原始数据”标记
        bPrevLeftHadRawData = bLeftHasRaw;
        bPrevRightHadRawData = bRightHasRaw;

        // 若两侧都未接受新帧，直接返回
        if (!bLeftAccepted && !bRightAccepted)
        {
            return;
        }

        // 广播与旋转计算使用处理后的数据
        OnBothHands.Broadcast(LeftKeysCopy, LeftProcessed, RightKeysCopy, RightProcessed);

        this->TransformToRotMap(LeftProcessed, LeftRotRelativeMap, LeftRotWorldMap, true);
        this->TransformToRotMap(RightProcessed, RightRotRelativeMap, RightRotWorldMap, false);

        // 兼容旧委托：优先右手
        if (RightKeysCopy.Num() > 0)
        {
            OnHands.Broadcast(RightKeysCopy, RightProcessed);
        }
        else if (LeftKeysCopy.Num() > 0)
        {
            OnHands.Broadcast(LeftKeysCopy, LeftProcessed);
        }
    });
}

void UHandDataListenerComponent::GetLatestHandData(FHandLandmarkData& OutLeftHand, FHandLandmarkData& OutRightHand)
{
    if (InputPlusSubsystem)
    {
        InputPlusSubsystem->GetLatestHandData(OutLeftHand, OutRightHand);
    }
    else
    {
        OutLeftHand.Reset();
        OutRightHand.Reset();
    }
}