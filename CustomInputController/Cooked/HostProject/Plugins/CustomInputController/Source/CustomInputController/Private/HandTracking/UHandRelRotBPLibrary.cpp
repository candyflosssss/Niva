// HandKinematicsBPLibrary.cpp

#include "HandTracking/UHandRelRotBPLibrary.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Animation/Skeleton.h"

// 五指链（0-4 拇指，5-8 食指...）
static const int32 CHAINS[5][5] = {
    {0,1,2,3,4},
    {0,5,6,7,8},
    {0,9,10,11,12},
    {0,13,14,15,16},
    {0,17,18,19,20}
};

// 会输出的 15 个关节
static const int32 JOINTS15[15] = {1,2,3, 5,6,7, 9,10,11, 13,14,15, 17,18,19};

FVector UHandKinematicsBPLibrary::NormalizeSafe(const FVector& V)
{
    const double S = V.Size();
    return (S > 1e-8) ? V / S : FVector::ZeroVector;
}

FVector UHandKinematicsBPLibrary::StablePalmZ(const TArray<FVector>& P, bool bIsRightHand, const FVector* PrevPalmZ)
{
    const int idxs[] = {0,5,9,13,17}; // Wrist + 四 MCP
    FVector n(0,0,0);
    for (int a=0; a<5; ++a)
    for (int b=a+1; b<5; ++b)
    for (int c=b+1; c<5; ++c)
    {
        const FVector& A = P[idxs[a]];
        const FVector& B = P[idxs[b]];
        const FVector& C = P[idxs[c]];
        n += FVector::CrossProduct(B - A, C - A);
    }
    n = NormalizeSafe(n);              // n 指向手背 or 手心
    if (bIsRightHand) n *= -1.0;       // 统一左右
    if (PrevPalmZ && !PrevPalmZ->IsNearlyZero())
    {
        // 与上一帧保持一致方向并略做平滑
        if (FVector::DotProduct(n, *PrevPalmZ) < 0) n *= -1.0;
        n = NormalizeSafe(n + 0.2 * (*PrevPalmZ));
    }
    return n;
}

/** 轴约定：Z=掌法向(手背)；X= (B-A) 在掌面内的投影；Y = Z × X */
FQuat UHandKinematicsBPLibrary::BoneFrameQuat_ZisPalmXisProj(const FVector& A, const FVector& B, const FVector& PalmZ, const FVector* PrevX)
{
    const FVector Z = PalmZ.GetSafeNormal();
    const FVector Raw = (B - A).GetSafeNormal();

    FVector X = Raw - Z * FVector::DotProduct(Raw, Z);
    if (X.SizeSquared() < 1e-6 && PrevX && PrevX->SizeSquared() > 0)
    {
        X = *PrevX - Z * FVector::DotProduct(*PrevX, Z);
    }
    if (X.SizeSquared() < 1e-6)
    {
        const FVector Tmp = (FMath::Abs(Z.X) < 0.9) ? FVector(1,0,0) : FVector(0,1,0);
        X = FVector::CrossProduct(Z, Tmp); // 随便造个与 Z 正交的 X
    }
    X = X.GetSafeNormal();
    const FVector Y = FVector::CrossProduct(Z, X);

    FMatrix M = FMatrix::Identity;
    M.SetAxis(0, X); M.SetAxis(1, Y); M.SetAxis(2, Z);
    return FQuat(M);
}

/** 计算父段(p->j)到子段(j->c)的相对旋转；可输出子段 X 用于连续性 */
FQuat UHandKinematicsBPLibrary::RelRotParentToChild(
    const FVector& P, const FVector& J, const FVector& C,
    const FVector& PalmZ, FVector* OutChildX, const FVector* PrevParentX, const FVector* PrevChildX)
{
    const FQuat Qp = BoneFrameQuat_ZisPalmXisProj(P, J, PalmZ, PrevParentX);
    const FQuat Qc = BoneFrameQuat_ZisPalmXisProj(J, C, PalmZ, PrevChildX);
    if (OutChildX)
    {
        const FMatrix Mc = FQuatRotationMatrix(Qc);
        *OutChildX = Mc.GetScaledAxis(EAxis::X);
    }
    return Qp.Inverse() * Qc; // 父->子 相对旋转（本地）
}

bool UHandKinematicsBPLibrary::IsPIPorDIP(int32 J)
{
    switch (J)
    {
        // 食/中/无/小：PIP(6,10,14,18) + DIP(7,11,15,19)
        case 6: case 7: case 10: case 11: case 14: case 15: case 18: case 19:
        // 拇指：IP(3) 也按单轴铰链处理
        case 3:
            return true;
        default: return false;
    }
}

bool UHandKinematicsBPLibrary::IsMCP(int32 J)
{
    switch (J) { case 1: case 5: case 9: case 13: case 17: return true; default: return false; }
}

/** PIP/DIP：只保留绕 +Y 的旋转并限位 */
FQuat UHandKinematicsBPLibrary::LimitPIPDIP_YHinge(const FQuat& Rrel, float MinDeg, float MaxDeg)
{
    FVector Axis; double Angle;
    Rrel.ToAxisAndAngle(Axis, Angle);                 // 弧度
    double Signed = Angle * FMath::Sign(FVector::DotProduct(Axis, FVector::YAxisVector));
    Signed = FMath::Clamp(Signed, FMath::DegreesToRadians(MinDeg), FMath::DegreesToRadians(MaxDeg));
    return FQuat(FVector::YAxisVector, Signed);
}

/** MCP：移除绕 X 的扭转（Twist），保留 Swing（Y/Z 平面） */
FQuat UHandKinematicsBPLibrary::RemoveTwistAroundX(const FQuat& Rrel)
{
    // swing-twist 分解（twist 轴=X）
    const FVector AxisX = FVector::XAxisVector;
    FVector r(Rrel.X, Rrel.Y, Rrel.Z);
    FVector proj = AxisX * FVector::DotProduct(r, AxisX);
    FQuat Twist(proj.X, proj.Y, proj.Z, Rrel.W);
    Twist.Normalize();
    FQuat Swing = Rrel * Twist.Inverse();
    Swing.Normalize();
    return Swing;
}

/** MCP 可选限位（绕 Y = 屈伸；绕 Z = 外展内收） */
FQuat UHandKinematicsBPLibrary::ClampMCP_YZ(const FQuat& Rrel, float FlexMinDeg, float FlexMaxDeg, float AbAdMaxDeg)
{
    // 把 Rrel 近似分解为绕 Y 和 Z 的合成（忽略 X 分量）
    FVector Axis; double Angle; Rrel.ToAxisAndAngle(Axis, Angle);
    FVector AxisYZ = FVector(Axis.X, Axis.Y, Axis.Z);
    AxisYZ.X = 0; // 去掉 X
    if (AxisYZ.IsNearlyZero()) return Rrel;

    AxisYZ.Normalize();
    // 把分量投到 Y 与 Z
    const double ydot = FVector::DotProduct(AxisYZ, FVector::YAxisVector);
    const double zdot = FVector::DotProduct(AxisYZ, FVector::ZAxisVector);
    double Ay = Angle * ydot;
    double Az = Angle * zdot;

    Ay = FMath::Clamp(Ay, FMath::DegreesToRadians(FlexMinDeg), FMath::DegreesToRadians(FlexMaxDeg));
    Az = FMath::Clamp(Az, -FMath::DegreesToRadians(AbAdMaxDeg), FMath::DegreesToRadians(AbAdMaxDeg));

    FQuat Qy(FVector::YAxisVector, Ay);
    FQuat Qz(FVector::ZAxisVector, Az);
    FQuat Out = Qz * Qy; // 先绕 Y 再绕 Z（顺序可按需要调整）
    Out.Normalize();
    return Out;
}

// —— 主计算 —— //
void UHandKinematicsBPLibrary::BuildRelRotations_Internal(
    const TArray<FVector>& Points21, bool bIsRight, FHandRuntimeState& State, const FHandLimitsConfig& Limits,
    TMap<int32, FQuat>& OutQuatMap)
{
    OutQuatMap.Reset();

    if (Points21.Num() < 21) return;

    // 1) 稳定掌法向（手背 +Z）
    const FVector PalmZ = StablePalmZ(Points21, bIsRight, &State.PrevPalmZ);
    State.PrevPalmZ = PalmZ;

    // 2) 逐指逐段：父->子 相对旋转，维护段内 X 的连续
    for (int f=0; f<5; ++f)
    {
        const int32* Chain = CHAINS[f];

        // 三个中间关节：i=1..3
        for (int i=1; i<=3; ++i)
        {
            const int32 p = Chain[i-1];
            const int32 j = Chain[i];
            const int32 c = Chain[i+1];

            // 取上一帧的 X：父段(p->j) 的 Key=j；子段(j->c) 的 Key=c
            const FVector* PrevXp = State.PrevXByChildJoint.Find(j);
            const FVector* PrevXc = State.PrevXByChildJoint.Find(c);

            FVector ChildX;
            FQuat Rrel = RelRotParentToChild(
                Points21[p], Points21[j], Points21[c],
                PalmZ, &ChildX, PrevXp, PrevXc);

            // 3) 限位/去扭
            if (IsPIPorDIP(j) && Limits.bLimitPIPDIP)
            {
                Rrel = LimitPIPDIP_YHinge(Rrel, Limits.PIPDIP_MinDeg, Limits.PIPDIP_MaxDeg);
            }
            else if (IsMCP(j))
            {
                if (Limits.bRemoveMCPTwistX) Rrel = RemoveTwistAroundX(Rrel);
                if (Limits.bLimitMCP)
                {
                    Rrel = ClampMCP_YZ(Rrel, Limits.MCP_FlexMinDeg, Limits.MCP_FlexMaxDeg, Limits.MCP_AbAdMaxDeg);
                }
            }

            Rrel.Normalize();
            OutQuatMap.Add(j, Rrel);

            // 更新子段 X，供下一帧使用
            State.PrevXByChildJoint.FindOrAdd(c) = ChildX;
        }
    }
}


// —— 对外：蓝图可见的函数 —— //

void UHandKinematicsBPLibrary::ComputeHandRelativeRotations_Map(
    const TArray<FVector>& Points21,
    bool bIsRightHand,
    FHandRuntimeState& InOutState,
    const FHandLimitsConfig& Limits,
    TMap<int32, FRotator>& OutJointRotMap)
{
    OutJointRotMap.Reset();
    if (Points21.Num() < 21)
    {
        for (int i=0;i<15;++i) OutJointRotMap.Add(JOINTS15[i], FRotator::ZeroRotator);
        UE_LOG(LogTemp, Warning, TEXT("ComputeHandRelativeRotations_Map: Points21.Num()=%d < 21"), Points21.Num());
        return;
    }

    TMap<int32, FQuat> QMap;
    BuildRelRotations_Internal(Points21, bIsRightHand, InOutState, Limits, QMap);

    for (const auto& KVP : QMap)
    {
        OutJointRotMap.Add(KVP.Key, KVP.Value.Rotator());
    }
}

FRotator UHandKinematicsBPLibrary::ComputeWristOrientation(
    const TArray<FVector>& Points21,
    bool bIsRightHand,
    FHandRuntimeState& InOutState)
{
    if (Points21.Num() < 21)
    {
        UE_LOG(LogTemp, Warning, TEXT("ComputeWristOrientation: Points21.Num()=%d < 21"), Points21.Num());
        return FRotator::ZeroRotator;
    }
    // Z = PalmZ；X = WRIST->MIDDLE_MCP 投影到掌面；Y = Z×X
    const FVector PalmZ = StablePalmZ(Points21, bIsRightHand, &InOutState.PrevPalmZ);
    InOutState.PrevPalmZ = PalmZ;

    const FVector A = Points21[0];
    const FVector B = Points21[9];

    const FQuat Qw = BoneFrameQuat_ZisPalmXisProj(A, B, PalmZ, nullptr);
    return Qw.Rotator();
}

// —— Offset 标定 —— //

void UHandKinematicsBPLibrary::CalibrateOffsets_AssumeIdentityRef(
    const TArray<FVector>& PointsAtBind,
    bool bIsRightHand,
    FHandRuntimeState& InOutState,
    const FHandLimitsConfig& Limits,
    FHandCalibOffsets& OutOffsets)
{
    TMap<int32, FQuat> QMap;
    BuildRelRotations_Internal(PointsAtBind, bIsRightHand, InOutState, Limits, QMap);

    OutOffsets.OffsetByJoint.Reset();
    for (int j : JOINTS15)
    {
        const FQuat* Rm = QMap.Find(j);
        const FQuat  O  = Rm ? Rm->Inverse() : FQuat::Identity;
        OutOffsets.OffsetByJoint.Add(j, O.Rotator());
    }
    OutOffsets.bValid = true;
}

void UHandKinematicsBPLibrary::CalibrateOffsets_FromSkeletalMesh(
    USkeletalMeshComponent* SkelComp,
    const TMap<int32, FName>& JointToBoneName,
    const TMap<int32, FRotator>& MeasuredAtBind,
    FHandCalibOffsets& OutOffsets)
{
    OutOffsets.OffsetByJoint.Reset();
    OutOffsets.bValid = false;

    if (!SkelComp || !SkelComp->GetSkeletalMeshAsset())
    {
        UE_LOG(LogTemp, Warning, TEXT("CalibrateOffsets_FromSkeletalMesh: invalid SkeletalMeshComponent"));
        return;
    }

    const USkeletalMesh* Skel = SkelComp->GetSkeletalMeshAsset();
    const FReferenceSkeleton& RefSkel = Skel->GetRefSkeleton();
    const TArray<FTransform>& RefPose = RefSkel.GetRefBonePose();

    for (const auto& Pair : JointToBoneName)
    {
        const int32 J = Pair.Key;
        const FName BoneName = Pair.Value;
        const int32 BoneIdx = RefSkel.FindBoneIndex(BoneName);
        if (BoneIdx == INDEX_NONE) continue;

        const FTransform& LocalRef = RefPose[BoneIdx];         // 父空间（本地）RefPose
        const FQuat UE_BindLocal = LocalRef.GetRotation();

        const FRotator* MeasRot = MeasuredAtBind.Find(J);
        const FQuat MeasBind = MeasRot ? FQuat(*MeasRot) : FQuat::Identity;

        const FQuat Offset = UE_BindLocal * MeasBind.Inverse();
        OutOffsets.OffsetByJoint.Add(J, Offset.Rotator());
    }

    OutOffsets.bValid = true;
}

void UHandKinematicsBPLibrary::ApplyOffsetsToMap(
    const TMap<int32, FRotator>& Measured,
    const FHandCalibOffsets& Offsets,
    TMap<int32, FRotator>& OutApplied)
{
    OutApplied.Reset();
    if (!Offsets.bValid)
    {
        // 直接透传
        for (const auto& K : Measured) OutApplied.Add(K.Key, K.Value);
        return;
    }

    for (const auto& K : Measured)
    {
        const int32 J = K.Key;
        const FQuat Rm = FQuat(K.Value);
        const FRotator* OffRot = Offsets.OffsetByJoint.Find(J);
        const FQuat O = OffRot ? FQuat(*OffRot) : FQuat::Identity;

        const FQuat Apply = O * Rm;
        OutApplied.Add(J, Apply.Rotator());
    }
}

// —— 骨名映射（Mannequin） —— //

void UHandKinematicsBPLibrary::MakeMannequinRightHandMap(TMap<int32, FName>& OutMap)
{
    OutMap.Reset();
    OutMap.Add(1,  "thumb_01_r"); OutMap.Add(2,  "thumb_02_r"); OutMap.Add(3,  "thumb_03_r");
    OutMap.Add(5,  "index_01_r"); OutMap.Add(6,  "index_02_r"); OutMap.Add(7,  "index_03_r");
    OutMap.Add(9,  "middle_01_r");OutMap.Add(10, "middle_02_r");OutMap.Add(11, "middle_03_r");
    OutMap.Add(13, "ring_01_r");  OutMap.Add(14, "ring_02_r");  OutMap.Add(15, "ring_03_r");
    OutMap.Add(17, "pinky_01_r"); OutMap.Add(18, "pinky_02_r"); OutMap.Add(19, "pinky_03_r");
}

void UHandKinematicsBPLibrary::MakeMannequinLeftHandMap(TMap<int32, FName>& OutMap)
{
    OutMap.Reset();
    OutMap.Add(1,  "thumb_01_l"); OutMap.Add(2,  "thumb_02_l"); OutMap.Add(3,  "thumb_03_l");
    OutMap.Add(5,  "index_01_l"); OutMap.Add(6,  "index_02_l"); OutMap.Add(7,  "index_03_l");
    OutMap.Add(9,  "middle_01_l");OutMap.Add(10, "middle_02_l");OutMap.Add(11, "middle_03_l");
    OutMap.Add(13, "ring_01_l");  OutMap.Add(14, "ring_02_l");  OutMap.Add(15, "ring_03_l");
    OutMap.Add(17, "pinky_01_l"); OutMap.Add(18, "pinky_02_l"); OutMap.Add(19, "pinky_03_l");
}




