#pragma once

#include <filesystem>
#include <optional>

#include "zio/pack/Package.h"

namespace zio_player
{

struct LaunchArgs
{
    std::filesystem::path package;
    std::optional<zio::pack::Key32> key;
};

std::optional<LaunchArgs> detectLaunch(int argc, char** argv);

int run(const LaunchArgs& args);

}
