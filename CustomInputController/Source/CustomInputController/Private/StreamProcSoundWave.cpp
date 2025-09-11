#include "StreamProcSoundWave.h"

void UStreamProcSoundWave::EnqueuePcm(const uint8* Data, int32 NumBytes)
{
    if (!Data || NumBytes <= 0)
    {
        return;
    }
    const int32 FrameBytes = 2 * FMath::Max(1, NumChannels);
    const int32 Aligned = NumBytes - (NumBytes % FrameBytes);
    if (Aligned <= 0)
    {
        return;
    }

    // 将数据复制到独立块并投递到无锁队列，供渲染线程合并
    TArray<uint8> Chunk;
    Chunk.Append(Data, Aligned);
    PendingChunks.Enqueue(MoveTemp(Chunk));
}

int32 UStreamProcSoundWave::OnGeneratePCMAudio(TArray<uint8>& OutAudio, int32 NumSamples)
{
    bInGenerate.store(true, std::memory_order_relaxed);

    // 先无锁地合并生产者投递的所有块到渲染线程私有缓冲
    {
        TArray<uint8> Chunk;
        while (PendingChunks.Dequeue(Chunk))
        {
            if (Chunk.Num() > 0)
            {
                const int32 OldSize = QueueData.Num();
                QueueData.AddUninitialized(Chunk.Num());
                FMemory::Memcpy(QueueData.GetData() + OldSize, Chunk.GetData(), Chunk.Num());
            }
            Chunk.Reset();
        }
    }

    // 请求的字节数：NumSamples 为总样本数（已含所有声道），每样本16-bit -> *2
    const int32 RequestedBytes = FMath::Max(0, NumSamples) * 2; 

    int32 CopiedBytes = 0;
    if (RequestedBytes > 0)
    {
        const int32 Available = FMath::Max(0, QueueData.Num() - ReadOffset);
        CopiedBytes = FMath::Min(RequestedBytes, Available);
        if (CopiedBytes > 0)
        {
            const int32 Start = OutAudio.Num();
            OutAudio.AddUninitialized(CopiedBytes);
            FMemory::Memcpy(OutAudio.GetData() + Start, QueueData.GetData() + ReadOffset, CopiedBytes);
            ReadOffset += CopiedBytes;
        }
    }

    const int32 Remainder = RequestedBytes - CopiedBytes;
    const bool bTrueUnderRun = (CopiedBytes == 0 && RequestedBytes > 0);

    // 若不足则零填满缓冲，避免爆音
    if (Remainder > 0)
    {
        OutAudio.AddZeroed(Remainder);
    }

    // 上一帧欠载 → 本帧有非零产出且启用开关 → 在本帧的最前端做淡入
    if (bEnableUnderRunFade && bNeedFadeIn && CopiedBytes > 0)
    {
        int16* Samples = reinterpret_cast<int16*>(OutAudio.GetData());
        const int32 ProducedFrames = CopiedBytes / (2 * FMath::Max(1, NumChannels));
        const int32 FadeFrames = FMath::Max(1, (SampleRate * FMath::Max(0, FadeMs)) / 1000);
        const int32 N = FMath::Min(FadeFrames, ProducedFrames);
        for (int32 i = 0; i < N; ++i)
        {
            const float g = (float)(i + 1) / (float)N;
            for (int32 ch = 0; ch < NumChannels; ++ch)
            {
                const int32 idx = i * NumChannels + ch;
                Samples[idx] = (int16)FMath::RoundToInt((float)Samples[idx] * g);
            }
        }
        bNeedFadeIn = false;
    }

    if (bEnableUnderRunFade && bTrueUnderRun)
    {
        bNeedFadeIn = true;
    }

    // 统计送入Mixer的真实字节（等于请求字节，含零填）
    if (RequestedBytes > 0)
    {
        ConsumedBytes.fetch_add((int64)RequestedBytes, std::memory_order_relaxed);
    }

    // 适度压缩：读偏移跨过阈值且剩余数据量不大时，将未读数据移到开头
    {
        const int32 Unread = QueueData.Num() - ReadOffset;
        if (ReadOffset > CompactThreshold && Unread < (CompactThreshold >> 1))
        {
            if (Unread > 0)
            {
                FMemory::Memmove(QueueData.GetData(), QueueData.GetData() + ReadOffset, Unread);
            }
            QueueData.SetNum(Unread, EAllowShrinking::No);
            ReadOffset = 0;
        }
    }

    bInGenerate.store(false, std::memory_order_relaxed);

    // 返回产生的样本数（按请求量）
    return NumSamples;
}

void UStreamProcSoundWave::OnBeginGenerate()
{
    Super::OnBeginGenerate();
    bInGenerate.store(true, std::memory_order_relaxed);
}

void UStreamProcSoundWave::OnEndGenerate()
{
    Super::OnEndGenerate();
    bInGenerate.store(false, std::memory_order_relaxed);
}
