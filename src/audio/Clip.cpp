#include "zio/audio/Clip.h"

#include "zio/jobs/JobSystem.h"

#if defined(ZIO_PLATFORM_WINDOWS)

#define INITGUID
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

#include <algorithm>
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

namespace
{

class MediaFoundationStream final : public AudioStream
{
public:
    static std::unique_ptr<AudioStream> create(std::string_view path)
    {
        auto stream = std::unique_ptr<MediaFoundationStream>(new MediaFoundationStream());
        if (!stream->init(path))
            return nullptr;
        return stream;
    }

    std::uint32_t channels() const override { return channels_; }
    std::uint32_t sampleRate() const override { return sampleRate_; }

    std::size_t read(std::uint8_t* destination, std::size_t maxFrames) override
    {
        const std::size_t frameBytes = static_cast<std::size_t>(channels_) * sizeof(std::int16_t);
        std::size_t produced         = 0;
        while (produced < maxFrames)
        {
            if (pendingOffset_ >= pending_.size())
            {
                if (!fill())
                    break;
                continue;
            }
            const std::size_t availableFrames = (pending_.size() - pendingOffset_) / frameBytes;
            const std::size_t take            = (std::min)(availableFrames, maxFrames - produced);
            std::memcpy(destination + produced * frameBytes, pending_.data() + pendingOffset_, take * frameBytes);
            pendingOffset_ += take * frameBytes;
            produced += take;
        }
        return produced;
    }

    bool rewind() override
    {
        const GUID noGuid{};
        PROPVARIANT start;
        PropVariantInit(&start);
        start.vt            = VT_I8;
        start.hVal.QuadPart = 0;
        const HRESULT hr    = reader_->SetCurrentPosition(noGuid, start);
        PropVariantClear(&start);
        pending_.clear();
        pendingOffset_ = 0;
        ended_         = false;
        return SUCCEEDED(hr);
    }

private:
    MediaFoundationStream() = default;

    bool init(std::string_view path)
    {
        if (FAILED(mf_.hr))
            return false;
        const std::wstring widePath = std::filesystem::path(std::string(path)).wstring();
        if (FAILED(MFCreateSourceReaderFromURL(widePath.c_str(), nullptr, &reader_)))
            return false;
        ComPtr<IMFMediaType> partial;
        if (FAILED(MFCreateMediaType(&partial)) || FAILED(partial->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio)) ||
            FAILED(partial->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM)))
            return false;
        const DWORD first = static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM);
        if (FAILED(reader_->SetCurrentMediaType(first, nullptr, partial.Get())) ||
            FAILED(reader_->SetStreamSelection(first, TRUE)))
            return false;
        ComPtr<IMFMediaType> actual;
        if (FAILED(reader_->GetCurrentMediaType(first, &actual)))
            return false;
        UINT32 channels = 0, rate = 0, bits = 0;
        actual->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels);
        actual->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate);
        actual->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &bits);
        if (channels == 0 || rate == 0 || bits == 0)
            return false;
        channels_   = channels;
        sampleRate_ = rate;
        bits_       = bits;
        return true;
    }

    bool fill()
    {
        pending_.clear();
        pendingOffset_ = 0;
        while (!ended_)
        {
            DWORD flags = 0;
            ComPtr<IMFSample> sample;
            if (FAILED(reader_->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), 0, nullptr, &flags,
                                           nullptr, &sample)))
            {
                ended_ = true;
                return false;
            }
            if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
            {
                ended_ = true;
                return false;
            }
            if (!sample)
                continue;
            ComPtr<IMFMediaBuffer> buffer;
            if (FAILED(sample->ConvertToContiguousBuffer(&buffer)))
                continue;
            BYTE* data = nullptr;
            DWORD len  = 0;
            if (FAILED(buffer->Lock(&data, nullptr, &len)))
                continue;
            std::vector<std::uint8_t> raw(data, data + len);
            buffer->Unlock();
            pending_ = convertToPcm16(raw, bits_);
            if (!pending_.empty())
                return true;
        }
        return false;
    }

    MfStartupGuard mf_;
    ComPtr<IMFSourceReader> reader_;
    std::uint32_t channels_   = 0;
    std::uint32_t sampleRate_ = 0;
    UINT32 bits_              = 16;
    std::vector<std::uint8_t> pending_;
    std::size_t pendingOffset_ = 0;
    bool ended_                = false;
};

}

std::unique_ptr<AudioStream> AudioStream::open(std::string_view path)
{
    return MediaFoundationStream::create(path);
}

}

#else

#include <miniaudio.h>

namespace zio::audio
{

namespace
{

class MiniaudioStream final : public AudioStream
{
public:
    static std::unique_ptr<AudioStream> create(std::string_view path)
    {
        auto stream              = std::unique_ptr<MiniaudioStream>(new MiniaudioStream());
        const std::string file(path);
        ma_decoder_config config = ma_decoder_config_init(ma_format_s16, 0, 0);
        if (ma_decoder_init_file(file.c_str(), &config, &stream->decoder_) != MA_SUCCESS)
            return nullptr;
        stream->open_ = true;
        return stream;
    }

    ~MiniaudioStream() override
    {
        if (open_)
            ma_decoder_uninit(&decoder_);
    }

    std::uint32_t channels() const override { return decoder_.outputChannels; }
    std::uint32_t sampleRate() const override { return decoder_.outputSampleRate; }

    std::size_t read(std::uint8_t* destination, std::size_t maxFrames) override
    {
        ma_uint64 frames = 0;
        ma_decoder_read_pcm_frames(&decoder_, destination, maxFrames, &frames);
        return static_cast<std::size_t>(frames);
    }

    bool rewind() override { return ma_decoder_seek_to_pcm_frame(&decoder_, 0) == MA_SUCCESS; }

private:
    MiniaudioStream() = default;

    ma_decoder decoder_{};
    bool open_ = false;
};

}

std::unique_ptr<AudioStream> AudioStream::open(std::string_view path)
{
    return MiniaudioStream::create(path);
}

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
