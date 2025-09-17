#pragma once
#include "CoreMinimal.h"
#include "Sockets.h"
#include "IPAddress.h"

/** 媒体包类型 */
enum class EMediaPacketType : uint8
{
    Audio = 0,
    Viseme = 1,
    Control = 2
};

/** Flags 位 */
namespace EMediaPacketFlags
{
    static const uint16 Keyframe = 1 << 0;    // 音频首包或关键校正
    static const uint16 Redundant = 1 << 1;   // 带冗余数据
}

#pragma pack(push,1)
struct FMediaPacketHeader
{
    char   Magic[2];       // 'A','S'
    uint8  Version;        // 1
    uint8  MediaType;      // EMediaPacketType
    uint16 StreamId;       // 多流区分
    uint32 Seq;            // 递增序号
    uint64 PtsUs;          // 播放时间（服务器时间线微秒）
    uint16 Flags;          // 标志位
    uint32 PayloadLen;     // 负载长度
};
#pragma pack(pop)
static_assert(sizeof(FMediaPacketHeader)==24, "Header size mismatch");

inline uint64 MSP_NowMicroseconds()
{
    const double Seconds = FPlatformTime::Seconds();
    return (uint64)(Seconds * 1000000.0);
}

inline void MSP_FillHeader(FMediaPacketHeader& H, EMediaPacketType Type, uint16 StreamId, uint32 Seq, uint64 PtsUs, uint16 Flags, uint32 PayloadLen)
{
    H.Magic[0] = 'A'; H.Magic[1] = 'S';
    H.Version = 1;
    H.MediaType = (uint8)Type;
    H.StreamId = StreamId;
    H.Seq = Seq;
    H.PtsUs = PtsUs;
    H.Flags = Flags;
    H.PayloadLen = PayloadLen;
}

inline bool MSP_ParseHeader(const TArray<uint8>& Data, FMediaPacketHeader& Out)
{
    if (Data.Num() < (int32)sizeof(FMediaPacketHeader)) return false;
    FMemory::Memcpy(&Out, Data.GetData(), sizeof(FMediaPacketHeader));
    if (Out.Magic[0] != 'A' || Out.Magic[1] != 'S') return false;
    if (Out.Version != 1) return false;
    if (Data.Num() < (int32)(sizeof(FMediaPacketHeader) + Out.PayloadLen)) return false;
    return true;
}

