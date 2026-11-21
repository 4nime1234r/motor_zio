#include "zio/audio/ClipCache.h"

#include <unordered_map>

namespace zio::audio
{

struct ClipCache::Impl
{
    std::unordered_map<std::string, std::weak_ptr<Clip>> cache;

    std::shared_ptr<Clip> findLive(const std::string& path)
    {
        auto it = cache.find(path);
        if (it == cache.end())
            return nullptr;
        return it->second.lock();
    }
};

ClipCache::ClipCache() : impl_(std::make_unique<Impl>()) {}
ClipCache::~ClipCache() = default;

ClipCache& ClipCache::instance()
{
    static ClipCache global;
    return global;
}

std::shared_ptr<Clip> ClipCache::loadSync(std::string_view path)
{
    const std::string key(path);
    if (auto existing = impl_->findLive(key))
        return existing;

    auto decoded = decodeAudioFile(path);
    if (!decoded)
        return nullptr;

    auto clip     = std::make_shared<Clip>(std::move(*decoded));
    impl_->cache[key] = clip;
    return clip;
}

void ClipCache::loadAsync(std::string path, CachedClipLoadCallback onLoaded)
{
    if (auto existing = impl_->findLive(path))
    {
        onLoaded(std::move(existing));
        return;
    }

    loadClipAsync(path, [this, path, onLoaded = std::move(onLoaded)](std::optional<Clip> decoded) {
        if (!decoded)
        {
            onLoaded(nullptr);
            return;
        }
        auto clip     = std::make_shared<Clip>(std::move(*decoded));
        impl_->cache[path] = clip;
        onLoaded(std::move(clip));
    });
}

std::size_t ClipCache::liveCount() const
{
    std::size_t count = 0;
    for (auto& [path, weak] : impl_->cache)
        if (!weak.expired())
            ++count;
    return count;
}

}
