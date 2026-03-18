#include "Audio/AudioStreamSettings.h"

const UAudioStreamSettings* UAudioStreamSettings::Get()
{
    return GetDefault<UAudioStreamSettings>();
}

FString UAudioStreamSettings::GetEffectiveWsScheme(EAudioStreamProtocolMode ProtocolMode) const
{
    const FString Specific = (ProtocolMode == EAudioStreamProtocolMode::PureWebSocket) ? PureWebSocketScheme : LegacyWsScheme;
    return Specific.IsEmpty() ? DefaultWsScheme : Specific;
}

FString UAudioStreamSettings::GetEffectiveWsHost(EAudioStreamProtocolMode ProtocolMode) const
{
    const FString Specific = (ProtocolMode == EAudioStreamProtocolMode::PureWebSocket) ? PureWebSocketHost : LegacyWsHost;
    return Specific.IsEmpty() ? DefaultWsHost : Specific;
}

FString UAudioStreamSettings::GetEffectiveWsPath(EAudioStreamProtocolMode ProtocolMode) const
{
    const FString Specific = (ProtocolMode == EAudioStreamProtocolMode::PureWebSocket) ? PureWebSocketPath : LegacyWsPathPrefix;
    return Specific.IsEmpty() ? DefaultWsPathPrefix : Specific;
}

void UAudioStreamSettings::PostInitProperties()
{
    Super::PostInitProperties();
    // Force load config to ensure values from DefaultGame.ini are applied early
    LoadConfig();

    // Print only core, actively used settings to avoid clutter
    UE_LOG(LogTemp, Log, TEXT("[AudioStreamSettings] Loaded: Protocol=%s Legacy=%s://%s%s PureWS=%s://%s%s SampleRate=%d Channels=%d FrameMs=%d StatsLive=%d"),
        DefaultProtocolMode == EAudioStreamProtocolMode::PureWebSocket ? TEXT("PureWebSocket") : TEXT("LegacyHttpWs"),
        *GetEffectiveWsScheme(EAudioStreamProtocolMode::LegacyHttpWs),
        *GetEffectiveWsHost(EAudioStreamProtocolMode::LegacyHttpWs),
        *GetEffectiveWsPath(EAudioStreamProtocolMode::LegacyHttpWs),
        *GetEffectiveWsScheme(EAudioStreamProtocolMode::PureWebSocket),
        *GetEffectiveWsHost(EAudioStreamProtocolMode::PureWebSocket),
        *GetEffectiveWsPath(EAudioStreamProtocolMode::PureWebSocket),
        DefaultSampleRate, DefaultChannels, FrameDurationMs,
        bStatsLiveLogDefault?1:0);
}

#if WITH_EDITOR
bool UAudioStreamSettings::CanEditChange(const FProperty* InProperty) const
{
    if (!Super::CanEditChange(InProperty) || InProperty == nullptr)
    {
        return false;
    }

    const FName PropertyName = InProperty->GetFName();
    const bool bLegacyMode = DefaultProtocolMode == EAudioStreamProtocolMode::LegacyHttpWs;
    const bool bPureWsMode = DefaultProtocolMode == EAudioStreamProtocolMode::PureWebSocket;

    if (PropertyName == GET_MEMBER_NAME_CHECKED(UAudioStreamSettings, LegacyWsScheme)
        || PropertyName == GET_MEMBER_NAME_CHECKED(UAudioStreamSettings, LegacyWsHost)
        || PropertyName == GET_MEMBER_NAME_CHECKED(UAudioStreamSettings, LegacyWsPathPrefix)
        || PropertyName == GET_MEMBER_NAME_CHECKED(UAudioStreamSettings, DefaultHttpRunPath)
        || PropertyName == GET_MEMBER_NAME_CHECKED(UAudioStreamSettings, DefaultHttpStreamPath)
        || PropertyName == GET_MEMBER_NAME_CHECKED(UAudioStreamSettings, DefaultHttpEndStreamPath))
    {
        return bLegacyMode;
    }

    if (PropertyName == GET_MEMBER_NAME_CHECKED(UAudioStreamSettings, PureWebSocketScheme)
        || PropertyName == GET_MEMBER_NAME_CHECKED(UAudioStreamSettings, PureWebSocketHost)
        || PropertyName == GET_MEMBER_NAME_CHECKED(UAudioStreamSettings, PureWebSocketPath))
    {
        return bPureWsMode;
    }

    if (PropertyName == GET_MEMBER_NAME_CHECKED(UAudioStreamSettings, DefaultWsScheme)
        || PropertyName == GET_MEMBER_NAME_CHECKED(UAudioStreamSettings, DefaultWsHost)
        || PropertyName == GET_MEMBER_NAME_CHECKED(UAudioStreamSettings, DefaultWsPathPrefix))
    {
        return bShowCompatibilityFallbackSettings;
    }

    return true;
}
#endif

