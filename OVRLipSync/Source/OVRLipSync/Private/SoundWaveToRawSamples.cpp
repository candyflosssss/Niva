#include "SoundWaveToRawSamples.h"
#include "Sound/SoundWave.h"
#include "Misc/ScopedSlowTask.h"
#include "Async/Async.h"
#include "Misc/Paths.h"
#include "Audio.h"
#include "Misc/MessageDialog.h"
#include "Logging/MessageLog.h"
#include "Logging/LogMacros.h"
#include "AudioDecompress.h"
#include "Engine/Engine.h"
#include "HAL/UnrealMemory.h" // For FMemory::Memcpy

DEFINE_LOG_CATEGORY_STATIC(LogSoundWaveToRawSamples, Log, All);

bool USoundWaveToRawSamples::ConvertSoundWaveToRawSamples(USoundWave* SoundWave, TArray<uint8>& OutRawSamples)
{
    if (!SoundWave)
    {
        UE_LOG(LogSoundWaveToRawSamples, Error, TEXT("Invalid SoundWave pointer."));
        return false;
    }

    UE_LOG(LogSoundWaveToRawSamples, Log, TEXT("SoundWave loaded with duration: %f seconds"), SoundWave->Duration);

    // 确保音频数据已加载到内存�?
    SoundWave->InitAudioResource(FName("SoundWaveToRawSamples"));

    const uint8* WaveData = SoundWave->GetResourceData();
    int32 WaveDataSize = SoundWave->GetResourceSize();

    if (!WaveData || WaveDataSize <= 0)
    {
        UE_LOG(LogSoundWaveToRawSamples, Error, TEXT("Invalid wave data or wave data size."));
        UE_LOG(LogSoundWaveToRawSamples, Log, TEXT("WaveData: %p, WaveDataSize: %d"), WaveData, WaveDataSize);

        // 打印更多�?SoundWave 属�?
        UE_LOG(LogSoundWaveToRawSamples, Log, TEXT("SoundWave RawData size: %d"), SoundWave->GetResourceSize());

        return false;
    }

    UE_LOG(LogSoundWaveToRawSamples, Log, TEXT("WaveDataSize: %d"), WaveDataSize);

    FWaveModInfo WaveInfo;
    if (WaveInfo.ReadWaveInfo((uint8*)WaveData, WaveDataSize)) // Cast to non-const for WaveInfo
    {
        int32 NumChannels = *WaveInfo.pChannels;
        int32 SampleRate = *WaveInfo.pSamplesPerSec;
        int32 PCMDataSize = WaveInfo.SampleDataSize / sizeof(int16_t);
        const int16_t* PCMData = reinterpret_cast<const int16_t*>(WaveData + 44); // Skip the wave header

        OutRawSamples.SetNumUninitialized(PCMDataSize * sizeof(int16_t));
        FMemory::Memcpy(OutRawSamples.GetData(), PCMData, PCMDataSize * sizeof(int16_t));

        return true;
    }
    else
    {
        UE_LOG(LogSoundWaveToRawSamples, Error, TEXT("Failed to read wave info."));
        return false;
    }
}