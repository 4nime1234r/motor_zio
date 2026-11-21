#pragma once

#include <cstddef>

namespace zio::scripting
{

struct EmbeddedFile
{
    const char* name;
    const unsigned char* data;
    std::size_t size;
};

const EmbeddedFile* embeddedBridgeFiles(int& count);

}
