// HandKinematicsBPLibrary.h
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Components/SkeletalMeshComponent.h"
#include "UHandRelRotBPLibrary.generated.h"

/** 运行时状态：用于让掌法向/指内 X 轴在帧间保持连续 */
USTRUCT(BlueprintType)
struct FHandRuntimeState
{
    GENERATED_BODY()

    /** 上一帧的掌法向（指向手背的 +Z） */
    UPROPERTY(BlueprintReadWrite, Category="HandKinematics")
    FVector PrevPalmZ = FVector::ZeroVector;

    /** 同一“段”的上一帧 X 轴；Key 用“该段的子关节索引”（比如段 5->6 的 Key=6） */
    UPROPERTY(BlueprintReadWrite, Category="HandKinematics")
    TMap<int32, FVector> PrevXByChildJoint;
};

/** 关节限位配置（默认已经合理，不用改也行） */
USTRUCT(BlueprintType)
struct FHandLimitsConfig
{
    GENERATED_BODY()

    /** 是否对 PIP/DIP 做单轴铰链并限位（绕 +Y） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HandKinematics")
    bool bLimitPIPDIP = true;

    /** PIP/DIP 最小/最大角度（度） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HandKinematics")
    float PIPDIP_MinDeg = -5.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HandKinematics")
    float PIPDIP_MaxDeg = 110.f;

    /** MCP 关节去扭（移除绕 X 的 twist） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HandKinematics")
    bool bRemoveMCPTwistX = true;

    /** MCP 是否限位（绕 Y = 屈伸；绕 Z = 内外展） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HandKinematics")
    bool bLimitMCP = false;

    /** MCP 限位（度） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HandKinematics", meta=(EditCondition="bLimitMCP"))
    float MCP_FlexMinDeg = -20.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HandKinematics", meta=(EditCondition="bLimitMCP"))
    float MCP_FlexMaxDeg = 90.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HandKinematics", meta=(EditCondition="bLimitMCP"))
    float MCP_AbAdMaxDeg = 35.f; // ±范围
};

/** Offset 标定结果（关节索引 -> Offset 旋转） */
USTRUCT(BlueprintType)
struct FHandCalibOffsets
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, Category="HandKinematics")
    bool bValid = false;

    UPROPERTY(BlueprintReadWrite, Category="HandKinematics")
    TMap<int32, FRotator> OffsetByJoint; // 存 Rotator，内部会转 Quat 运算
};

UCLASS()
class CUSTOMINPUTCONTROLLER_API UHandKinematicsBPLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:

    /**
     * 计算 15 个关节（不含 0 和 4/8/12/16/20 指尖）的“父->子”相对旋转（Bone/Local 空间）
     * - 轴约定：Z=手背，X=掌面内的骨向，Y=Z×X
     * - bIsRightHand：右手时会统一法向
     * - InOutState：传入并回传上一帧的 PalmZ 和每段 X，以保持连续
     * - Limits：是否做生理限位（默认已开启 PIP/DIP 铰链）
     * 输出：TMap<关节索引, Rotator(度)>
     */
    UFUNCTION(BlueprintCallable, Category="HandKinematics")
    static void ComputeHandRelativeRotations_Map(
        const TArray<FVector>& Points21,
        bool bIsRightHand,
        UPARAM(ref) FHandRuntimeState& InOutState,
        const FHandLimitsConfig& Limits,
        UPARAM(ref) TMap<int32, FRotator>& OutJointRotMap);

    /** 计算腕/手掌整体朝向（可用于 hand_r/hand_l），同样遵循 Z=手背、X=WRIST->MIDDLE_MCP 的投影 */
    UFUNCTION(BlueprintCallable, Category="HandKinematics")
    static FRotator ComputeWristOrientation(
        const TArray<FVector>& Points21,
        bool bIsRightHand,
        UPARAM(ref) FHandRuntimeState& InOutState);

    /** Offset：假定 UE 参考姿势本地旋转≈单位，Offset = Inverse(MeasuredBind) */
    UFUNCTION(BlueprintCallable, Category="HandKinematics")
    static void CalibrateOffsets_AssumeIdentityRef(
        const TArray<FVector>& PointsAtBind,
        bool bIsRightHand,
        UPARAM(ref) FHandRuntimeState& InOutState,
        const FHandLimitsConfig& Limits,
        UPARAM(ref) FHandCalibOffsets& OutOffsets);

    /** Offset：从骨架 RefPose 读取本地旋转，Offset = UE_BindLocal * Inverse(MeasuredBind) */
    UFUNCTION(BlueprintCallable, Category="HandKinematics")
    static void CalibrateOffsets_FromSkeletalMesh(
        USkeletalMeshComponent* SkelComp,
        const TMap<int32, FName>& JointToBoneName,
        const TMap<int32, FRotator>& MeasuredAtBind,
        UPARAM(ref) FHandCalibOffsets& OutOffsets);

    /** 应用 Offset：Apply = Offset * Measured（逐关节） */
    UFUNCTION(BlueprintCallable, Category="HandKinematics")
    static void ApplyOffsetsToMap(
        const TMap<int32, FRotator>& Measured,
        const FHandCalibOffsets& Offsets,
        UPARAM(ref) TMap<int32, FRotator>& OutApplied);

    /** 生成 UE5 Mannequin 右手映射（关节索引 -> 骨名） */
    UFUNCTION(BlueprintCallable, Category="HandKinematics")
    static void MakeMannequinRightHandMap(UPARAM(ref) TMap<int32, FName>& OutMap);

    /** 生成 UE5 Mannequin 左手映射（关节索引 -> 骨名） */
    UFUNCTION(BlueprintCallable, Category="HandKinematics")
    static void MakeMannequinLeftHandMap(UPARAM(ref) TMap<int32, FName>& OutMap);



private:
    // —— 内部工具 —— //
    static FORCEINLINE FVector NormalizeSafe(const FVector& V);
    static FVector StablePalmZ(const TArray<FVector>& P, bool bIsRightHand, const FVector* PrevPalmZ);
    static FQuat   BoneFrameQuat_ZisPalmXisProj(const FVector& A, const FVector& B, const FVector& PalmZ, const FVector* PrevX);
    static FQuat   RelRotParentToChild(const FVector& P, const FVector& J, const FVector& C, const FVector& PalmZ, FVector* OutChildX, const FVector* PrevParentX, const FVector* PrevChildX);

    static bool    IsPIPorDIP(int32 J);
    static bool    IsMCP(int32 J);

    static FQuat   LimitPIPDIP_YHinge(const FQuat& Rrel, float MinDeg, float MaxDeg);
    static FQuat   RemoveTwistAroundX(const FQuat& Rrel);
    static FQuat   ClampMCP_YZ(const FQuat& Rrel, float FlexMinDeg, float FlexMaxDeg, float AbAdMaxDeg);

    static void    BuildRelRotations_Internal(
        const TArray<FVector>& Points21, bool bIsRight, FHandRuntimeState& State, const FHandLimitsConfig& Limits,
        TMap<int32, FQuat>& OutQuatMap);




};
