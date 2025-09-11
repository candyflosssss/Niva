// Fill out your copyright notice in the Description page of Project Settings.


#include "UUDPHandler.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"



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
    UDPReceiver = new FUdpSocketReceiver(ListenSocket, FTimespan::FromMilliseconds(100), TEXT("UDP_Receiver"));
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

    FString ReceivedString;
    
    // 获取原始字节数据
    TArray<uint8> ReceivedData;
    const int32 DataSize = ArrayReaderPtr->TotalSize();
    
    if (DataSize > 0)
    {
        // 从ArrayReader读取原始字节
        ReceivedData.SetNum(DataSize);
        ArrayReaderPtr->Serialize(ReceivedData.GetData(), DataSize);
        
        // 确保数据以null结尾（如果还没有的话）
        if (ReceivedData.Last() != 0)
        {
            ReceivedData.Add(0);
        }
        
        // 将UTF-8字节转换为FString
        const char* CharData = reinterpret_cast<const char*>(ReceivedData.GetData());
        ReceivedString = FString(UTF8_TO_TCHAR(CharData));
        
        // 调试输出
        // UE_LOG(LogTemp, Log, TEXT("UDP接收到 %d 字节数据，转换后字符串长度: %d"), 
        //        DataSize, ReceivedString.Len());
    }
    
    // 将广播切到GameThread，避免在接收线程中调用蓝图动态委托
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