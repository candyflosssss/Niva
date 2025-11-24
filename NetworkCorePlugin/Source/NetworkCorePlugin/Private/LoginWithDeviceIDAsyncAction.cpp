#include "LoginWithDeviceIDAsyncAction.h"

#include "NivaOnlineSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "OnlineSubsystem.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

ULoginWithDeviceIDAsyncAction* ULoginWithDeviceIDAsyncAction::LoginWithDeviceIDAsync(UObject* WorldContextObjectIn)
{
    ULoginWithDeviceIDAsyncAction* Action = NewObject<ULoginWithDeviceIDAsyncAction>();
    Action->WorldContextObject = WorldContextObjectIn;
    return Action;
}

void ULoginWithDeviceIDAsyncAction::Activate()
{
    UObject* WC = WorldContextObject.Get();
    UWorld* World = WC ? WC->GetWorld() : nullptr;
    if (!World)
    {
        OnFailure.Broadcast(TEXT("Invalid WorldContextObject"));
        SetReadyToDestroy();
        return;
    }

    UGameInstance* GI = World->GetGameInstance();
    if (!GI)
    {
        OnFailure.Broadcast(TEXT("No GameInstance"));
        SetReadyToDestroy();
        return;
    }

    UNivaOnlineSubsystem* NivaSubsystem = GI->GetSubsystem<UNivaOnlineSubsystem>();
    if (!NivaSubsystem)
    {
        OnFailure.Broadcast(TEXT("NivaOnlineSubsystem not available"));
        SetReadyToDestroy();
        return;
    }

    // 优先获取 EOS 子系统的 Identity，以确保委托与实际登录的子系统一致
    IOnlineSubsystem* OSS = IOnlineSubsystem::Get(TEXT("EOS"));
    if (!OSS)
    {
        OSS = IOnlineSubsystem::Get();
        if (!OSS)
        {
            OnFailure.Broadcast(TEXT("No OnlineSubsystem"));
            SetReadyToDestroy();
            return;
        }
    }

    IOnlineIdentityPtr Identity = OSS->GetIdentityInterface();
    if (!Identity.IsValid())
    {
        OnFailure.Broadcast(TEXT("Identity interface invalid"));
        SetReadyToDestroy();
        return;
    }

    // 绑定回调（监听登录结果）
    if (!LoginCompleteDelegateHandle.IsValid())
    {
        LoginCompleteDelegateHandle = Identity->AddOnLoginCompleteDelegate_Handle(0,
            FOnLoginCompleteDelegate::CreateUObject(this, &ULoginWithDeviceIDAsyncAction::HandleLoginComplete));
    }

    // 通过子系统触发登录逻辑（内部会在首次尝试 EOS Connect 创建 DeviceId）
    const bool bTriggered = NivaSubsystem->LoginWithDeviceID();
    if (!bTriggered)
    {
        // 未触发 Login：在纯匿名（Connect-only）模式下，这通常表示配置不正确或未启用EOS为默认子系统
        if (OSS)
        {
            if (Identity.IsValid() && LoginCompleteDelegateHandle.IsValid())
            {
                Identity->ClearOnLoginCompleteDelegate_Handle(0, LoginCompleteDelegateHandle);
                LoginCompleteDelegateHandle.Reset();
            }
        }
        OnFailure.Broadcast(TEXT("Login did not start. Please configure EOS Connect-only: bUseNewLoginFlow=true, bUseEAS=false, bUseEOSConnect=true, and OnlineSubsystem.DefaultPlatformService=EOS."));
        SetReadyToDestroy();
        return;
    }
}

void ULoginWithDeviceIDAsyncAction::HandleLoginComplete(int32 LocalUserNum, bool bWasSuccessful, const FUniqueNetId& UserId, const FString& Error)
{
    IOnlineSubsystem* OSS = IOnlineSubsystem::Get();
    if (OSS)
    {
        IOnlineIdentityPtr Identity = OSS->GetIdentityInterface();
        if (Identity.IsValid() && LoginCompleteDelegateHandle.IsValid())
        {
            Identity->ClearOnLoginCompleteDelegate_Handle(LocalUserNum, LoginCompleteDelegateHandle);
            LoginCompleteDelegateHandle.Reset();
        }
    }

    if (bWasSuccessful)
    {
        OnSuccess.Broadcast(UserId.ToString());
    }
    else
    {
        OnFailure.Broadcast(Error);
    }

    SetReadyToDestroy();
}
