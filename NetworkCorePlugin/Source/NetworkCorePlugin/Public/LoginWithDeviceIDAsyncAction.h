// Copyright

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "LoginWithDeviceIDAsyncAction.generated.h"

class UNivaOnlineSubsystem;
class FUniqueNetId;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FLoginWithDeviceIDSuccess, FString, UserId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FLoginWithDeviceIDFailure, FString, ErrorMessage);

UCLASS()
class NETWORKCOREPLUGIN_API ULoginWithDeviceIDAsyncAction : public UBlueprintAsyncActionBase
{
    GENERATED_BODY()

public:
    // 工厂方法
    UFUNCTION(BlueprintCallable, Category = "NivaOnline|Login", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject"))
    static ULoginWithDeviceIDAsyncAction* LoginWithDeviceIDAsync(UObject* WorldContextObject);

    // UBlueprintAsyncActionBase
    virtual void Activate() override;

public:
    UPROPERTY(BlueprintAssignable)
    FLoginWithDeviceIDSuccess OnSuccess;

    UPROPERTY(BlueprintAssignable)
    FLoginWithDeviceIDFailure OnFailure;

private:
    void HandleLoginComplete(int32 LocalUserNum, bool bWasSuccessful, const FUniqueNetId& UserId, const FString& Error);

private:
    TWeakObjectPtr<UObject> WorldContextObject;
    FDelegateHandle LoginCompleteDelegateHandle;
};
