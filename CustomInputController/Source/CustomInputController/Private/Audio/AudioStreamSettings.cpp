#include "Audio/AudioStreamSettings.h"
#include "Misc/ConfigCacheIni.h"

const UAudioStreamSettings* UAudioStreamSettings::Get()
{
    return GetDefault<UAudioStreamSettings>();
}

void UAudioStreamSettings::PostInitProperties()
{
    Super::PostInitProperties();
    // Force load config to ensure values from DefaultGame.ini are applied early
    LoadConfig();

    // Print only core, actively used settings to avoid clutter
    UE_LOG(LogTemp, Log, TEXT("[AudioStreamSettings] Loaded: Ws=%s://%s%s SampleRate=%d Channels=%d FrameMs=%d StatsLive=%d"),
        *DefaultWsScheme, *DefaultWsHost, *DefaultWsPathPrefix,
        DefaultSampleRate, DefaultChannels, FrameDurationMs,
        bStatsLiveLogDefault?1:0);
}
