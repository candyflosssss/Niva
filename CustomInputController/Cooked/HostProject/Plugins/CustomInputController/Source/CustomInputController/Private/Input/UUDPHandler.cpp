// Fill out your copyright notice in the Description page of Project Settings.


#include "Input/UUDPHandler.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"
// #include "Common/UdpSocketReceiver.h" // removed threaded receiver
#include "HAL/PlatformProcess.h"
#include "Misc/CoreDelegates.h"
#include "Containers/Queue.h"
#include "Templates/SharedPointer.h"
#include "Async/Async.h"
#include "Engine/Engine.h"
#include "Containers/Ticker.h"

UUDPHandler::UUDPHandler()
    : ListenSocket(nullptr)
    , TickerHandle()
    , bIsListening(false)
{
    // 在引擎退出前，我们会先停止接收器，避免析构阶段回调/线程竞态
    if (!PreExitHandle.IsValid())
    {
        PreExitHandle = FCoreDelegates::OnPreExit.AddUObject(this, &UUDPHandler::StopUDPReceiver);
    }
}

UUDPHandler::~UUDPHandler()
{
    StopUDPReceiver();
    if (PreExitHandle.IsValid())
    {
        FCoreDelegates::OnPreExit.Remove(PreExitHandle);
        PreExitHandle.Reset();
    }
}

void UUDPHandler::BeginDestroy()
{
    StopUDPReceiver();
    Super::BeginDestroy();
}

bool UUDPHandler::StartUDPReceiver(int32 Port)
{
    if (bIsListening)
    {
        UE_LOG(LogTemp, Warning, TEXT("UDP Receiver is already listening"));
        return false;
    }

    ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    if (!SocketSubsystem)
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to get socket subsystem"));
        return false;
    }

    // 创建UDP Socket
    ListenSocket = SocketSubsystem->CreateSocket(NAME_DGram, TEXT("UDP_Receiver"), false);
    if (!ListenSocket)
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to create UDP socket"));
        return false;
    }

    // 微调：允许端口复用、增大接收缓冲区，提升抗抖动能力
    {
        ListenSocket->SetReuseAddr(true);
        ListenSocket->SetNonBlocking(true);

        int32 DesiredSize = 4 * 1024 * 1024; // 4MB
        int32 AppliedSize = 0;
        if (!ListenSocket->SetReceiveBufferSize(DesiredSize, AppliedSize))
        {
            UE_LOG(LogTemp, Warning, TEXT("SetReceiveBufferSize failed, requested=%d applied=%d"), DesiredSize, AppliedSize);
        }

        FIPv4Endpoint Endpoint(FIPv4Address::Any, Port);
        if (!ListenSocket->Bind(*Endpoint.ToInternetAddr()))
        {
            UE_LOG(LogTemp, Error, TEXT("Failed to bind UDP socket to port %d"), Port);
            ListenSocket->Close();
            SocketSubsystem->DestroySocket(ListenSocket);
            ListenSocket = nullptr;
            return false;
        }
    }

    // 游戏线程轮询：~100Hz（0.01s），非阻塞
    TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UUDPHandler::PollSocket), 0.01f);

    bIsListening = true;
    UE_LOG(LogTemp, Log, TEXT("UDP poller started on port %d"), Port);
    return true;
}

void UUDPHandler::StopUDPReceiver()
{
    if (!bIsListening && !ListenSocket && !TickerHandle.IsValid())
    {
        return;
    }

    bIsListening = false;

    if (TickerHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
        TickerHandle.Reset();
    }

    if (ListenSocket)
    {
        ListenSocket->Close();
        if (ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM))
        {
            SocketSubsystem->DestroySocket(ListenSocket);
        }
        ListenSocket = nullptr;
    }

    // 清空蓝图/动态委托，避免GC阶段回调
    OnDataReceived.Clear();
    OnBinaryReceived.Clear();
    OnDataReceivedDynamic.Clear();

    UE_LOG(LogTemp, Log, TEXT("UDP Receiver stopped"));
}

bool UUDPHandler::PollSocket(float /*DeltaTime*/)
{
    if (!bIsListening || !ListenSocket)
    {
        return true; // keep ticker; Stop will remove
    }

    uint32 PendingSize = 0;
    while (ListenSocket->HasPendingData(PendingSize))
    {
        TArray<uint8> Buffer;
        Buffer.SetNumUninitialized(FMath::Min<uint32>(PendingSize, 65535));
        int32 BytesRead = 0;
        TSharedRef<FInternetAddr> Sender = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();
        if (ListenSocket->RecvFrom(Buffer.GetData(), Buffer.Num(), BytesRead, *Sender))
        {
            Buffer.SetNum(BytesRead);

            // 二进制广播
            if (OnBinaryReceived.IsBound())
            {
                uint32 OutIp = 0; 
                int32 OutPort = 0;
                Sender->GetIp(OutIp);
                OutPort = Sender->GetPort();
                const FIPv4Endpoint Remote(FIPv4Address(OutIp), OutPort);
                OnBinaryReceived.Broadcast(Buffer, Remote);
            }

            // 文本广播（UTF-8 尝试）
            if (OnDataReceived.IsBound() || OnDataReceivedDynamic.IsBound())
            {
                FString Msg;
                if (BytesRead > 0)
                {
                    if (Buffer.Last() != 0) { Buffer.Add(0); }
                    Msg = FString(UTF8_TO_TCHAR(reinterpret_cast<const char*>(Buffer.GetData())));
                }

                if (OnDataReceived.IsBound())
                {
                    OnDataReceived.Broadcast(Msg);
                }
                if (OnDataReceivedDynamic.IsBound())
                {
                    OnDataReceivedDynamic.Broadcast(Msg);
                }
            }
        }
        else
        {
            break; // recv失败则下一帧再试
        }
    }

    return true; // keep ticking
}

void UUDPHandler::OnUDPMessageReceived(const FArrayReaderPtr& /*ArrayReaderPtr*/, const FIPv4Endpoint& /*EndPt*/)
{
    // 已迁移为 PollSocket，不再使用该回调
}
