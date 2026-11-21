#pragma once

#include <cstdint>
#include <string>

namespace zio::core
{

struct AppConfig
{
    std::string title        = "Zio Application";
    std::uint32_t windowWidth  = 1280;
    std::uint32_t windowHeight = 720;

    std::uint32_t targetFps = 60;

    bool enableRendering = true;

    bool requireRendering = false;

    bool enableAudio = true;

    bool enablePhysics = true;
};

}
