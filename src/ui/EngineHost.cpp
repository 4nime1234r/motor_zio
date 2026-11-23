#include "zio/ui/EngineHost.h"

#include <string>

#include "zio/events/EventBus.h"
#include "zio/platform/Window.h"
#include "zio/ui/BatchPainter.h"

#if defined(ZIO_PLATFORM_WINDOWS)

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>

#else

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#endif

namespace zio::ui
{

namespace
{

#if defined(ZIO_PLATFORM_WINDOWS)

std::string readClipboard()
{
    std::string result;
    if (!OpenClipboard(nullptr))
        return result;
    if (HANDLE handle = GetClipboardData(CF_UNICODETEXT))
    {
        if (const auto* wide = static_cast<const wchar_t*>(GlobalLock(handle)))
        {
            const int size = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
            if (size > 1)
            {
                result.resize(static_cast<std::size_t>(size - 1));
                WideCharToMultiByte(CP_UTF8, 0, wide, -1, result.data(), size, nullptr, nullptr);
            }
            GlobalUnlock(handle);
        }
    }
    CloseClipboard();
    return result;
}

void writeClipboard(const std::string& text)
{
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (size <= 0 || !OpenClipboard(nullptr))
        return;
    EmptyClipboard();
    if (HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, static_cast<SIZE_T>(size) * sizeof(wchar_t)))
    {
        if (auto* wide = static_cast<wchar_t*>(GlobalLock(memory)))
        {
            MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wide, size);
            GlobalUnlock(memory);
            if (!SetClipboardData(CF_UNICODETEXT, memory))
                GlobalFree(memory);
        }
        else
        {
            GlobalFree(memory);
        }
    }
    CloseClipboard();
}

#else

std::string readClipboard()
{
    auto* window = static_cast<GLFWwindow*>(platform::Window::instance().nativeHandle());
    const char* text = window ? glfwGetClipboardString(window) : nullptr;
    return text ? std::string(text) : std::string();
}

void writeClipboard(const std::string& text)
{
    if (auto* window = static_cast<GLFWwindow*>(platform::Window::instance().nativeHandle()))
        glfwSetClipboardString(window, text.c_str());
}

#endif

platform::CursorShape toShape(Cursor cursor)
{
    switch (cursor)
    {
    case Cursor::IBeam:
        return platform::CursorShape::IBeam;
    case Cursor::ResizeHorizontal:
        return platform::CursorShape::ResizeHorizontal;
    case Cursor::ResizeVertical:
        return platform::CursorShape::ResizeVertical;
    case Cursor::Hand:
        return platform::CursorShape::Hand;
    case Cursor::Arrow:
        break;
    }
    return platform::CursorShape::Arrow;
}

}

struct EngineHost::Impl
{
    Context& context;
    BatchPainter painter;
    Modifiers modifiers;
    float scale = 1.f;
    std::vector<events::ListenerId> listeners;

    explicit Impl(Context& ctx) : context(ctx) {}
};

EngineHost::EngineHost(Context& context) : impl_(std::make_unique<Impl>(context))
{
    using events::KeyCode;
    auto& bus = events::EventBus::instance();
    Impl* self = impl_.get();

    context.setClipboardHandlers(&readClipboard, &writeClipboard);

    self->listeners.push_back(bus.onMouseMove([self](const events::MouseMoveEvent& e) {
        self->context.mouseMove({e.x / self->scale, e.y / self->scale});
    }));
    self->listeners.push_back(bus.onMouseButton([self](const events::MouseButtonEvent& e) {
        self->context.mouseButton(e.button, e.down, {e.x / self->scale, e.y / self->scale}, self->modifiers);
    }));
    self->listeners.push_back(bus.onMouseScroll([self](const events::MouseScrollEvent& e) {
        self->context.mouseScroll({e.x / self->scale, e.y / self->scale}, {e.scrollX, e.scrollY}, self->modifiers);
    }));
    self->listeners.push_back(bus.onKey([self](const events::KeyEvent& e) {
        switch (e.code)
        {
        case KeyCode::LeftShift:
        case KeyCode::RightShift:
            self->modifiers.shift = e.down;
            break;
        case KeyCode::LeftCtrl:
        case KeyCode::RightCtrl:
            self->modifiers.ctrl = e.down;
            break;
        case KeyCode::LeftAlt:
        case KeyCode::RightAlt:
            self->modifiers.alt = e.down;
            break;
        default:
            break;
        }
        self->context.key(e.code, e.down, e.repeat, self->modifiers);
    }));
    self->listeners.push_back(bus.onText([self](const events::TextEvent& e) { self->context.text(e.codepoint); }));
}

EngineHost::~EngineHost()
{
    auto& bus = events::EventBus::instance();
    for (events::ListenerId id : impl_->listeners)
        bus.unsubscribe(id);
}

void EngineHost::frame(float dt)
{
    auto& window = platform::Window::instance();
    impl_->scale = window.dpiScale();
    impl_->context.setScale(impl_->scale);
    impl_->painter.setScale(impl_->scale);
    impl_->context.setSize({static_cast<float>(window.width()) / impl_->scale, static_cast<float>(window.height()) / impl_->scale});
    impl_->context.update(dt);
    window.setCursor(toShape(impl_->context.cursor()));
}

void EngineHost::draw()
{
    const Vec2 size = impl_->context.size();
    if (impl_->painter.begin(size.x, size.y))
        impl_->context.draw(impl_->painter);
}

}
