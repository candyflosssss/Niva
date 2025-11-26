#include "Audio/NetMicWsSubsystem.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Modules/ModuleManager.h"
#include "WebSocketsModule.h"

void UNetMicWsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UNetMicWsSubsystem::Deinitialize()
{
	StopMic();
	Super::Deinitialize();
}

void UNetMicWsSubsystem::StartByPost(const FString& HttpUrl, const FString& JsonBody)
{
	// 每次尝试都重置暂存区，并断开旧连接
	ResetBuffer();
	CloseWebSocket();

	FHttpModule& Http = FHttpModule::Get();
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = Http.CreateRequest();
	Req->SetURL(HttpUrl);
	Req->SetVerb(TEXT("POST"));
	Req->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Req->SetContentAsString(JsonBody);
	Req->OnProcessRequestComplete().BindLambda([this](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSuccess)
	{
		if (!bSuccess || !Response.IsValid())
		{
			UE_LOG(LogTemp, Error, TEXT("NetMic POST failed"));
			return;
		}
		const int32 Code = Response->GetResponseCode();
		if (Code < 200 || Code >= 300)
		{
			UE_LOG(LogTemp, Error, TEXT("NetMic POST HTTP %d: %s"), Code, *Response->GetContentAsString());
			return;
		}
		FString WsUrl;
		{
			TSharedPtr<FJsonObject> Obj;
			const FString Body = Response->GetContentAsString();
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
			if (FJsonSerializer::Deserialize(Reader, Obj) && Obj.IsValid())
			{
				if (Obj->HasTypedField<EJson::String>(TEXT("ws"))) WsUrl = Obj->GetStringField(TEXT("ws"));
				else if (Obj->HasTypedField<EJson::String>(TEXT("ws_url"))) WsUrl = Obj->GetStringField(TEXT("ws_url"));
				else if (Obj->HasTypedField<EJson::String>(TEXT("url"))) WsUrl = Obj->GetStringField(TEXT("url"));
			}
		}
		if (WsUrl.IsEmpty())
		{
			UE_LOG(LogTemp, Error, TEXT("NetMic POST succeeded but no ws url in response"));
			return;
		}
		// 连接 WebSocket
		AsyncTask(ENamedThreads::GameThread, [this, WsUrl]() { StartDirect(WsUrl); });
	});
	Req->ProcessRequest();
}

void UNetMicWsSubsystem::StartDirect(const FString& WsUrl)
{
	ResetBuffer();
	CloseWebSocket();
	ConnectWebSocket(WsUrl);
}

void UNetMicWsSubsystem::StopMic()
{
	CloseWebSocket();
	ResetBuffer();
}

float UNetMicWsSubsystem::GetBufferedSeconds() const
{
	FScopeLock Lock(&BufferCS);
	if (Ring.Num() == 0) return 0.f;
	double Now = FPlatformTime::Seconds();
	double Oldest = Ring[0].TimeSec;
	return static_cast<float>(Now - Oldest);
}

void UNetMicWsSubsystem::ResetBuffer()
{
	FScopeLock Lock(&BufferCS);
	Ring.Reset();
}

void UNetMicWsSubsystem::ConnectWebSocket(const FString& Url)
{
	if (!FModuleManager::Get().IsModuleLoaded("WebSockets"))
	{
		FModuleManager::Get().LoadModuleChecked<IModuleInterface>("WebSockets");
	}

	TSharedPtr<IWebSocket> NewSocket = FWebSocketsModule::Get().CreateWebSocket(Url);
	NewSocket->OnConnected().AddUObject(this, &UNetMicWsSubsystem::OnWsConnected);
	NewSocket->OnConnectionError().AddUObject(this, &UNetMicWsSubsystem::OnWsError);
	NewSocket->OnClosed().AddUObject(this, &UNetMicWsSubsystem::OnWsClosed);
	NewSocket->OnMessage().AddUObject(this, &UNetMicWsSubsystem::OnWsText);
	NewSocket->OnRawMessage().AddUObject(this, &UNetMicWsSubsystem::OnWsBinary);

	Socket = NewSocket;
	Socket->Connect();
}

void UNetMicWsSubsystem::CloseWebSocket()
{
	if (Socket.IsValid())
	{
		Socket->Close();
		Socket.Reset();
	}
}

void UNetMicWsSubsystem::OnWsConnected()
{
	UE_LOG(LogTemp, Log, TEXT("NetMic WS connected"));
}

void UNetMicWsSubsystem::OnWsError(const FString& Error)
{
	UE_LOG(LogTemp, Error, TEXT("NetMic WS error: %s"), *Error);
}

void UNetMicWsSubsystem::OnWsClosed(int32 StatusCode, const FString& Reason, bool bWasClean)
{
	UE_LOG(LogTemp, Warning, TEXT("NetMic WS closed: %d %s"), StatusCode, *Reason);
}

void UNetMicWsSubsystem::OnWsText(const FString& Message)
{
	// 可选：处理控制信令
}

void UNetMicWsSubsystem::OnWsBinary(const void* Data, SIZE_T Size, SIZE_T /*BytesRemaining*/)
{
	if (Size == 0) return;

	FPacket P;
	P.TimeSec = FPlatformTime::Seconds();
	P.Bytes.SetNumUninitialized(Size);
	FMemory::Memcpy(P.Bytes.GetData(), Data, Size);

	{
		FScopeLock Lock(&BufferCS);
		Ring.Add(MoveTemp(P));
		// 修剪至时间上限
		double Now = FPlatformTime::Seconds();
		while (Ring.Num() > 0 && (Now - Ring[0].TimeSec) > MaxBufferSeconds)
		{
			Ring.RemoveAt(0);
		}
	}

	// 内部分发：仅广播蓝图，网络转发留待后续实现
	if (bForwardEnabled && OnAudioBinary.IsBound())
	{
		OnAudioBinary.Broadcast(Ring.Last().Bytes);
	}
}
