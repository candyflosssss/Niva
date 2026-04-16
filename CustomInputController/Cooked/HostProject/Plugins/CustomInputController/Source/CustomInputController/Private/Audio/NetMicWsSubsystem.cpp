#include "Audio/NetMicWsSubsystem.h"
#include "Audio/NetMicWsComponent.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Async/Async.h"

void UNetMicWsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UE_LOG(LogTemp, Log, TEXT("NetMicWsSubsystem initialized in compatibility mode; live WebSocket ownership belongs to NetMicWsComponent."));
}

void UNetMicWsSubsystem::Deinitialize()
{
	StopMic();
	Super::Deinitialize();
}

void UNetMicWsSubsystem::StartByPost(const FString& HttpUrl, const FString& JsonBody)
{
	WarnCompatibilityUse(TEXT("StartByPost"));
	// 每次尝试都重置暂存区，并委托组件重连
	ResetBuffer();
	StopMic();

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
	WarnCompatibilityUse(TEXT("StartDirect"));
	ResetBuffer();
	if (UNetMicWsComponent* Component = GetCompatibilityComponent())
	{
		Component->Connect(WsUrl);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("NetMicWsSubsystem::StartDirect has no registered NetMicWsComponent to delegate to."));
	}
}

void UNetMicWsSubsystem::StopMic()
{
	WarnCompatibilityUse(TEXT("StopMic"));
	if (UNetMicWsComponent* Component = GetCompatibilityComponent())
	{
		Component->Disconnect();
	}
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

void UNetMicWsSubsystem::WarnCompatibilityUse(const TCHAR* FunctionName) const
{
	UE_LOG(LogTemp, Warning, TEXT("NetMicWsSubsystem::%s is running in compatibility mode. Prefer UNetMicWsComponent for live WebSocket ownership."), FunctionName);
}

UNetMicWsComponent* UNetMicWsSubsystem::GetCompatibilityComponent() const
{
	return CompatibilityComponent.Get();
}

void UNetMicWsSubsystem::RegisterCompatibilityComponent(UNetMicWsComponent* InComponent)
{
	if (!IsValid(InComponent))
	{
		return;
	}

	if (!CompatibilityComponent.IsValid())
	{
		CompatibilityComponent = InComponent;
		UE_LOG(LogTemp, Log, TEXT("NetMicWsSubsystem registered compatibility component: %s"), *InComponent->GetName());
	}
}

void UNetMicWsSubsystem::UnregisterCompatibilityComponent(UNetMicWsComponent* InComponent)
{
	if (CompatibilityComponent.Get() == InComponent)
	{
		CompatibilityComponent.Reset();
	}
}

void UNetMicWsSubsystem::MirrorAudioFrame(const TArray<uint8>& Data)
{
	if (Data.Num() == 0)
	{
		return;
	}

	FPacket P;
	P.TimeSec = FPlatformTime::Seconds();
	P.Bytes = Data;

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

	OnAudioBinary.Broadcast(Data);
}
