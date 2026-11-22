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
#include <xaudio2fx.h>
#include <wrl/client.h>

#include <cmath>
#include <string>

using Microsoft::WRL::ComPtr;

namespace zio::audio
{

namespace
{
constexpr float kMaxFreqRatio = 4.f;
constexpr UINT32 kBusChannels = 2;
constexpr UINT32 kRootStage   = 12;
}

class VoiceCallback;

struct AudioManager::Impl
{
    struct Bus
    {
        std::string name;
        IXAudio2SubmixVoice* voice = nullptr;
        BusHandle parent           = kInvalidBusHandle;
        UINT32 stage               = kRootStage;
    };

    bool comOwned = false;
    ComPtr<IXAudio2> engine;
    IXAudio2MasteringVoice* mastering = nullptr;
    UINT32 sampleRate                 = 48000;
    std::vector<Bus> buses;
    Listener listener;

    struct VoiceEntry
    {
        IXAudio2SourceVoice* voice = nullptr;
        std::shared_ptr<Clip> clip;
        std::unique_ptr<VoiceCallback> callback;
        IXAudio2SubmixVoice* target = nullptr;
        bool spatial                = false;
        math::Vec3 position         = math::Vec3::zero();
        float minDistance           = 1.f;
        float maxDistance           = 50.f;
        UINT32 channels             = 1;
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

    Bus* find(BusHandle handle)
    {
        return handle != kInvalidBusHandle && handle <= buses.size() && buses[handle - 1].voice ? &buses[handle - 1]
                                                                                                   : nullptr;
    }

    BusHandle makeBus(const std::string& name, BusHandle parentHandle)
    {
        Bus* parent = parentHandle != kInvalidBusHandle ? find(parentHandle) : nullptr;
        if (parentHandle != kInvalidBusHandle && !parent)
            return kInvalidBusHandle;
        if (parent && parent->stage <= 1)
            return kInvalidBusHandle;

        Bus bus;
        bus.name   = name;
        bus.parent = parentHandle;
        bus.stage  = parent ? parent->stage - 1 : kRootStage;

        XAUDIO2_SEND_DESCRIPTOR send{0, parent ? static_cast<IXAudio2Voice*>(parent->voice) : nullptr};
        XAUDIO2_VOICE_SENDS sends{1, &send};
        const XAUDIO2_VOICE_SENDS* sendList = parent ? &sends : nullptr;
        if (FAILED(engine->CreateSubmixVoice(&bus.voice, kBusChannels, sampleRate, XAUDIO2_VOICE_USEFILTER, bus.stage,
                                             sendList, nullptr)))
            return kInvalidBusHandle;
        buses.push_back(std::move(bus));
        return static_cast<BusHandle>(buses.size());
    }

    void applySpatial(VoiceEntry& entry)
    {
        if (!entry.voice || !entry.target)
            return;
        const SpatialGains gains = computeSpatialGains(listener, entry.position, entry.minDistance, entry.maxDistance);
        float matrix[4] = {};
        if (entry.channels == 1)
        {
            matrix[0] = gains.left;
            matrix[1] = gains.right;
        }
        else if (entry.channels == 2)
        {
            matrix[0] = gains.left;
            matrix[3] = gains.right;
        }
        else
        {
            entry.voice->SetVolume(0.5f * (gains.left + gains.right));
            return;
        }
        entry.voice->SetOutputMatrix(entry.target, entry.channels, kBusChannels, matrix);
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
    impl_->sampleRate = details.InputSampleRate;

    static const char* kCategoryNames[kCategoryCount] = {"Sfx", "Music"};
    for (std::size_t i = 0; i < kCategoryCount; ++i)
    {
        if (impl_->makeBus(kCategoryNames[i], kInvalidBusHandle) == kInvalidBusHandle)
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

    for (auto it = impl_->buses.rbegin(); it != impl_->buses.rend(); ++it)
        if (it->voice)
        {
            it->voice->DestroyVoice();
            it->voice = nullptr;
        }
    impl_->buses.clear();
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

    BusHandle busHandle = params.bus;
    if (busHandle == kInvalidBusHandle)
        busHandle = static_cast<BusHandle>(static_cast<std::size_t>(params.category) + 1);
    Impl::Bus* bus = impl_->find(busHandle);
    if (!bus)
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

    XAUDIO2_SEND_DESCRIPTOR sendDesc{0, bus->voice};
    XAUDIO2_VOICE_SENDS sends{1, &sendDesc};

    IXAudio2SourceVoice* voice = nullptr;
    if (FAILED(impl_->engine->CreateSourceVoice(&voice, &wfx, 0, kMaxFreqRatio, callback.get(), &sends, nullptr)))
        return kInvalidVoiceHandle;

    XAUDIO2_BUFFER buffer{};
    buffer.AudioBytes = static_cast<UINT32>(clip->pcm.size());
    buffer.pAudioData = clip->pcm.data();
    buffer.LoopCount  = params.loop ? XAUDIO2_LOOP_INFINITE : 0;
    buffer.Flags      = XAUDIO2_END_OF_STREAM;

    Impl::VoiceEntry entry;
    entry.voice       = voice;
    entry.target      = bus->voice;
    entry.spatial     = params.spatial;
    entry.position    = params.position;
    entry.minDistance = params.minDistance;
    entry.maxDistance = params.maxDistance;
    entry.channels    = clip->channels;
    if (entry.spatial)
        impl_->applySpatial(entry);

    if (FAILED(voice->SubmitSourceBuffer(&buffer)) ||
        FAILED(voice->SetVolume(std::max(params.volume, 0.f))) ||
        FAILED(voice->SetFrequencyRatio(std::clamp(params.pitch, XAUDIO2_MIN_FREQ_RATIO, kMaxFreqRatio))) ||
        FAILED(voice->Start(0)))
    {
        voice->DestroyVoice();
        return kInvalidVoiceHandle;
    }

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
    setBusVolume(bus(category), volume);
}

void AudioManager::setListener(const Listener& listener)
{
    impl_->listener = listener;
    for (auto& [handle, entry] : impl_->voices)
        if (entry.spatial)
            impl_->applySpatial(entry);
}

const Listener& AudioManager::listener() const
{
    return impl_->listener;
}

void AudioManager::setPosition(VoiceHandle voice, const math::Vec3& position)
{
    auto it = impl_->voices.find(voice);
    if (it == impl_->voices.end() || !it->second.spatial)
        return;
    it->second.position = position;
    impl_->applySpatial(it->second);
}

BusHandle AudioManager::bus(Category category) const
{
    return static_cast<BusHandle>(static_cast<std::size_t>(category) + 1);
}

BusHandle AudioManager::createBus(std::string_view name, BusHandle parent)
{
    if (!impl_->engine || name.empty() || findBus(name) != kInvalidBusHandle)
        return kInvalidBusHandle;
    return impl_->makeBus(std::string(name), parent);
}

BusHandle AudioManager::findBus(std::string_view name) const
{
    for (std::size_t i = 0; i < impl_->buses.size(); ++i)
        if (impl_->buses[i].voice && impl_->buses[i].name == name)
            return static_cast<BusHandle>(i + 1);
    return kInvalidBusHandle;
}

void AudioManager::setBusVolume(BusHandle bus, float volume)
{
    if (Impl::Bus* target = impl_->find(bus))
        target->voice->SetVolume(std::max(volume, 0.f));
}

bool AudioManager::setBusEffect(BusHandle bus, Effect effect, float value)
{
    Impl::Bus* target = impl_->find(bus);
    if (!target)
        return false;

    if (effect == Effect::Reverb)
    {
        IUnknown* apo = nullptr;
        if (FAILED(XAudio2CreateReverb(&apo)))
            return false;
        XAUDIO2_EFFECT_DESCRIPTOR descriptor{apo, TRUE, kBusChannels};
        XAUDIO2_EFFECT_CHAIN chain{1, &descriptor};
        const HRESULT chainResult = target->voice->SetEffectChain(&chain);
        apo->Release();
        if (FAILED(chainResult))
            return false;
        XAUDIO2FX_REVERB_I3DL2_PARAMETERS preset = XAUDIO2FX_I3DL2_PRESET_LARGEROOM;
        XAUDIO2FX_REVERB_PARAMETERS native{};
        ReverbConvertI3DL2ToNative(&preset, &native);
        native.WetDryMix = std::clamp(value, 0.f, 1.f) * 100.f;
        return SUCCEEDED(target->voice->SetEffectParameters(0, &native, sizeof(native)));
    }

    XAUDIO2_FILTER_PARAMETERS filter{};
    filter.Type = effect == Effect::LowPass ? LowPassFilter : HighPassFilter;
    const float hz = std::clamp(value, 10.f, static_cast<float>(impl_->sampleRate) * 0.25f);
    filter.Frequency = std::min(2.f * std::sin(3.14159265f * hz / static_cast<float>(impl_->sampleRate)), XAUDIO2_MAX_FILTER_FREQUENCY);
    filter.OneOverQ = 1.f;
    return SUCCEEDED(target->voice->SetFilterParameters(&filter));
}

void AudioManager::clearBusEffects(BusHandle bus)
{
    Impl::Bus* target = impl_->find(bus);
    if (!target)
        return;
    target->voice->SetEffectChain(nullptr);
    XAUDIO2_FILTER_PARAMETERS filter{LowPassFilter, XAUDIO2_MAX_FILTER_FREQUENCY, 1.f};
    target->voice->SetFilterParameters(&filter);
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

#include <string>

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

    struct Bus
    {
        std::string name;
        std::unique_ptr<ma_sound_group> group;
        BusHandle parent = kInvalidBusHandle;
    };

    bool ready = false;
    ma_engine engine{};
    std::vector<Bus> buses;
    Listener listener;
    std::unordered_map<VoiceHandle, std::unique_ptr<Voice>> voices;
    VoiceHandle nextHandle = 1;

    Bus* find(BusHandle handle)
    {
        return handle != kInvalidBusHandle && handle <= buses.size() && buses[handle - 1].group ? &buses[handle - 1]
                                                                                                  : nullptr;
    }

    BusHandle makeBus(const std::string& name, BusHandle parentHandle)
    {
        Bus* parent = parentHandle != kInvalidBusHandle ? find(parentHandle) : nullptr;
        if (parentHandle != kInvalidBusHandle && !parent)
            return kInvalidBusHandle;
        Bus bus;
        bus.name   = name;
        bus.parent = parentHandle;
        bus.group  = std::make_unique<ma_sound_group>();
        if (ma_sound_group_init(&engine, 0, parent ? parent->group.get() : nullptr, bus.group.get()) != MA_SUCCESS)
            return kInvalidBusHandle;
        buses.push_back(std::move(bus));
        return static_cast<BusHandle>(buses.size());
    }

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
    static const char* kCategoryNames[kCategoryCount] = {"Sfx", "Music"};
    for (std::size_t i = 0; i < kCategoryCount; ++i)
        if (impl_->makeBus(kCategoryNames[i], kInvalidBusHandle) == kInvalidBusHandle)
        {
            impl_->buses.clear();
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
    for (auto it = impl_->buses.rbegin(); it != impl_->buses.rend(); ++it)
        if (it->group)
            ma_sound_group_uninit(it->group.get());
    impl_->buses.clear();
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
    BusHandle busHandle = params.bus;
    if (busHandle == kInvalidBusHandle)
        busHandle = static_cast<BusHandle>(static_cast<std::size_t>(params.category) + 1);
    Impl::Bus* bus = impl_->find(busHandle);
    if (!bus)
        return kInvalidVoiceHandle;

    auto voice  = std::make_unique<Impl::Voice>();
    voice->clip = clip;
    voice->loop = params.loop;

    ma_audio_buffer_config config =
        ma_audio_buffer_config_init(ma_format_s16, clip->channels, clip->frameCount(), clip->pcm.data(), nullptr);
    config.sampleRate = clip->sampleRate;
    if (ma_audio_buffer_init(&config, &voice->buffer) != MA_SUCCESS)
        return kInvalidVoiceHandle;
    const ma_uint32 soundFlags = params.spatial ? 0 : MA_SOUND_FLAG_NO_SPATIALIZATION;
    if (ma_sound_init_from_data_source(&impl_->engine, &voice->buffer, soundFlags, bus->group.get(), &voice->sound) !=
        MA_SUCCESS)
    {
        ma_audio_buffer_uninit(&voice->buffer);
        return kInvalidVoiceHandle;
    }
    ma_sound_set_volume(&voice->sound, std::max(params.volume, 0.f));
    ma_sound_set_pitch(&voice->sound, std::max(params.pitch, 0.01f));
    ma_sound_set_looping(&voice->sound, params.loop ? MA_TRUE : MA_FALSE);
    if (params.spatial)
    {
        ma_sound_set_position(&voice->sound, params.position.x, params.position.y, params.position.z);
        ma_sound_set_min_distance(&voice->sound, std::max(params.minDistance, 0.01f));
        ma_sound_set_max_distance(&voice->sound, std::max(params.maxDistance, params.minDistance + 0.01f));
        ma_sound_set_attenuation_model(&voice->sound, ma_attenuation_model_inverse);
    }
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
    setBusVolume(bus(category), volume);
}

void AudioManager::setListener(const Listener& listener)
{
    impl_->listener = listener;
    if (!impl_->ready)
        return;
    ma_engine_listener_set_position(&impl_->engine, 0, listener.position.x, listener.position.y, listener.position.z);
    ma_engine_listener_set_direction(&impl_->engine, 0, listener.forward.x, listener.forward.y, listener.forward.z);
    ma_engine_listener_set_world_up(&impl_->engine, 0, listener.up.x, listener.up.y, listener.up.z);
}

const Listener& AudioManager::listener() const
{
    return impl_->listener;
}

void AudioManager::setPosition(VoiceHandle voice, const math::Vec3& position)
{
    const auto it = impl_->voices.find(voice);
    if (it != impl_->voices.end())
        ma_sound_set_position(&it->second->sound, position.x, position.y, position.z);
}

BusHandle AudioManager::bus(Category category) const
{
    return static_cast<BusHandle>(static_cast<std::size_t>(category) + 1);
}

BusHandle AudioManager::createBus(std::string_view name, BusHandle parent)
{
    if (!impl_->ready || name.empty() || findBus(name) != kInvalidBusHandle)
        return kInvalidBusHandle;
    return impl_->makeBus(std::string(name), parent);
}

BusHandle AudioManager::findBus(std::string_view name) const
{
    for (std::size_t i = 0; i < impl_->buses.size(); ++i)
        if (impl_->buses[i].group && impl_->buses[i].name == name)
            return static_cast<BusHandle>(i + 1);
    return kInvalidBusHandle;
}

void AudioManager::setBusVolume(BusHandle bus, float volume)
{
    if (Impl::Bus* target = impl_->find(bus))
        ma_sound_group_set_volume(target->group.get(), std::max(volume, 0.f));
}

bool AudioManager::setBusEffect(BusHandle, Effect, float)
{
    return false;
}

void AudioManager::clearBusEffects(BusHandle) {}

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
