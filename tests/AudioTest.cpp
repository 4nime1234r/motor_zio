#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <thread>
#include <vector>

#include "zio/audio/AudioManager.h"

using namespace zio;

namespace
{

int failures = 0;

void check(bool condition, const char* name)
{
    std::printf("%s %s\n", condition ? "[ok]  " : "[FALLO]", name);
    if (!condition)
        ++failures;
}

std::shared_ptr<audio::Clip> tone(float seconds)
{
    auto clip        = std::make_shared<audio::Clip>();
    clip->channels   = 1;
    clip->sampleRate = 22050;
    const std::size_t frames = static_cast<std::size_t>(seconds * 22050.f);
    clip->pcm.resize(frames * 2);
    for (std::size_t i = 0; i < frames; ++i)
    {
        const auto sample = static_cast<std::int16_t>(std::sin(static_cast<float>(i) * 0.05f) * 8000.f);
        clip->pcm[i * 2]     = static_cast<std::uint8_t>(sample & 0xFF);
        clip->pcm[i * 2 + 1] = static_cast<std::uint8_t>((sample >> 8) & 0xFF);
    }
    return clip;
}

std::filesystem::path writeWav(const std::filesystem::path& path, float seconds)
{
    const std::uint32_t rate   = 22050;
    const std::uint32_t frames = static_cast<std::uint32_t>(seconds * static_cast<float>(rate));
    const std::uint32_t bytes  = frames * 2;
    std::ofstream out(path, std::ios::binary);
    const auto put32 = [&out](std::uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
    const auto put16 = [&out](std::uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); };
    out.write("RIFF", 4);
    put32(36 + bytes);
    out.write("WAVEfmt ", 8);
    put32(16);
    put16(1);
    put16(1);
    put32(rate);
    put32(rate * 2);
    put16(2);
    put16(16);
    out.write("data", 4);
    put32(bytes);
    for (std::uint32_t i = 0; i < frames; ++i)
    {
        const auto sample = static_cast<std::int16_t>(std::sin(static_cast<float>(i) * 0.05f) * 8000.f);
        out.write(reinterpret_cast<const char*>(&sample), 2);
    }
    return path;
}

}

int main()
{
    audio::Listener listener;
    const auto right = audio::computeSpatialGains(listener, {5.f, 0.f, 0.f}, 1.f, 50.f);
    const auto left  = audio::computeSpatialGains(listener, {-5.f, 0.f, 0.f}, 1.f, 50.f);
    const auto front = audio::computeSpatialGains(listener, {0.f, 0.f, -5.f}, 1.f, 50.f);
    check(right.right > right.left * 3.f, "un sonido a la derecha suena mas por la derecha");
    check(left.left > left.right * 3.f, "un sonido a la izquierda suena mas por la izquierda");
    check(std::fabs(front.left - front.right) < 0.01f, "un sonido al frente suena igual por los dos lados");
    const auto near = audio::computeSpatialGains(listener, {0.f, 0.f, -2.f}, 1.f, 50.f);
    const auto far  = audio::computeSpatialGains(listener, {0.f, 0.f, -20.f}, 1.f, 50.f);
    check(near.left > far.left, "un sonido lejano suena mas bajo");
    const auto beyond = audio::computeSpatialGains(listener, {0.f, 0.f, -80.f}, 1.f, 50.f);
    check(beyond.left == 0.f && beyond.right == 0.f, "pasado el alcance maximo no se oye");
    audio::Listener turned;
    turned.forward = {1.f, 0.f, 0.f};
    const auto behindTurn = audio::computeSpatialGains(turned, {0.f, 0.f, 5.f}, 1.f, 50.f);
    check(behindTurn.right > behindTurn.left, "girar al oyente cambia de lado el sonido");

    const auto wav = writeWav(std::filesystem::temp_directory_path() / "zio_stream_test.wav", 3.f);
    {
        auto stream = audio::AudioStream::open(wav.string());
        check(stream != nullptr, "se abre un archivo de audio como flujo");
        if (stream)
        {
            check(stream->channels() == 1 && stream->sampleRate() == 22050, "el flujo informa canales y frecuencia");
            std::vector<std::uint8_t> chunk(4096 * 2);
            std::size_t total = 0, read = 0;
            while ((read = stream->read(chunk.data(), 4096)) > 0)
                total += read;
            check(total == 66150, "el flujo entrega todos los cuadros en trozos");
            check(stream->rewind() && stream->read(chunk.data(), 4096) == 4096, "el flujo se puede rebobinar");
        }
        check(audio::AudioStream::open((wav.string() + ".no_existe").c_str()) == nullptr, "un archivo que no existe no abre flujo");
    }

    auto& manager = audio::AudioManager::instance();
    if (!manager.init())
    {
        std::printf("[omitida] no hay dispositivo de audio en este equipo\n");
        std::printf("%s\n", failures == 0 ? "TODO OK" : "HAY FALLOS");
        return failures == 0 ? 0 : 1;
    }

    const audio::BusHandle effects = manager.createBus("efectos");
    const audio::BusHandle voices  = manager.createBus("voces", effects);
    check(effects != audio::kInvalidBusHandle && voices != audio::kInvalidBusHandle, "se crean buses y sub-buses");
    check(manager.findBus("voces") == voices, "findBus encuentra un bus por nombre");
    check(manager.createBus("voces") == audio::kInvalidBusHandle, "no se repiten nombres de bus");
    check(manager.createBus("huerfano", 9999) == audio::kInvalidBusHandle, "un padre que no existe da error");

    audio::PlayParams params;
    params.bus      = voices;
    params.spatial  = true;
    params.position = {4.f, 0.f, 0.f};
    params.loop     = true;
    const audio::VoiceHandle voice = manager.play(tone(1.f), params);
    check(voice != audio::kInvalidVoiceHandle && manager.isPlaying(voice), "se reproduce un sonido posicional en un bus");
    manager.setListener(listener);
    manager.setPosition(voice, {-4.f, 0.f, 0.f});
    manager.setBusVolume(effects, 0.5f);
    check(manager.setBusEffect(voices, audio::Effect::LowPass, 2000.f), "se aplica un filtro paso bajo al bus");
    check(manager.setBusEffect(voices, audio::Effect::HighPass, 200.f), "se aplica un filtro paso alto al bus");
    check(manager.setBusEffect(voices, audio::Effect::Reverb, 0.4f), "se aplica reverberacion al bus");
    manager.clearBusEffects(voices);
    manager.update();
    check(manager.isPlaying(voice), "el sonido sigue tras cambiar efectos");
    manager.stop(voice);
    check(!manager.isPlaying(voice), "stop detiene el sonido");

    audio::PlayParams music;
    music.category = audio::Category::Music;
    music.loop     = true;
    const audio::VoiceHandle streamed = manager.playStream(wav.string(), music);
    check(streamed != audio::kInvalidVoiceHandle && manager.isPlaying(streamed), "se reproduce musica en streaming");
    for (int i = 0; i < 40; ++i)
    {
        manager.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    check(manager.isPlaying(streamed), "la musica en bucle sigue sonando pasada su duracion");
    manager.stop(streamed);
    check(manager.playStream(wav.string() + ".no_existe", music) == audio::kInvalidVoiceHandle, "streaming de un archivo que no existe da error");
    manager.shutdown();

    std::printf("%s\n", failures == 0 ? "TODO OK" : "HAY FALLOS");
    return failures == 0 ? 0 : 1;
}
