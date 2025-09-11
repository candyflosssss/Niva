// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "UUDPHandler.h"
#include "InputPlusSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSubsystemUDPDataReceived, const FString&, ReceivedData);

// 双手数据委托 - 分别传递左手和右手数据
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FHandDataDelegateDynamic, 
    const TArray<FString>&, LeftHandKeys, 
    const TArray<FVector>&, LeftHandValues,
    const TArray<FString>&, RightHandKeys, 
    const TArray<FVector>&, RightHandValues);

DECLARE_MULTICAST_DELEGATE_FourParams(FHandDataDelegate, 
    const TArray<FString>&, 
    const TArray<FVector>&, 
    const TArray<FString>&, 
    const TArray<FVector>&);

/**
 * 手部数据结构
 */
USTRUCT(BlueprintType)
struct CUSTOMINPUTCONTROLLER_API FHandLandmarkData
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Hand Data")
    TArray<FString> Keys;

    UPROPERTY(BlueprintReadOnly, Category = "Hand Data")
    TArray<FVector> Values;

    UPROPERTY(BlueprintReadOnly, Category = "Hand Data")
    bool bIsValidData = false;

    FHandLandmarkData()
    {
        Keys.Empty();
        Values.Empty();
        bIsValidData = false;
    }

    void Reset()
    {
        Keys.Empty();
        Values.Empty();
        bIsValidData = false;
    }
};

/**
 * 
 */
UCLASS()
class CUSTOMINPUTCONTROLLER_API UInputPlusSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	
	// USubsystem interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/**
	 * 解析手部关键点数据字符串并返回双手数据
	 * @param DataString 输入的数据字符串
	 * @param OutLeftHand 输出的左手数据
	 * @param OutRightHand 输出的右手数据
	 */
	UFUNCTION(BlueprintCallable, Category = "Hand Landmark")
	void ParseHandLandmarkData(const FString& DataString, FHandLandmarkData& OutLeftHand, FHandLandmarkData& OutRightHand);

	/**
	 * 解析单手数据（保留兼容性）
	 * @param DataString 输入的数据字符串
	 * @return 手部关键点的 TMap，索引0-20对应21个关键点
	 */
	UFUNCTION(BlueprintCallable, Category = "Hand Landmark")
	TMap<FString, FVector> ParseSingleHandLandmarkData(const FString& DataString);

	/**
	 * 获取最新的双手数据
	 */
	UFUNCTION(BlueprintCallable, Category = "Hand Landmark")
	void GetLatestHandData(FHandLandmarkData& OutLeftHand, FHandLandmarkData& OutRightHand);

	/**
	 * 双手数据接收事件
	 */
	UPROPERTY(BlueprintAssignable, Category = "Hand Landmark")
	FHandDataDelegateDynamic OnHandDataReceivedDynamic;
	
	FHandDataDelegate OnHandDataReceived;

	/**
	 * UDP数据接收事件
	 */
	UPROPERTY(BlueprintAssignable, Category = "UDP")
	FOnSubsystemUDPDataReceived OnUDPDataReceived;

private:
	/**
	 * UDP处理器
	 */
	UPROPERTY()
	UUDPHandler* UDPHandler;

	/**
	 * 缓存的左手数据
	 */
	UPROPERTY()
	FHandLandmarkData CachedLeftHandData;

	/**
	 * 缓存的右手数据
	 */
	UPROPERTY()
	FHandLandmarkData CachedRightHandData;

	/**
	 * 处理接收到的UDP数据
	 */
	UFUNCTION()
	void OnUDPDataReceivedInternal(const FString& ReceivedData);

	/**
	 * 解析单个手部数据
	 */
	FHandLandmarkData ParseSingleHandData(const TArray<FString>& Parts, int32 StartIndex);

	const TArray<FString> HandLandmarkNames = {
		TEXT("WRIST"),
		TEXT("THUMB_CMC"),
		TEXT("THUMB_MCP"),
		TEXT("THUMB_IP"),
		TEXT("THUMB_TIP"),
		TEXT("INDEX_FINGER_MCP"),
		TEXT("INDEX_FINGER_PIP"),
		TEXT("INDEX_FINGER_DIP"),
		TEXT("INDEX_FINGER_TIP"),
		TEXT("MIDDLE_FINGER_MCP"),
		TEXT("MIDDLE_FINGER_PIP"),
		TEXT("MIDDLE_FINGER_DIP"),
		TEXT("MIDDLE_FINGER_TIP"),
		TEXT("RING_FINGER_MCP"),
		TEXT("RING_FINGER_PIP"),
		TEXT("RING_FINGER_DIP"),
		TEXT("RING_FINGER_TIP"),
		TEXT("PINKY_MCP"),
		TEXT("PINKY_PIP"),
		TEXT("PINKY_DIP"),
		TEXT("PINKY_TIP")
	};
};