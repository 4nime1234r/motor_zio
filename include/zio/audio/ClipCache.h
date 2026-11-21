#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "zio/audio/Clip.h"
#include "zio/core/Platform.h"

namespace zio::audio
{

using CachedClipLoadCallback = std::function<void(std::shared_ptr<Clip>)>;

class ZIO_API ClipCache
{
public:
    ClipCache();
    ~ClipCache();

    ClipCache(const ClipCache&)            = delete;
    ClipCache& operator=(const ClipCache&) = delete;

    static ClipCache& instance();

    std::shared_ptr<Clip> loadSync(std::string_view path);

    void loadAsync(std::string path, CachedClipLoadCallback onLoaded);

    std::size_t liveCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
