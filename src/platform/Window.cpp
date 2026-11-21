#include "zio/platform/Window.h"

#if defined(ZIO_PLATFORM_WINDOWS)

#define WIN32_LEAN_AND_MEAN
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <windowsx.h>

#include <algorithm>
#include <string>
#include <vector>

#include "platform/WindowIcon.h"
#include "zio/events/EventBus.h"
#include "zio/events/Types.h"

namespace zio::platform
{

namespace
{

events::KeyCode translateKey(WPARAM vk, LPARAM lParam)
{
    const bool extended  = (lParam & (1 << 24)) != 0;
    const UINT scancode   = static_cast<UINT>((lParam >> 16) & 0xFF);

    switch (vk)
    {
    case VK_SHIFT:
    {
        const UINT full = MapVirtualKeyW(scancode, MAPVK_VSC_TO_VK_EX);
        return full == VK_RSHIFT ? events::KeyCode::RightShift : events::KeyCode::LeftShift;
    }
    case VK_CONTROL:
        return extended ? events::KeyCode::RightCtrl : events::KeyCode::LeftCtrl;
    case VK_MENU:
        return extended ? events::KeyCode::RightAlt : events::KeyCode::LeftAlt;

    case VK_ESCAPE:
        return events::KeyCode::Escape;
    case VK_TAB:
        return events::KeyCode::Tab;
    case VK_RETURN:
        return events::KeyCode::Enter;
    case VK_BACK:
        return events::KeyCode::Backspace;
    case VK_SPACE:
        return events::KeyCode::Space;
    case VK_DELETE:
        return events::KeyCode::Delete;
    case VK_INSERT:
        return events::KeyCode::Insert;
    case VK_CAPITAL:
        return events::KeyCode::CapsLock;
    case VK_HOME:
        return events::KeyCode::Home;
    case VK_END:
        return events::KeyCode::End;
    case VK_PRIOR:
        return events::KeyCode::PageUp;
    case VK_NEXT:
        return events::KeyCode::PageDown;
    case VK_LEFT:
        return events::KeyCode::Left;
    case VK_RIGHT:
        return events::KeyCode::Right;
    case VK_UP:
        return events::KeyCode::Up;
    case VK_DOWN:
        return events::KeyCode::Down;

    case VK_OEM_MINUS:
        return events::KeyCode::Minus;
    case VK_OEM_PLUS:
        return events::KeyCode::Equal;
    case VK_OEM_4:
        return events::KeyCode::LeftBracket;
    case VK_OEM_6:
        return events::KeyCode::RightBracket;
    case VK_OEM_5:
        return events::KeyCode::BackSlash;
    case VK_OEM_1:
        return events::KeyCode::Semicolon;
    case VK_OEM_7:
        return events::KeyCode::Apostrophe;
    case VK_OEM_3:
        return events::KeyCode::Grave;
    case VK_OEM_COMMA:
        return events::KeyCode::Comma;
    case VK_OEM_PERIOD:
        return events::KeyCode::Period;
    case VK_OEM_2:
        return events::KeyCode::Slash;

    default:
        break;
    }

    if (vk >= 'A' && vk <= 'Z')
        return static_cast<events::KeyCode>(static_cast<int>(events::KeyCode::A) + (vk - 'A'));
    if (vk >= '0' && vk <= '9')
        return static_cast<events::KeyCode>(static_cast<int>(events::KeyCode::Num0) + (vk - '0'));
    if (vk >= VK_F1 && vk <= VK_F12)
        return static_cast<events::KeyCode>(static_cast<int>(events::KeyCode::F1) + (vk - VK_F1));

    return events::KeyCode::Unknown;
}

}

struct Window::Impl
{
    static constexpr std::uint32_t kMinClientWidth  = 320;
    static constexpr std::uint32_t kMinClientHeight = 240;

    static constexpr DWORD kStyle = WS_OVERLAPPEDWINDOW;

    HWND hwnd               = nullptr;
    std::uint32_t width     = 0;
    std::uint32_t height    = 0;
    bool closeRequested     = false;
    float lastMouseX        = 0.f;
    float lastMouseY        = 0.f;
    bool haveLastMouse      = false;

    bool minimized = false;

    std::uint32_t notifiedWidth  = 0;
    std::uint32_t notifiedHeight = 0;

    std::vector<events::KeyCode> heldKeys;
    bool mouseButtonHeld[5] = {};

    int capturedButtonCount = 0;

    void setButtonHeld(events::MouseButton button, bool down)
    {
        const auto index = static_cast<std::size_t>(button);
        if (mouseButtonHeld[index] == down)
            return;
        mouseButtonHeld[index] = down;
        if (down)
        {
            if (capturedButtonCount++ == 0)
                SetCapture(hwnd);
        }
        else if (capturedButtonCount > 0 && --capturedButtonCount == 0)
        {
            ReleaseCapture();
        }
    }

    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        Impl* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (!self)
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        return self->handleMessage(msg, wParam, lParam);
    }

    LRESULT handleMessage(UINT msg, WPARAM wParam, LPARAM lParam)
    {
        auto& bus = events::EventBus::instance();

        switch (msg)
        {
        case WM_CLOSE:
            closeRequested = true;
            return 0;

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        {
            const bool repeat            = (lParam & (1 << 30)) != 0;
            const events::KeyCode code = translateKey(wParam, lParam);
            if (code != events::KeyCode::Unknown && std::find(heldKeys.begin(), heldKeys.end(), code) == heldKeys.end())
                heldKeys.push_back(code);
            bus.postKey(code, true, repeat);
            return 0;
        }
        case WM_KEYUP:
        case WM_SYSKEYUP:
        {
            const events::KeyCode code = translateKey(wParam, lParam);
            heldKeys.erase(std::remove(heldKeys.begin(), heldKeys.end(), code), heldKeys.end());
            bus.postKey(code, false, false);
            return 0;
        }

        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
        {
            events::MouseButton button = msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP ? events::MouseButton::Left
                                          : msg == WM_RBUTTONDOWN || msg == WM_RBUTTONUP
                                              ? events::MouseButton::Right
                                              : events::MouseButton::Middle;
            const bool down = msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN || msg == WM_MBUTTONDOWN;
            setButtonHeld(button, down);
            bus.postMouseButton(button, static_cast<float>(GET_X_LPARAM(lParam)),
                                 static_cast<float>(GET_Y_LPARAM(lParam)), down);
            return 0;
        }

        case WM_XBUTTONDOWN:
        case WM_XBUTTONUP:
        {
            const events::MouseButton button =
                GET_XBUTTON_WPARAM(wParam) == XBUTTON1 ? events::MouseButton::Extra1 : events::MouseButton::Extra2;
            const bool down = msg == WM_XBUTTONDOWN;
            setButtonHeld(button, down);
            bus.postMouseButton(button, static_cast<float>(GET_X_LPARAM(lParam)),
                                 static_cast<float>(GET_Y_LPARAM(lParam)), down);
            return TRUE;
        }

        case WM_MOUSEMOVE:
        {
            const float x = static_cast<float>(GET_X_LPARAM(lParam));
            const float y = static_cast<float>(GET_Y_LPARAM(lParam));
            const float dx = haveLastMouse ? x - lastMouseX : 0.f;
            const float dy = haveLastMouse ? y - lastMouseY : 0.f;
            lastMouseX     = x;
            lastMouseY     = y;
            haveLastMouse  = true;
            bus.postMouseMove(x, y, dx, dy);
            return 0;
        }

        case WM_CAPTURECHANGED:
            capturedButtonCount = 0;
            return 0;

        case WM_KILLFOCUS:
        {
            for (events::KeyCode code : heldKeys)
                bus.postKey(code, false, false);
            heldKeys.clear();

            for (std::size_t i = 0; i < 5; ++i)
            {
                if (mouseButtonHeld[i])
                {
                    mouseButtonHeld[i] = false;
                    bus.postMouseButton(static_cast<events::MouseButton>(i), lastMouseX, lastMouseY, false);
                }
            }
            if (capturedButtonCount != 0)
            {
                capturedButtonCount = 0;
                ReleaseCapture();
            }
            return 0;
        }

        case WM_SIZE:
            if (wParam == SIZE_MINIMIZED)
            {
                minimized = true;
            }
            else
            {
                minimized = false;
                width     = LOWORD(lParam);
                height    = HIWORD(lParam);
            }
            return 0;

        case WM_GETMINMAXINFO:
        {
            RECT rect{0, 0, static_cast<LONG>(kMinClientWidth), static_cast<LONG>(kMinClientHeight)};
            AdjustWindowRect(&rect, kStyle, FALSE);
            auto* info               = reinterpret_cast<MINMAXINFO*>(lParam);
            info->ptMinTrackSize.x   = rect.right - rect.left;
            info->ptMinTrackSize.y   = rect.bottom - rect.top;
            return 0;
        }

        case WM_MOUSEWHEEL:
        {
            POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ScreenToClient(hwnd, &pt);
            const float notches = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / WHEEL_DELTA;
            bus.postMouseScroll(static_cast<float>(pt.x), static_cast<float>(pt.y), 0.f, notches);
            return 0;
        }

        default:
            break;
        }

        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    bool create(std::string_view title, std::uint32_t w, std::uint32_t h)
    {
        static const wchar_t* kClassName = L"ZioWindow";
        static bool classRegistered      = false;
        if (!classRegistered)
        {
            WNDCLASSEXW wc{};
            wc.cbSize        = sizeof(wc);
            wc.style         = CS_OWNDC;
            wc.lpfnWndProc   = &Impl::wndProc;
            wc.hInstance     = GetModuleHandleW(nullptr);
            wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
            loadWindowIcons(wc.hInstance, wc.hIcon, wc.hIconSm);
            wc.lpszClassName = kClassName;
            if (!RegisterClassExW(&wc))
                return false;
            classRegistered = true;
        }

        RECT rect{0, 0, static_cast<LONG>(w), static_cast<LONG>(h)};
        AdjustWindowRect(&rect, kStyle, FALSE);

        std::wstring wideTitle(title.begin(), title.end());
        hwnd = CreateWindowExW(0, kClassName, wideTitle.c_str(), kStyle, CW_USEDEFAULT, CW_USEDEFAULT,
                                rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr,
                                GetModuleHandleW(nullptr), nullptr);
        if (!hwnd)
            return false;

        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);

        width          = w;
        height         = h;
        notifiedWidth  = w;
        notifiedHeight = h;
        minimized      = false;
        closeRequested = false;
        haveLastMouse  = false;
        heldKeys.clear();
        for (bool& held : mouseButtonHeld)
            held = false;
        capturedButtonCount = 0;
        return true;
    }

    void destroy()
    {
        if (hwnd)
        {
            if (capturedButtonCount != 0)
                ReleaseCapture();
            DestroyWindow(hwnd);
            hwnd = nullptr;
        }
        width = height = notifiedWidth = notifiedHeight = 0;
        minimized = false;
        heldKeys.clear();
        for (bool& held : mouseButtonHeld)
            held = false;
        capturedButtonCount = 0;
    }
};

Window::Window() : impl_(std::make_unique<Impl>()) {}
Window::~Window()
{
    destroy();
}

Window& Window::instance()
{
    static Window global;
    return global;
}

bool Window::create(std::string_view title, std::uint32_t width, std::uint32_t height)
{
    destroy();
    return impl_->create(title, width, height);
}

void Window::destroy()
{
    impl_->destroy();
}

bool Window::isOpen() const
{
    return impl_->hwnd != nullptr;
}

void Window::pumpMessages()
{
    if (!impl_->hwnd)
        return;

    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

bool Window::closeRequested() const
{
    return impl_->closeRequested;
}

bool Window::isMinimized() const
{
    return impl_->minimized;
}

bool Window::consumeResize(std::uint32_t& outWidth, std::uint32_t& outHeight)
{
    if (impl_->minimized)
        return false;
    if (impl_->width == impl_->notifiedWidth && impl_->height == impl_->notifiedHeight)
        return false;

    outWidth              = impl_->width;
    outHeight             = impl_->height;
    impl_->notifiedWidth  = impl_->width;
    impl_->notifiedHeight = impl_->height;
    return true;
}

void* Window::nativeHandle() const
{
    return impl_->hwnd;
}

std::uint32_t Window::width() const
{
    return impl_->width;
}

std::uint32_t Window::height() const
{
    return impl_->height;
}

}

#else

namespace zio::platform
{

struct Window::Impl
{
};

Window::Window() : impl_(std::make_unique<Impl>()) {}
Window::~Window() = default;

Window& Window::instance()
{
    static Window global;
    return global;
}

bool Window::create(std::string_view, std::uint32_t, std::uint32_t)
{
    return false;
}

void Window::destroy() {}

bool Window::isOpen() const
{
    return false;
}

void Window::pumpMessages() {}

bool Window::closeRequested() const
{
    return false;
}

bool Window::isMinimized() const
{
    return false;
}

bool Window::consumeResize(std::uint32_t&, std::uint32_t&)
{
    return false;
}

void* Window::nativeHandle() const
{
    return nullptr;
}

std::uint32_t Window::width() const
{
    return 0;
}

std::uint32_t Window::height() const
{
    return 0;
}

}

#endif
