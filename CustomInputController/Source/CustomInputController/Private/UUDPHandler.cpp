// Fill out your copyright notice in the Description page of Project Settings.


#include "UUDPHandler.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"
#include "AudioStreamSettings.h"



UUDPHandler::UUDPHandler()
    : ListenSocket(nullptr)
    , UDPReceiver(nullptr)
    , bIsListening(false)
{
}

UUDPHandler::~UUDPHandler()
{
    StopUDPReceiver();
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
        int32 DesiredSize = 4 * 1024 * 1024; // 4MB default
        if (const UAudioStreamSettings* S = GetDefault<UAudioStreamSettings>())
        {
            DesiredSize = FMath::Max(65536, S->UdpRecvBufferBytes);
        }
        int32 AppliedSize = 0;
        if (!ListenSocket->SetReceiveBufferSize(DesiredSize, AppliedSize))
        {
            UE_LOG(LogTemp, Warning, TEXT("SetReceiveBufferSize failed, requested=%d applied=%d"), DesiredSize, AppliedSize);
        }
        else
        {
            UE_LOG(LogTemp, Log, TEXT("UDP recv buffer set to %d bytes (requested=%d)"), AppliedSize, DesiredSize);
        }
    }
    
    // 绑定端口
    FIPv4Address Addr = FIPv4Address::Any;
    FIPv4Endpoint Endpoint(Addr, Port);
    
    if (!ListenSocket->Bind(*Endpoint.ToInternetAddr()))
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to bind UDP socket to port %d"), Port);
        ListenSocket->Close();
        ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(ListenSocket);
        ListenSocket = nullptr;
        return false;
    }

    // 创建UDP接收器（降低Wait时间，提升Stop响应）
    UDPReceiver = new FUdpSocketReceiver(ListenSocket, FTimespan::FromMilliseconds(3), TEXT("UDP_Receiver"));
    UDPReceiver->OnDataReceived().BindUObject(this, &UUDPHandler::OnUDPMessageReceived);
    UDPReceiver->Start();

    bIsListening = true;
    UE_LOG(LogTemp, Log, TEXT("UDP Receiver started on port %d"), Port);
    return true;
}

void UUDPHandler::StopUDPReceiver()
{
    if (!bIsListening)
    {
        return;
    }

    bIsListening = false;


    // Close the socket to unblock any pending Wait/Recv on receiver thread, but don't destroy yet 
    if (ListenSocket)
    {
        ListenSocket->Close();
    }

    // Stop and delete the UDP receiver thread after unblocking
    if (UDPReceiver)
    {
        UDPReceiver->Stop();
        delete UDPReceiver;
        UDPReceiver = nullptr;
    }

    // Now safely destroy the socket object
    if (ListenSocket)
    {
        ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(ListenSocket);
        ListenSocket = nullptr;
    }

    UE_LOG(LogTemp, Log, TEXT("UDP Receiver stopped"));
}


void UUDPHandler::OnUDPMessageReceived(const FArrayReaderPtr& ArrayReaderPtr, const FIPv4Endpoint& EndPt)
{
    if (!ArrayReaderPtr.IsValid())
    {
        return;
    }

    // 先复制原始字节（用于二进制媒体包）
    TArray<uint8> Raw;
    const int32 DataSize = ArrayReaderPtr->TotalSize();

    // 调试：逐包日志（来源与长度）
    UE_LOG(LogTemp, Verbose, TEXT("[UDP] packet from %s, bytes=%d"), *EndPt.ToString(), DataSize);

    if (DataSize > 0)
    {
        Raw.SetNum(DataSize);
        ArrayReaderPtr->Serialize(Raw.GetData(), DataSize);

        // 广播二进制（C++）
        if (OnBinaryReceived.IsBound())
        {
            OnBinaryReceived.Broadcast(Raw, EndPt);
        }
    }

    // 兼容旧路径：尝试按UTF-8转字符串
    FString ReceivedString;
    if (Raw.Num() > 0)
    {
        if (Raw.Last() != 0) { Raw.Add(0); }
        ReceivedString = FString(UTF8_TO_TCHAR(reinterpret_cast<const char*>(Raw.GetData())));
    }

    AsyncTask(ENamedThreads::GameThread, [this, Msg = MoveTemp(ReceivedString)]()
    {
        if (OnDataReceived.IsBound())
        {
            OnDataReceived.Broadcast(Msg);
        }
        if (OnDataReceivedDynamic.IsBound())
        {
            OnDataReceivedDynamic.Broadcast(Msg);
        }
    });
}