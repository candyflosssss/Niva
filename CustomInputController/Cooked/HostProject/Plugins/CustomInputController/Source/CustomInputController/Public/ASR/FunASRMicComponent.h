#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AudioCaptureCore.h"
#include "FunASRMicComponent.generated.h"

/**
 * Component to capture microphone audio and send to FunASR Subsystem
 */
UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class CUSTOMINPUTCONTROLLER_API UFunASRMicComponent : public UActorComponent
{
	GENERATED_BODY()

public:	
	UFunASRMicComponent();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:	
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Manually start the ASR service and microphone capture */
	UFUNCTION(BlueprintCallable, Category="FunASR")
	void Start();

	/** Manually stop the microphone capture */
	UFUNCTION(BlueprintCallable, Category="FunASR")
	void Stop();

private:
	// Audio Capture
	bool StartAudioCapture();
	void StopAudioCapture();
	void OnAudioCaptureData(const void* InAudioData, int32 InNumFrames, int32 InNumChannels, int32 InSampleRate, double InStreamTime, bool bOverflow);

	TUniquePtr<Audio::FAudioCapture> AudioCapture;
	
	// Buffering
	TArray<uint8> PendingAudioData;
	mutable FCriticalSection AudioBufferMutex;


	static UFunASRMicComponent* GlobalActiveMic;
};
