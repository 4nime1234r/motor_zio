#pragma once

#include <memory>
#include <string_view>

#include "zio/audio/Clip.h"
#include "zio/audio/Types.h"
#include "zio/core/Platform.h"

namespace zio::audio
{

class ZIO_API AudioManager
{
public:
    AudioManager();
    ~AudioManager();

    AudioManager(const AudioManager&)            = delete;
    AudioManager& operator=(const AudioManager&) = delete;

    static AudioManager& instance();

    bool init();
    void shutdown();
    bool isInitialized() const;

    VoiceHandle play(std::shared_ptr<Clip> clip, const PlayParams& params = {});

    void stop(VoiceHandle voice);
    void setVolume(VoiceHandle voice, float volume);
    void setPitch(VoiceHandle voice, float pitch);
    bool isPlaying(VoiceHandle voice) const;

    void setCategoryVolume(Category category, float volume);

    void setListener(const Listener& listener);
    const Listener& listener() const;
    void setPosition(VoiceHandle voice, const math::Vec3& position);

    BusHandle bus(Category category) const;
    BusHandle createBus(std::string_view name, BusHandle parent = kInvalidBusHandle);
    BusHandle findBus(std::string_view name) const;
    void setBusVolume(BusHandle bus, float volume);
    bool setBusEffect(BusHandle bus, Effect effect, float value);
    void clearBusEffects(BusHandle bus);

    void update();

private:
    friend class VoiceCallback;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
