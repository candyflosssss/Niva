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
    UE_LOG(LogTemp, Log, TEXT("[AudioStreamSettings] Loaded: DefaultServerIp=%s MediaUdpPort=%d DefaultWsHost=%s DefaultWsScheme=%s DefaultHttpRunPath=%s DefaultHttpStreamPath=%s DefaultHttpEndStreamPath=%s bStatsLiveLogDefault=%d"),
        *DefaultServerIp, MediaUdpPort, *DefaultWsHost, *DefaultWsScheme, *DefaultHttpRunPath, *DefaultHttpStreamPath, *DefaultHttpEndStreamPath, bStatsLiveLogDefault?1:0);
}

