#include "zio/audio/AudioManager.h"

#include <algorithm>
#include <mutex>
#include <unordered_map>
#include <vector>

#if defined(ZIO_PLATFORM_WINDOWS)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <xaudio2.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace zio::audio
{

namespace
{
constexpr float kMaxFreqRatio = 4.f;
}

class VoiceCallback;

struct AudioManager::Impl
{
    bool comOwned = false;
    ComPtr<IXAudio2> engine;
    IXAudio2MasteringVoice* mastering                       = nullptr;
    IXAudio2SubmixVoice* submix[kCategoryCount]              = {};

    struct VoiceEntry
    {
        IXAudio2SourceVoice* voice = nullptr;
        std::shared_ptr<Clip> clip;
        std::unique_ptr<VoiceCallback> callback;
    };

    std::unordered_map<VoiceHandle, VoiceEntry> voices;
    VoiceHandle nextHandle = 1;

    std::mutex finishedMutex;
    std::vector<VoiceHandle> finishedQueue;

    void notifyFinished(VoiceHandle handle)
    {
        std::lock_guard<std::mutex> lock(finishedMutex);
        finishedQueue.push_back(handle);
    }

    static void destroyEntry(VoiceEntry& entry)
    {
        if (entry.voice)
        {
            entry.voice->Stop(0);
            entry.voice->DestroyVoice();
            entry.voice = nullptr;
        }
    }
};

class VoiceCallback final : public IXAudio2VoiceCallback
{
public:
    VoiceCallback(AudioManager::Impl* owner, VoiceHandle handle) : owner_(owner), handle_(handle) {}

    STDMETHOD_(void, OnVoiceProcessingPassStart)(UINT32) override {}
    STDMETHOD_(void, OnVoiceProcessingPassEnd)() override {}
    STDMETHOD_(void, OnBufferStart)(void*) override {}
    STDMETHOD_(void, OnBufferEnd)(void*) override {}
    STDMETHOD_(void, OnLoopEnd)(void*) override {}
    STDMETHOD_(void, OnStreamEnd)() override { owner_->notifyFinished(handle_); }
    STDMETHOD_(void, OnVoiceError)(void*, HRESULT) override { owner_->notifyFinished(handle_); }

private:
    AudioManager::Impl* owner_;
    VoiceHandle handle_;
};

AudioManager::AudioManager() : impl_(std::make_unique<Impl>()) {}
AudioManager::~AudioManager()
{
    shutdown();
}

AudioManager& AudioManager::instance()
{
    static AudioManager global;
    return global;
}

bool AudioManager::init()
{
    if (impl_->engine)
        return true;

    const HRESULT comHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    impl_->comOwned      = SUCCEEDED(comHr);
    if (FAILED(comHr) && comHr != RPC_E_CHANGED_MODE)
        return false;

    if (FAILED(XAudio2Create(&impl_->engine, 0, XAUDIO2_DEFAULT_PROCESSOR)))
    {
        shutdown();
        return false;
    }

    if (FAILED(impl_->engine->CreateMasteringVoice(&impl_->mastering)))
    {
        shutdown();
        return false;
    }

    XAUDIO2_VOICE_DETAILS details{};
    impl_->mastering->GetVoiceDetails(&details);

    for (std::size_t i = 0; i < kCategoryCount; ++i)
    {
        if (FAILED(impl_->engine->CreateSubmixVoice(&impl_->submix[i], details.InputChannels,
                                                      details.InputSampleRate)))
        {
            shutdown();
            return false;
        }
    }

    return true;
}

void AudioManager::shutdown()
{
    for (auto& [handle, entry] : impl_->voices)
        Impl::destroyEntry(entry);
    impl_->voices.clear();

    for (auto& submixVoice : impl_->submix)
    {
        if (submixVoice)
        {
            submixVoice->DestroyVoice();
            submixVoice = nullptr;
        }
    }
    if (impl_->mastering)
    {
        impl_->mastering->DestroyVoice();
        impl_->mastering = nullptr;
    }
    impl_->engine.Reset();

    if (impl_->comOwned)
    {
        CoUninitialize();
        impl_->comOwned = false;
    }

    std::lock_guard<std::mutex> lock(impl_->finishedMutex);
    impl_->finishedQueue.clear();
}

bool AudioManager::isInitialized() const
{
    return impl_->engine != nullptr;
}

VoiceHandle AudioManager::play(std::shared_ptr<Clip> clip, const PlayParams& params)
{
    if (!impl_->engine || !clip || clip->pcm.empty() || clip->channels == 0)
        return kInvalidVoiceHandle;

    const auto categoryIndex = static_cast<std::size_t>(params.category);
    if (categoryIndex >= kCategoryCount || !impl_->submix[categoryIndex])
        return kInvalidVoiceHandle;

    WAVEFORMATEX wfx{};
    wfx.wFormatTag      = WAVE_FORMAT_PCM;
    wfx.nChannels       = static_cast<WORD>(clip->channels);
    wfx.nSamplesPerSec  = clip->sampleRate;
    wfx.wBitsPerSample  = 16;
    wfx.nBlockAlign     = static_cast<WORD>(clip->channels * sizeof(std::int16_t));
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

    const VoiceHandle handle = impl_->nextHandle++;
    auto callback              = std::make_unique<VoiceCallback>(impl_.get(), handle);

    XAUDIO2_SEND_DESCRIPTOR sendDesc{0, impl_->submix[categoryIndex]};
    XAUDIO2_VOICE_SENDS sends{1, &sendDesc};

    IXAudio2SourceVoice* voice = nullptr;
    if (FAILED(impl_->engine->CreateSourceVoice(&voice, &wfx, 0, kMaxFreqRatio, callback.get(), &sends, nullptr)))
        return kInvalidVoiceHandle;

    XAUDIO2_BUFFER buffer{};
    buffer.AudioBytes = static_cast<UINT32>(clip->pcm.size());
    buffer.pAudioData = clip->pcm.data();
    buffer.LoopCount  = params.loop ? XAUDIO2_LOOP_INFINITE : 0;
    buffer.Flags      = XAUDIO2_END_OF_STREAM;

    if (FAILED(voice->SubmitSourceBuffer(&buffer)) ||
        FAILED(voice->SetVolume(std::max(params.volume, 0.f))) ||
        FAILED(voice->SetFrequencyRatio(std::clamp(params.pitch, XAUDIO2_MIN_FREQ_RATIO, kMaxFreqRatio))) ||
        FAILED(voice->Start(0)))
    {
        voice->DestroyVoice();
        return kInvalidVoiceHandle;
    }

    Impl::VoiceEntry entry;
    entry.voice    = voice;
    entry.clip     = std::move(clip);
    entry.callback = std::move(callback);
    impl_->voices.emplace(handle, std::move(entry));

    return handle;
}

void AudioManager::stop(VoiceHandle voice)
{
    auto it = impl_->voices.find(voice);
    if (it == impl_->voices.end())
        return;
    Impl::destroyEntry(it->second);
    impl_->voices.erase(it);
}

void AudioManager::setVolume(VoiceHandle voice, float volume)
{
    auto it = impl_->voices.find(voice);
    if (it == impl_->voices.end() || !it->second.voice)
        return;
    it->second.voice->SetVolume(std::max(volume, 0.f));
}

void AudioManager::setPitch(VoiceHandle voice, float pitch)
{
    auto it = impl_->voices.find(voice);
    if (it == impl_->voices.end() || !it->second.voice)
        return;
    it->second.voice->SetFrequencyRatio(std::clamp(pitch, XAUDIO2_MIN_FREQ_RATIO, kMaxFreqRatio));
}

bool AudioManager::isPlaying(VoiceHandle voice) const
{
    return impl_->voices.find(voice) != impl_->voices.end();
}

void AudioManager::setCategoryVolume(Category category, float volume)
{
    const auto index = static_cast<std::size_t>(category);
    if (index >= kCategoryCount || !impl_->submix[index])
        return;
    impl_->submix[index]->SetVolume(std::max(volume, 0.f));
}

void AudioManager::update()
{
    std::vector<VoiceHandle> finished;
    {
        std::lock_guard<std::mutex> lock(impl_->finishedMutex);
        finished.swap(impl_->finishedQueue);
    }
    for (VoiceHandle handle : finished)
    {
        auto it = impl_->voices.find(handle);
        if (it == impl_->voices.end())
            continue;
        Impl::destroyEntry(it->second);
        impl_->voices.erase(it);
    }
}

}

#else

#include <miniaudio.h>

namespace zio::audio
{

struct AudioManager::Impl
{
    struct Voice
    {
        std::shared_ptr<Clip> clip;
        ma_audio_buffer buffer{};
        ma_sound sound{};
        bool loop = false;
    };

    bool ready = false;
    ma_engine engine{};
    ma_sound_group groups[kCategoryCount]{};
    std::unordered_map<VoiceHandle, std::unique_ptr<Voice>> voices;
    VoiceHandle nextHandle = 1;

    static void destroy(Voice& voice)
    {
        ma_sound_uninit(&voice.sound);
        ma_audio_buffer_uninit(&voice.buffer);
    }
};

AudioManager::AudioManager() : impl_(std::make_unique<Impl>()) {}

AudioManager::~AudioManager()
{
    shutdown();
}

AudioManager& AudioManager::instance()
{
    static AudioManager global;
    return global;
}

bool AudioManager::init()
{
    if (impl_->ready)
        return true;
    if (ma_engine_init(nullptr, &impl_->engine) != MA_SUCCESS)
        return false;
    for (ma_sound_group& group : impl_->groups)
        if (ma_sound_group_init(&impl_->engine, 0, nullptr, &group) != MA_SUCCESS)
        {
            ma_engine_uninit(&impl_->engine);
            return false;
        }
    impl_->ready = true;
    return true;
}

void AudioManager::shutdown()
{
    if (!impl_->ready)
        return;
    for (auto& entry : impl_->voices)
        Impl::destroy(*entry.second);
    impl_->voices.clear();
    for (ma_sound_group& group : impl_->groups)
        ma_sound_group_uninit(&group);
    ma_engine_uninit(&impl_->engine);
    impl_->ready = false;
}

bool AudioManager::isInitialized() const
{
    return impl_->ready;
}

VoiceHandle AudioManager::play(std::shared_ptr<Clip> clip, const PlayParams& params)
{
    if (!impl_->ready || !clip || clip->pcm.empty() || clip->channels == 0)
        return kInvalidVoiceHandle;
    const auto categoryIndex = static_cast<std::size_t>(params.category);
    if (categoryIndex >= kCategoryCount)
        return kInvalidVoiceHandle;

    auto voice  = std::make_unique<Impl::Voice>();
    voice->clip = clip;
    voice->loop = params.loop;

    ma_audio_buffer_config config =
        ma_audio_buffer_config_init(ma_format_s16, clip->channels, clip->frameCount(), clip->pcm.data(), nullptr);
    config.sampleRate = clip->sampleRate;
    if (ma_audio_buffer_init(&config, &voice->buffer) != MA_SUCCESS)
        return kInvalidVoiceHandle;
    if (ma_sound_init_from_data_source(&impl_->engine, &voice->buffer, 0, &impl_->groups[categoryIndex], &voice->sound) !=
        MA_SUCCESS)
    {
        ma_audio_buffer_uninit(&voice->buffer);
        return kInvalidVoiceHandle;
    }
    ma_sound_set_volume(&voice->sound, std::max(params.volume, 0.f));
    ma_sound_set_pitch(&voice->sound, std::max(params.pitch, 0.01f));
    ma_sound_set_looping(&voice->sound, params.loop ? MA_TRUE : MA_FALSE);
    ma_sound_start(&voice->sound);

    const VoiceHandle handle = impl_->nextHandle++;
    impl_->voices.emplace(handle, std::move(voice));
    return handle;
}

void AudioManager::stop(VoiceHandle voice)
{
    const auto it = impl_->voices.find(voice);
    if (it == impl_->voices.end())
        return;
    Impl::destroy(*it->second);
    impl_->voices.erase(it);
}

void AudioManager::setVolume(VoiceHandle voice, float volume)
{
    const auto it = impl_->voices.find(voice);
    if (it != impl_->voices.end())
        ma_sound_set_volume(&it->second->sound, std::max(volume, 0.f));
}

void AudioManager::setPitch(VoiceHandle voice, float pitch)
{
    const auto it = impl_->voices.find(voice);
    if (it != impl_->voices.end())
        ma_sound_set_pitch(&it->second->sound, std::max(pitch, 0.01f));
}

bool AudioManager::isPlaying(VoiceHandle voice) const
{
    const auto it = impl_->voices.find(voice);
    return it != impl_->voices.end() && ma_sound_is_playing(&it->second->sound) && !ma_sound_at_end(&it->second->sound);
}

void AudioManager::setCategoryVolume(Category category, float volume)
{
    const auto index = static_cast<std::size_t>(category);
    if (impl_->ready && index < kCategoryCount)
        ma_sound_group_set_volume(&impl_->groups[index], std::max(volume, 0.f));
}

void AudioManager::update()
{
    if (!impl_->ready)
        return;
    for (auto it = impl_->voices.begin(); it != impl_->voices.end();)
    {
        if (!it->second->loop && ma_sound_at_end(&it->second->sound))
        {
            Impl::destroy(*it->second);
            it = impl_->voices.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

}

#endif
