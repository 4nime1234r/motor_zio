#include "zio/audio/Clip.h"

#include "zio/jobs/JobSystem.h"

#if defined(ZIO_PLATFORM_WINDOWS)

#define INITGUID
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

#include <cstring>
#include <filesystem>

using Microsoft::WRL::ComPtr;

namespace zio::audio
{

namespace
{

struct ComThreadGuard
{
    HRESULT hr;
    ComThreadGuard() : hr(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
    ~ComThreadGuard()
    {
        if (SUCCEEDED(hr))
            CoUninitialize();
    }
};

struct MfStartupGuard
{
    HRESULT hr;
    MfStartupGuard() : hr(MFStartup(MF_VERSION, MFSTARTUP_LITE)) {}
    ~MfStartupGuard()
    {
        if (SUCCEEDED(hr))
            MFShutdown();
    }
};

std::vector<std::uint8_t> convertToPcm16(const std::vector<std::uint8_t>& raw, UINT32 bitsPerSample)
{
    switch (bitsPerSample)
    {
    case 16:
        return raw;

    case 8:
    {
        std::vector<std::uint8_t> out(raw.size() * 2);
        auto* dst = reinterpret_cast<std::int16_t*>(out.data());
        for (std::size_t i = 0; i < raw.size(); ++i)
            dst[i] = static_cast<std::int16_t>((static_cast<int>(raw[i]) - 128) << 8);
        return out;
    }

    case 24:
    {
        const std::size_t frames = raw.size() / 3;
        std::vector<std::uint8_t> out(frames * 2);
        auto* dst = reinterpret_cast<std::int16_t*>(out.data());
        for (std::size_t i = 0; i < frames; ++i)
        {
            std::int32_t sample = (static_cast<std::int32_t>(raw[i * 3 + 2]) << 16) |
                                   (static_cast<std::int32_t>(raw[i * 3 + 1]) << 8) | raw[i * 3];
            if (sample & 0x800000)
                sample |= static_cast<std::int32_t>(0xFF000000);
            dst[i] = static_cast<std::int16_t>(sample >> 8);
        }
        return out;
    }

    case 32:
    {
        const std::size_t frames = raw.size() / 4;
        std::vector<std::uint8_t> out(frames * 2);
        auto* dst        = reinterpret_cast<std::int16_t*>(out.data());
        const auto* src = reinterpret_cast<const std::int32_t*>(raw.data());
        for (std::size_t i = 0; i < frames; ++i)
            dst[i] = static_cast<std::int16_t>(src[i] >> 16);
        return out;
    }

    default:
        return {};
    }
}

}

std::optional<Clip> decodeAudioFile(std::string_view path)
{
    thread_local ComThreadGuard comGuard;
    if (FAILED(comGuard.hr) && comGuard.hr != RPC_E_CHANGED_MODE)
        return std::nullopt;

    MfStartupGuard mfGuard;
    if (FAILED(mfGuard.hr))
        return std::nullopt;

    std::wstring widePath = std::filesystem::path(std::string(path)).wstring();

    ComPtr<IMFSourceReader> reader;
    if (FAILED(MFCreateSourceReaderFromURL(widePath.c_str(), nullptr, &reader)))
        return std::nullopt;

    ComPtr<IMFMediaType> partialType;
    if (FAILED(MFCreateMediaType(&partialType)))
        return std::nullopt;
    if (FAILED(partialType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio)))
        return std::nullopt;
    if (FAILED(partialType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM)))
        return std::nullopt;
    if (FAILED(reader->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), nullptr,
                                            partialType.Get())))
        return std::nullopt;
    if (FAILED(reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), TRUE)))
        return std::nullopt;

    ComPtr<IMFMediaType> actualType;
    if (FAILED(reader->GetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), &actualType)))
        return std::nullopt;

    UINT32 channels = 0, sampleRate = 0, bitsPerSample = 0;
    actualType->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels);
    actualType->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &sampleRate);
    actualType->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &bitsPerSample);
    if (channels == 0 || sampleRate == 0 || bitsPerSample == 0)
        return std::nullopt;

    std::vector<std::uint8_t> raw;
    for (;;)
    {
        DWORD flags = 0;
        ComPtr<IMFSample> sample;
        if (FAILED(reader->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), 0, nullptr, &flags,
                                       nullptr, &sample)))
            return std::nullopt;
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
            break;
        if (!sample)
            continue;

        ComPtr<IMFMediaBuffer> buffer;
        if (FAILED(sample->ConvertToContiguousBuffer(&buffer)))
            return std::nullopt;

        BYTE* data = nullptr;
        DWORD len  = 0;
        if (FAILED(buffer->Lock(&data, nullptr, &len)))
            return std::nullopt;
        raw.insert(raw.end(), data, data + len);
        buffer->Unlock();
    }

    Clip clip;
    clip.channels   = channels;
    clip.sampleRate = sampleRate;
    clip.pcm        = convertToPcm16(raw, bitsPerSample);
    if (clip.pcm.empty())
        return std::nullopt;

    return clip;
}

}

#else

#include <miniaudio.h>

namespace zio::audio
{

std::optional<Clip> decodeAudioFile(std::string_view path)
{
    const std::string file(path);
    ma_decoder_config config = ma_decoder_config_init(ma_format_s16, 0, 0);
    ma_decoder decoder;
    if (ma_decoder_init_file(file.c_str(), &config, &decoder) != MA_SUCCESS)
        return std::nullopt;

    Clip clip;
    clip.channels      = decoder.outputChannels;
    clip.sampleRate    = decoder.outputSampleRate;
    clip.bitsPerSample = 16;

    constexpr ma_uint64 kChunkFrames = 4096;
    std::vector<std::int16_t> chunk(static_cast<std::size_t>(kChunkFrames) * clip.channels);
    for (;;)
    {
        ma_uint64 framesRead = 0;
        const ma_result result = ma_decoder_read_pcm_frames(&decoder, chunk.data(), kChunkFrames, &framesRead);
        if (framesRead > 0)
        {
            const auto* bytes = reinterpret_cast<const std::uint8_t*>(chunk.data());
            clip.pcm.insert(clip.pcm.end(), bytes, bytes + framesRead * clip.channels * sizeof(std::int16_t));
        }
        if (result != MA_SUCCESS || framesRead < kChunkFrames)
            break;
    }
    ma_decoder_uninit(&decoder);
    if (clip.pcm.empty() || clip.channels == 0)
        return std::nullopt;
    return clip;
}

}

#endif

namespace zio::audio
{

void loadClipAsync(std::string path, ClipLoadCallback onLoaded)
{
    jobs::run([path = std::move(path)]() { return decodeAudioFile(path); },
              [onLoaded = std::move(onLoaded)](std::optional<Clip> clip) { onLoaded(std::move(clip)); });
}

}
