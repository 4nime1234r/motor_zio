#include "zio/core/Application.h"

#include <chrono>
#include <thread>

#if defined(ZIO_PLATFORM_WINDOWS)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <timeapi.h>
#endif

#include "zio/audio/AudioManager.h"
#include "zio/events/EventBus.h"
#include "zio/input/InputManager.h"
#include "zio/jobs/JobSystem.h"
#include "zio/physics/World.h"
#include "zio/physics3d/World3D.h"
#include "zio/platform/Window.h"
#include "zio/prefs/Prefs.h"
#include "zio/render/Device.h"
#include "zio/scene/SceneStack.h"

namespace zio::core
{

Application* Application::instance_ = nullptr;

Application::Application()
{
    ZIO_ASSERT(instance_ == nullptr, "zio::core::Application: only one Application may exist per process");
    instance_ = this;
}

Application::~Application()
{
    if (instance_ == this)
        instance_ = nullptr;
}

Application* Application::instance()
{
    return instance_;
}

void Application::requestExit()
{
    running_ = false;
}

bool Application::run(AppConfig config)
{
    config_ = std::move(config);

    if (config_.enableRendering)
    {
        if (platform::Window::instance().create(config_.title, config_.windowWidth, config_.windowHeight))
        {
            renderReady_ = render::Device::instance().init(platform::Window::instance().nativeHandle(),
                                                             config_.windowWidth, config_.windowHeight, config_.msaaSamples);
        }
        if (!renderReady_)
        {
            render::Device::instance().shutdown();
            platform::Window::instance().destroy();
        }
        if (!renderReady_ && config_.requireRendering)
            return false;
    }

    if (config_.enableAudio)
        audioReady_ = audio::AudioManager::instance().init();

    if (!onInit())
    {
        if (audioReady_)
            audio::AudioManager::instance().shutdown();
        if (renderReady_)
        {
            render::Device::instance().shutdown();
            platform::Window::instance().destroy();
        }
        return false;
    }

    const std::uint32_t fps = config_.targetFps == 0 ? 60 : config_.targetFps;
    const float dt          = 1.f / static_cast<float>(fps);
    const auto tickDuration  = std::chrono::duration<float>(dt);

#if defined(ZIO_PLATFORM_WINDOWS)
    timeBeginPeriod(1);
#endif

    running_ = true;
    while (running_)
    {
        const auto tickStart = std::chrono::steady_clock::now();

        if (renderReady_)
        {
            platform::Window::instance().pumpMessages();
            if (platform::Window::instance().closeRequested())
            {
                if (onCloseRequested())
                    requestExit();
                else
                    platform::Window::instance().cancelClose();
            }

            std::uint32_t newWidth = 0, newHeight = 0;
            if (platform::Window::instance().consumeResize(newWidth, newHeight))
                render::Device::instance().resize(newWidth, newHeight);
        }

        onBeforeTick(dt);

        jobs::JobSystem::instance().pumpMainThread();
        audio::AudioManager::instance().update();
        input::InputManager::instance().update();
        events::EventBus::instance().dispatchPending();
        if (config_.enablePhysics)
        {
            physics::World::instance().step(dt);
            physics3d::World3D::instance().step(dt);
        }
        scene::SceneStack::instance().update(dt);

        if (renderReady_ && !platform::Window::instance().isMinimized())
            onRender(dt);

        onAfterTick(dt);

        const auto elapsed = std::chrono::steady_clock::now() - tickStart;
        const auto sleepFor = std::chrono::duration_cast<std::chrono::steady_clock::duration>(tickDuration) - elapsed;
        if (sleepFor > std::chrono::steady_clock::duration::zero())
            std::this_thread::sleep_for(sleepFor);
    }

#if defined(ZIO_PLATFORM_WINDOWS)
    timeEndPeriod(1);
#endif

    onShutdown();

    prefs::Prefs::instance().flush();
    if (audioReady_)
        audio::AudioManager::instance().shutdown();
    if (renderReady_)
    {
        render::Device::instance().shutdown();
        platform::Window::instance().destroy();
    }

    return true;
}

}
