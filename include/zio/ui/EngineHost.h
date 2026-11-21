#pragma once

#include <memory>

#include "zio/core/Platform.h"
#include "zio/ui/Context.h"

namespace zio::ui
{

class ZIO_API EngineHost
{
public:
    explicit EngineHost(Context& context);
    ~EngineHost();

    EngineHost(const EngineHost&)            = delete;
    EngineHost& operator=(const EngineHost&) = delete;

    void frame(float dt);
    void draw();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
