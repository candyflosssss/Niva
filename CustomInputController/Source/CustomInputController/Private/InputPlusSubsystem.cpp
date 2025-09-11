// Fill out your copyright notice in the Description page of Project Settings.

#include "InputPlusSubsystem.h"

void UInputPlusSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    
    // 创建UDP处理器并自动启动端口8092
    UDPHandler = NewObject<UUDPHandler>(this);
    if (UDPHandler)
    {
        UDPHandler->OnDataReceivedDynamic.AddDynamic(this, &UInputPlusSubsystem::OnUDPDataReceivedInternal);
        UDPHandler->StartUDPReceiver(8092);
    }

    // 初始化缓存数据
    CachedLeftHandData.Reset();
    CachedRightHandData.Reset();
}

void UInputPlusSubsystem::Deinitialize()
{
    if (UDPHandler)
    {
        UDPHandler->StopUDPReceiver();
        UDPHandler = nullptr;
    }
    
    Super::Deinitialize();
}

void UInputPlusSubsystem::ParseHandLandmarkData(const FString& DataString, FHandLandmarkData& OutLeftHand, FHandLandmarkData& OutRightHand)
{
    OutLeftHand.Reset();
    OutRightHand.Reset();

    TArray<FString> Parts;
    DataString.ParseIntoArray(Parts, TEXT("/"));

    if (Parts.Num() < 3)
    {
        UE_LOG(LogTemp, Warning, TEXT("Invalid hand data format: %s"), *DataString);
        return;
    }

    // 数据格式应该是：protocol/version/hand_type/x1,y1,z1/x2,y2,z2/...
    // 对于双手数据，可能的格式：protocol/version/both/left_data.../right_data...
    // 或者分别接收：protocol/version/left/data... 和 protocol/version/right/data...

    FString HandType = Parts[2];
    
    if (HandType == TEXT("left"))
    {
        // 处理左手数据
        OutLeftHand = ParseSingleHandData(Parts, 3);
        OutLeftHand.bIsValidData = true;
        CachedLeftHandData = OutLeftHand;
        
        UE_LOG(LogTemp, Log, TEXT("Parsed Left Hand Data: %s"), *DataString);
    }
    else if (HandType == TEXT("right"))
    {
        // 处理右手数据
        OutRightHand = ParseSingleHandData(Parts, 3);
        OutRightHand.bIsValidData = true;
        CachedRightHandData = OutRightHand;
        
        UE_LOG(LogTemp, Log, TEXT("Parsed Right Hand Data: %s"), *DataString);
    }
    else if (HandType == TEXT("both"))
    {
        // 处理双手数据 - 假设格式为：protocol/version/both/left_x1,y1,z1/.../right_x1,y1,z1/...
        // 需要找到左手和右手数据的分界点
        int32 LeftHandEndIndex = 3 + HandLandmarkNames.Num(); // 左手数据结束位置
        
        if (Parts.Num() >= LeftHandEndIndex + HandLandmarkNames.Num())
        {
            OutLeftHand = ParseSingleHandData(Parts, 3);
            OutLeftHand.bIsValidData = true;
            CachedLeftHandData = OutLeftHand;
            
            OutRightHand = ParseSingleHandData(Parts, LeftHandEndIndex);
            OutRightHand.bIsValidData = true;
            CachedRightHandData = OutRightHand;
            
            UE_LOG(LogTemp, Log, TEXT("Parsed Both Hands Data: %s"), *DataString);
        }
    }

    // 广播双手数据事件
    OnHandDataReceivedDynamic.Broadcast(
        CachedLeftHandData.Keys, CachedLeftHandData.Values,
        CachedRightHandData.Keys, CachedRightHandData.Values
    );
    
    OnHandDataReceived.Broadcast(
        CachedLeftHandData.Keys, CachedLeftHandData.Values,
        CachedRightHandData.Keys, CachedRightHandData.Values
    );
}

TMap<FString, FVector> UInputPlusSubsystem::ParseSingleHandLandmarkData(const FString& DataString)
{
    TMap<FString, FVector> LandmarkMap;
    
    TArray<FString> Parts;
    DataString.ParseIntoArray(Parts, TEXT("/"));

    if (Parts.Num() < 3)
    {
        return LandmarkMap;
    }

    // 跳过前两个元素，从第3个开始解析坐标
    for (int32 i = 3; i < Parts.Num() && (i - 3) < HandLandmarkNames.Num(); ++i)
    {
        TArray<FString> Coords;
        Parts[i].ParseIntoArray(Coords, TEXT(","));
        
        if (Coords.Num() == 3)
        {
            float X = FCString::Atof(*Coords[0]);
            float Y = FCString::Atof(*Coords[1]);
            float Z = FCString::Atof(*Coords[2]);
            
            LandmarkMap.Add(HandLandmarkNames[i-3], FVector(X, Y, Z));
        }
    }

    UE_LOG(LogTemp, Log, TEXT("Parsed Single Hand Landmark Data: %s"), *DataString);
    return LandmarkMap;
}

void UInputPlusSubsystem::GetLatestHandData(FHandLandmarkData& OutLeftHand, FHandLandmarkData& OutRightHand)
{
    OutLeftHand = CachedLeftHandData;
    OutRightHand = CachedRightHandData;
}

void UInputPlusSubsystem::OnUDPDataReceivedInternal(const FString& ReceivedData)
{
    FHandLandmarkData LeftHandData;
    FHandLandmarkData RightHandData;
    
    ParseHandLandmarkData(ReceivedData, LeftHandData, RightHandData);
    
    // UDP数据接收事件广播
    OnUDPDataReceived.Broadcast(ReceivedData);
}

FHandLandmarkData UInputPlusSubsystem::ParseSingleHandData(const TArray<FString>& Parts, int32 StartIndex)
{
    FHandLandmarkData HandData;
    
    // 确保有足够的数据点
    int32 ExpectedEndIndex = StartIndex + HandLandmarkNames.Num();
    if (Parts.Num() < ExpectedEndIndex)
    {
        UE_LOG(LogTemp, Warning, TEXT("Insufficient hand landmark data. Expected %d points, got %d"), 
               HandLandmarkNames.Num(), Parts.Num() - StartIndex);
        return HandData;
    }

    // 解析从StartIndex开始的手部关键点数据
    for (int32 i = 0; i < HandLandmarkNames.Num() && (StartIndex + i) < Parts.Num(); ++i)
    {
        TArray<FString> Coords;
        Parts[StartIndex + i].ParseIntoArray(Coords, TEXT(","));
        
        if (Coords.Num() == 3)
        {
            float X = FCString::Atof(*Coords[0]);
            float Y = FCString::Atof(*Coords[1]);
            float Z = FCString::Atof(*Coords[2]);
            
            HandData.Keys.Add(HandLandmarkNames[i]);
            HandData.Values.Add(FVector(X, Y, Z));
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("Invalid coordinate format at index %d: %s"), 
                   StartIndex + i, *Parts[StartIndex + i]);
        }
    }

    HandData.bIsValidData = (HandData.Keys.Num() == HandLandmarkNames.Num());
    return HandData;
}