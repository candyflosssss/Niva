#include "Input/CICGazeTrackingSubsystem.h"
#include "Core/CICRuntimeSettings.h"
#include "Input/CICGazeTrackingSettings.h"
#include "Common/UdpSocketBuilder.h"
#include "Common/UdpSocketReceiver.h"
#include "Serialization/ArrayReader.h"
#include "Async/Async.h"
#include "HAL/CriticalSection.h"
#include "Misc/ScopeLock.h"

void UCICGazeTrackingSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	
	const UCICRuntimeSettings* RuntimeSettings = UCICRuntimeSettings::Get();
	const bool bAutoStartListener = RuntimeSettings ? RuntimeSettings->bAutoStartGazeListener : true;
	int32 Port = RuntimeSettings ? RuntimeSettings->GazeUdpPort : 8888;
	const UCICGazeTrackingSettings* Settings = GetDefault<UCICGazeTrackingSettings>();
	if ((!RuntimeSettings || Port <= 0) && Settings)
	{
		Port = Settings->GazeCleanupPort;
	}

	if (bAutoStartListener)
	{
		StartListener(Port);
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("CICGazeTrackingSubsystem: 按配置跳过自动启动注视监听，端口=%d"), Port);
	}
}

void UCICGazeTrackingSubsystem::Deinitialize()
{
	StopListener();
	Super::Deinitialize();
}

void UCICGazeTrackingSubsystem::StartListener(int32 Port)
{
	StopListener();

	FIPv4Endpoint Endpoint(FIPv4Address::Any, Port);
	
	Socket = FUdpSocketBuilder(TEXT("CICGazeTrackingSocket"))
		.AsNonBlocking()
		.AsReusable()
		.BoundToEndpoint(Endpoint)
		.WithReceiveBufferSize(2 * 1024 * 1024);

	if (Socket)
	{
		UDPReceiver = MakeShared<FUdpSocketReceiver>(Socket, FTimespan::FromMilliseconds(1), TEXT("CICGazeTrackingUDPReceiver"));
		UDPReceiver->OnDataReceived().BindUObject(this, &UCICGazeTrackingSubsystem::HandleDataReceived);
		UDPReceiver->Start();
		UE_LOG(LogTemp, Log, TEXT("CICGazeTrackingSubsystem: Listener started on port %d"), Port);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("CICGazeTrackingSubsystem: Failed to create socket on port %d"), Port);
	}
}

void UCICGazeTrackingSubsystem::StopListener()
{
	if (UDPReceiver.IsValid())
	{
		UDPReceiver->Stop();
		UDPReceiver.Reset();
	}

	if (Socket)
	{
		Socket->Close();
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket);
		Socket = nullptr;
	}
}

FCICGazeData UCICGazeTrackingSubsystem::GetLatestGazeData() const
{
	FScopeLock Lock(&DataLock);
	return LatestGazeData;
}

int64 UCICGazeTrackingSubsystem::GetLatestUnixTimestamp() const
{
	FScopeLock Lock(&DataLock);
	return LatestUnixTimestamp;
}

float UCICGazeTrackingSubsystem::GetLatestLatencyMs() const
{
	FScopeLock Lock(&DataLock);
	if (LatestUnixTimestamp <= 0)
	{
		return -1.0f;
	}

	// LatestUnixTimestamp is in UNIX ms (as provided by the sender). We compute now in the same scale.
	const FDateTime Now = FDateTime::UtcNow();
	const int64 NowUnixMs = (Now - FDateTime(1970, 1, 1)).GetTotalMilliseconds();
	return static_cast<float>(NowUnixMs - LatestUnixTimestamp);
}

void UCICGazeTrackingSubsystem::HandleDataReceived(const TSharedPtr<FArrayReader, ESPMode::ThreadSafe>& ArrayReaderPtr, const FIPv4Endpoint& EndPoint)
{
	if (!ArrayReaderPtr.IsValid())
	{
		return;
	}

	int32 NumBytes = ArrayReaderPtr->Num();
	const uint8* Data = ArrayReaderPtr->GetData();
    
	// Assuming UTF8
	FUTF8ToTCHAR Converted((const char*)Data, NumBytes);
	FString ReceivedString(Converted.Length(), Converted.Get());

	// Clean up potential whitespace
	ReceivedString.TrimStartAndEndInline();

	// Parse: Timestamp/<pupilWorld>/LeftEyeX,LeftEyeY,LeftEyeZ/RightEyeX,RightEyeY,RightEyeZ
	// Example: 1769162996041/<pupilWorld>/139.476,3.823,-2.119/139.476,0.439,-8.247
	TArray<FString> Parts;
	ReceivedString.ParseIntoArray(Parts, TEXT("/"), true);

	// Expect 4 parts: [0]=timestamp, [1]=coord space tag (ignored), [2]=left, [3]=right
	if (Parts.Num() != 4)
	{
		return;
	}

	int64 Timestamp = FCString::Atoi64(*Parts[0]);

	FCICGazeData NewData;
	NewData.Timestamp = Timestamp;
    
	bool bParsedLeft = false;
	bool bParsedRight = false;
    
	TArray<FString> LeftEyeParts;
	Parts[2].ParseIntoArray(LeftEyeParts, TEXT(","), true);
	if (LeftEyeParts.Num() == 3)
	{
		NewData.LeftEye.X = FCString::Atof(*LeftEyeParts[0]);
		NewData.LeftEye.Y = FCString::Atof(*LeftEyeParts[1]);
		NewData.LeftEye.Z = FCString::Atof(*LeftEyeParts[2]);
		bParsedLeft = true;
	}

	TArray<FString> RightEyeParts;
	Parts[3].ParseIntoArray(RightEyeParts, TEXT(","), true);
	if (RightEyeParts.Num() == 3)
	{
		NewData.RightEye.X = FCString::Atof(*RightEyeParts[0]);
		NewData.RightEye.Y = FCString::Atof(*RightEyeParts[1]);
		NewData.RightEye.Z = FCString::Atof(*RightEyeParts[2]);
		bParsedRight = true;
	}

	if (bParsedLeft && bParsedRight)
	{
		bool bShouldScheduleBroadcast = false;
		{
			FScopeLock Lock(&DataLock);
			if (Timestamp <= LatestGazeData.Timestamp)
			{
				// Old data
				return;
			}
			LatestGazeData = NewData;
			LatestUnixTimestamp = Timestamp; // store raw unix timestamp

			// if no broadcast task queued, we queue one now to coalesce multiple updates
			if (!bBroadcastScheduled)
			{
				bBroadcastScheduled = true;
				bShouldScheduleBroadcast = true;
			}
		}

		if (bShouldScheduleBroadcast)
		{
			AsyncTask(ENamedThreads::GameThread, [this]()
			{
				FCICGazeData DataToBroadcast;
				{
					FScopeLock Lock(&DataLock);
					DataToBroadcast = LatestGazeData; // grab the newest only
					bBroadcastScheduled = false; // allow scheduling next task
				}
				OnGazeDataReceived.Broadcast(DataToBroadcast);
			});
		}
	}
}

void UCICGazeTrackingSubsystem::ClearPreviewTrails()
{
	// No-op after removing preview page
}
