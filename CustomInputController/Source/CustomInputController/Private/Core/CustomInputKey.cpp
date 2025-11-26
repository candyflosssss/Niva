#include "Core/CustomInputKey.h"
#include "Engine/Engine.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"

#define LOCTEXT_NAMESPACE "MyCustomInputKeys"

// 定义静态键 - 这里定义键的内部名称
const FKey FMyCustomInputKeys::GazeXYZ("GazeXYZ");
const FKey FMyCustomInputKeys::Gaze_X("Gaze_X");
const FKey FMyCustomInputKeys::Gaze_Y("Gaze_Y");
const FKey FMyCustomInputKeys::Gaze_Z("Gaze_Z");
const FKey FMyCustomInputKeys::UDPButton1("UDPButton1");
const FKey FMyCustomInputKeys::UDPButton2("UDPButton2");

// 初始化静态向量
FVector FMyCustomInputKeys::CurrentUDPVector = FVector::ZeroVector;

void FMyCustomInputKeys::AddKeys()
{
    UE_LOG(LogTemp, Log, TEXT("开始注册自定义UDP输入键..."));
    
    // 注册3D向量轴键
    EKeys::AddKey(FKeyDetails(GazeXYZ, 
        LOCTEXT("GazeXYZ", "Gaze Location XYZ"), 
        FKeyDetails::GamepadKey | FKeyDetails::Axis3D ));
    
    EKeys::AddKey(FKeyDetails(Gaze_X, 
        LOCTEXT("Gaze_X", "UDP Vector X-Axis"), 
        FKeyDetails::GamepadKey | FKeyDetails::Axis1D ));
        
    EKeys::AddKey(FKeyDetails(Gaze_Y, 
        LOCTEXT("Gaze_Y", "UDP Vector Y-Axis"), 
        FKeyDetails::GamepadKey | FKeyDetails::Axis1D ));
        
    EKeys::AddKey(FKeyDetails(Gaze_Z,
        LOCTEXT("Gaze_Z", "UDP Vector Z-Axis"), 
        FKeyDetails::GamepadKey | FKeyDetails::Axis1D ));
    
    // 注册按钮键
    EKeys::AddKey(FKeyDetails(UDPButton1, 
        LOCTEXT("UDPButton1", "UDP Button 1"), 
        FKeyDetails::GamepadKey));
        
    EKeys::AddKey(FKeyDetails(UDPButton2, 
        LOCTEXT("UDPButton2", "UDP Button 2"), 
        FKeyDetails::GamepadKey));
    
    UE_LOG(LogTemp, Log, TEXT("自定义UDP输入键注册完成"));
    
    if (ValidateKeysRegistered())
    {
        UE_LOG(LogTemp, Log, TEXT("所有自定义键验证成功"));
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("部分自定义键注册失败！"));
    }
}

void FMyCustomInputKeys::RemoveKeys()
{
    UE_LOG(LogTemp, Log, TEXT("自定义UDP输入键已移除（引擎关闭时自动清理）"));
}

void FMyCustomInputKeys::LogAllKeys()
{
    UE_LOG(LogTemp, Log, TEXT("=== 自定义UDP输入键列表 ==="));
    
    TArray<FKey> CustomKeys = {
        GazeXYZ,
        Gaze_X, Gaze_Y, Gaze_Z,
        UDPButton1, UDPButton2
    };
    
    for (int32 i = 0; i < CustomKeys.Num(); ++i)
    {
        const FKey& Key = CustomKeys[i];
        TSharedPtr<FKeyDetails> DetailsPtr = EKeys::GetKeyDetails(Key);
        
        if (DetailsPtr.IsValid())
        {
            const FKeyDetails* Details = DetailsPtr.Get();
            
            FString TypeStr = TEXT("按钮");
            if (Details->IsAxis1D())
            {
                TypeStr = TEXT("1D轴");
            }
            else if (Details->IsAxis2D())
            {
                TypeStr = TEXT("2D轴");
            }
            else if (Details->IsButtonAxis())
            {
                TypeStr = TEXT("按钮轴");
            }
            
            UE_LOG(LogTemp, Log, TEXT("%d. %s [%s] - 显示名: %s"), 
                i + 1, 
                *Key.ToString(), 
                *TypeStr, 
                *Details->GetDisplayName().ToString());
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("%d. %s - 未找到详细信息！"), i + 1, *Key.ToString());
        }
    }
    
    UE_LOG(LogTemp, Log, TEXT("当前UDP 3D向量值: X=%.3f, Y=%.3f, Z=%.3f"), 
        CurrentUDPVector.X, CurrentUDPVector.Y, CurrentUDPVector.Z);
}

bool FMyCustomInputKeys::ValidateKeysRegistered()
{
    TArray<FKey> AllKeys;
    EKeys::GetAllKeys(AllKeys);
    
    TArray<FKey> CustomKeys = {
        GazeXYZ,
        Gaze_X, Gaze_Y, Gaze_Z,
        UDPButton1, UDPButton2
    };
    
    bool bAllRegistered = true;
    
    for (const FKey& CustomKey : CustomKeys)
    {
        bool bFound = AllKeys.Contains(CustomKey);
        if (!bFound)
        {
            UE_LOG(LogTemp, Error, TEXT("自定义键 '%s' 未在系统中找到！"), *CustomKey.ToString());
            bAllRegistered = false;
        }
    }
    
    UE_LOG(LogTemp, Log, TEXT("键验证结果: %s"), bAllRegistered ? TEXT("成功") : TEXT("失败"));
    return bAllRegistered;
}

FUDPInputDevice::FUDPInputDevice(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler)
    : MessageHandler(InMessageHandler)
    , CurrentX(0.f)
    , CurrentY(0.f)
    , CurrentZ(0.f)
{
    // Map ControllerId(0) to DeviceId
    IPlatformInputDeviceMapper& Mapper = IPlatformInputDeviceMapper::Get();
    FPlatformUserId TempUser;
    Mapper.RemapControllerIdToPlatformUserAndDevice(0, TempUser, DeviceId);

    // 创建UDP处理器
    UDPHandler = NewObject<UUDPHandler>();
    if (UDPHandler)
    {
        // 绑定普通委托（不是动态委托）
        UDPHandler->OnDataReceived.AddLambda([this](const FString& ReceivedData)
        {
            // 直接调用处理函数
            OnUDPDataReceived(ReceivedData);
        });
        
        // 启动UDP监听，使用端口8091
        if (UDPHandler->StartUDPReceiver(8091))
        {
            UE_LOG(LogTemp, Log, TEXT("FUDPInputDevice: UDP监听器启动成功"));
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("FUDPInputDevice: UDP监听器启动失败"));
        }
    }
}

// 其他方法保持不变...

FUDPInputDevice::~FUDPInputDevice()
{
    if (UDPHandler)
    {
        UDPHandler->StopUDPReceiver();
        UDPHandler = nullptr;
    }
}

void FUDPInputDevice::OnUDPDataReceived(const FString& ReceivedData)
{
    float X, Y, Z;
    UE_LOG(LogTemp, Warning, TEXT("UDP接收数据: %s"), *ReceivedData);

    ParseTextToXYZ(ReceivedData, X, Y, Z);

    CurrentX.store(X);
    CurrentY.store(Y);
    CurrentZ.store(Z);
}

void FUDPInputDevice::SendControllerEvents()
{
    auto& Mapper = IPlatformInputDeviceMapper::Get();
    FPlatformUserId UserId = Mapper.GetPrimaryPlatformUser();

    if (!UserId.IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("Primary Platform User is invalid! Input may not be routed correctly."));
    }

    // 发送轴输入事件
    MessageHandler->OnControllerAnalog(FMyCustomInputKeys::Gaze_X.GetFName(), UserId, DeviceId, CurrentX);
    MessageHandler->OnControllerAnalog(FMyCustomInputKeys::Gaze_Y.GetFName(), UserId, DeviceId, CurrentY);
    MessageHandler->OnControllerAnalog(FMyCustomInputKeys::Gaze_Z.GetFName(), UserId, DeviceId, CurrentZ);
}

void FUDPInputDevice::Tick(float DeltaTime)
{
    // 现在不需要在这里处理UDP，UUDPHandler会自动处理
}

void FUDPInputDevice::SetMessageHandler(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler)
{
    MessageHandler = InMessageHandler;
}

void FUDPInputDevice::ParseTextToXYZ(const FString& InText, float& OutX, float& OutY, float& OutZ)
{
    // InText示例： 38284/524.631,165.715,-0.674/493.470,171.578,-10.267
    // 解析格式：提取最后一组X,Y,Z数据
    
    // 初始化输出值
    OutX = 0.0f;
    OutY = 0.0f;
    OutZ = 0.0f;
    
    if (InText.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("ParseTextToXYZ: 输入文本为空"));
        return;
    }
    
    // 按 '/' 分割字符串，获取所有部分
    TArray<FString> Parts;
    InText.ParseIntoArray(Parts, TEXT("/"), true);
    
    if (Parts.Num() < 2)
    {
        UE_LOG(LogTemp, Warning, TEXT("ParseTextToXYZ: 数据格式不正确，缺少足够的分段: %s"), *InText);
        return;
    }
    
    // 取最后一个部分作为XYZ坐标数据（跳过第一个数字部分）
    FString XYZData = Parts.Last();
    
    // 按逗号分割XYZ数据
    TArray<FString> Coordinates;
    XYZData.ParseIntoArray(Coordinates, TEXT(","), true);
    
    if (Coordinates.Num() != 3)
    {
        UE_LOG(LogTemp, Warning, TEXT("ParseTextToXYZ: XYZ坐标数据不完整，期望3个值，实际收到%d个: %s"), 
               Coordinates.Num(), *XYZData);
        return;
    }
    
    // 解析X, Y, Z值
    bool bParseSuccess = true;
    
    // 解析X
    if (!Coordinates[0].IsNumeric())
    {
        UE_LOG(LogTemp, Warning, TEXT("ParseTextToXYZ: X值不是有效数字: %s"), *Coordinates[0]);
        bParseSuccess = false;
    }
    else
    {
        OutX = FCString::Atof(*Coordinates[0]);
    }
    
    // 解析Y
    if (!Coordinates[1].IsNumeric())
    {
        UE_LOG(LogTemp, Warning, TEXT("ParseTextToXYZ: Y值不是有效数字: %s"), *Coordinates[1]);
        bParseSuccess = false;
    }
    else
    {
        OutY = FCString::Atof(*Coordinates[1]);
    }
    
    // 解析Z
    if (!Coordinates[2].IsNumeric())
    {
        UE_LOG(LogTemp, Warning, TEXT("ParseTextToXYZ: Z值不是有效数字: %s"), *Coordinates[2]);
        bParseSuccess = false;
    }
    else
    {
        OutZ = FCString::Atof(*Coordinates[2]);
    }
    
    if (bParseSuccess)
    {
        UE_LOG(LogTemp, Warning, TEXT("ParseTextToXYZ: 成功解析坐标 - X=%.3f, Y=%.3f, Z=%.3f"), 
               OutX, OutY, OutZ);
    }
    else
    {
        // 解析失败时重置为0
        OutX = 0.0f;
        OutY = 0.0f;
        OutZ = 0.0f;
        UE_LOG(LogTemp, Error, TEXT("ParseTextToXYZ: 解析失败，重置坐标为零值"));
    }
}

#undef LOCTEXT_NAMESPACE