#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Networking.h"
#include "Sockets.h"
#include "Templates/SharedPointer.h"
#include "CICGazeTrackingSubsystem.generated.h"

USTRUCT(BlueprintType)
struct FCICGazeData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "GazeTracking")
	int64 Timestamp = 0;

	UPROPERTY(BlueprintReadOnly, Category = "GazeTracking")
	FVector LeftEye = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "GazeTracking")
	FVector RightEye = FVector::ZeroVector;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGazeDataReceived, const FCICGazeData&, NewGazeData);

UCLASS()
class CUSTOMINPUTCONTROLLER_API UCICGazeTrackingSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category = "GazeTracking")
	void StartListener(int32 Port = 8888);

	UFUNCTION(BlueprintCallable, Category = "GazeTracking")
	void StopListener();

	UFUNCTION(BlueprintPure, Category = "GazeTracking")
	FCICGazeData GetLatestGazeData() const;

	// Returns the latest UNIX timestamp (as received) stored on the subsystem
	UFUNCTION(BlueprintPure, Category = "GazeTracking")
	int64 GetLatestUnixTimestamp() const;

	// Returns current estimated end-to-end latency in milliseconds based on latest UNIX timestamp; returns <0 if unavailable
	UFUNCTION(BlueprintPure, Category = "GazeTracking")
	float GetLatestLatencyMs() const;

	UPROPERTY(BlueprintAssignable, Category = "GazeTracking")
	FOnGazeDataReceived OnGazeDataReceived;

	UFUNCTION(BlueprintCallable, Category = "GazeTracking|Preview")
	void ClearPreviewTrails();

private:
	void HandleDataReceived(const FArrayReaderPtr& ArrayReaderPtr, const FIPv4Endpoint& EndPoint);
	
	FSocket* Socket = nullptr;
	TSharedPtr<class FUdpSocketReceiver> UDPReceiver;
	
	mutable FCriticalSection DataLock;
	FCICGazeData LatestGazeData;

	// Stores the latest received UNIX timestamp (same unit as incoming data)
	UPROPERTY(BlueprintReadOnly, Category = "GazeTracking", meta = (AllowPrivateAccess = "true"))
	int64 LatestUnixTimestamp = 0;

	// Prevent piling up many GameThread tasks; true when a broadcast task has been queued
	bool bBroadcastScheduled = false;
};
