#pragma once

#include "zio/core/Config.h"
#include "zio/core/Platform.h"

namespace zio::core
{

class ZIO_API Application
{
public:
    Application();
    virtual ~Application();

    Application(const Application&)            = delete;
    Application& operator=(const Application&) = delete;

    static Application* instance();

    bool run(AppConfig config);

    void requestExit();

    bool isRunning() const { return running_; }
    bool isRenderReady() const { return renderReady_; }
    bool isAudioReady() const { return audioReady_; }
    const AppConfig& config() const { return config_; }

protected:
    virtual bool onInit() = 0;

    virtual void onBeforeTick(float dt) {}

    virtual void onRender(float dt) {}

    virtual void onAfterTick(float dt) {}

    virtual void onShutdown() {}

private:
    static Application* instance_;

    AppConfig config_;
    bool running_     = false;
    bool renderReady_ = false;
    bool audioReady_  = false;
};

}
