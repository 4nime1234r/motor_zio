#pragma once

#include <cstddef>

namespace zio::scripting
{

struct SdkFile
{
    const char* path;
    const unsigned char* data;
    std::size_t size;
};

const SdkFile* sdkFiles(int& count);

}
