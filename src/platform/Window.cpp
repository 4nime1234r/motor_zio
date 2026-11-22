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
    WindowMode windowMode = WindowMode::Windowed;
    WINDOWPLACEMENT savedPlacement{};
    LONG_PTR savedStyle = 0;
    CursorShape cursorShape = CursorShape::Arrow;
    char32_t pendingHighSurrogate = 0;

    static HCURSOR cursorHandle(CursorShape shape)
    {
        switch (shape)
        {
        case CursorShape::IBeam:
            return LoadCursorW(nullptr, IDC_IBEAM);
        case CursorShape::ResizeHorizontal:
            return LoadCursorW(nullptr, IDC_SIZEWE);
        case CursorShape::ResizeVertical:
            return LoadCursorW(nullptr, IDC_SIZENS);
        case CursorShape::Hand:
            return LoadCursorW(nullptr, IDC_HAND);
        case CursorShape::Arrow:
            break;
        }
        return LoadCursorW(nullptr, IDC_ARROW);
    }

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

        case WM_SETCURSOR:
            if (LOWORD(lParam) == HTCLIENT)
            {
                SetCursor(cursorHandle(cursorShape));
                return TRUE;
            }
            break;

        case WM_CHAR:
        {
            char32_t codepoint = static_cast<char32_t>(wParam);
            if (codepoint >= 0xD800 && codepoint <= 0xDBFF)
            {
                pendingHighSurrogate = codepoint;
                return 0;
            }
            if (codepoint >= 0xDC00 && codepoint <= 0xDFFF)
            {
                if (!pendingHighSurrogate)
                    return 0;
                codepoint = 0x10000 + ((pendingHighSurrogate - 0xD800) << 10) + (codepoint - 0xDC00);
                pendingHighSurrogate = 0;
            }
            if (codepoint >= 32 && codepoint != 127)
                bus.postText(codepoint);
            return 0;
        }

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
                if (windowMode != WindowMode::Fullscreen)
                    windowMode = wParam == SIZE_MAXIMIZED ? WindowMode::Maximized : WindowMode::Windowed;
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
        minimized  = false;
        windowMode = WindowMode::Windowed;
        heldKeys.clear();
        for (bool& held : mouseButtonHeld)
            held = false;
        capturedButtonCount = 0;
    }

    void leaveFullscreen()
    {
        SetWindowLongPtrW(hwnd, GWL_STYLE, savedStyle);
        SetWindowPlacement(hwnd, &savedPlacement);
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        windowMode = WindowMode::Windowed;
    }

    bool setMode(WindowMode target)
    {
        if (!hwnd)
            return false;
        if (target == windowMode)
            return true;

        if (target == WindowMode::Fullscreen)
        {
            savedStyle = GetWindowLongPtrW(hwnd, GWL_STYLE);
            savedPlacement.length = sizeof(savedPlacement);
            GetWindowPlacement(hwnd, &savedPlacement);
            MONITORINFO info{};
            info.cbSize = sizeof(info);
            if (!GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &info))
                return false;
            SetWindowLongPtrW(hwnd, GWL_STYLE, (savedStyle & ~static_cast<LONG_PTR>(WS_OVERLAPPEDWINDOW)) | WS_POPUP);
            SetWindowPos(hwnd, HWND_TOP, info.rcMonitor.left, info.rcMonitor.top,
                         info.rcMonitor.right - info.rcMonitor.left, info.rcMonitor.bottom - info.rcMonitor.top,
                         SWP_NOOWNERZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
            windowMode = WindowMode::Fullscreen;
            return true;
        }

        if (windowMode == WindowMode::Fullscreen)
            leaveFullscreen();
        if (target == WindowMode::Maximized)
        {
            ShowWindow(hwnd, SW_MAXIMIZE);
            windowMode = WindowMode::Maximized;
        }
        else
        {
            ShowWindow(hwnd, SW_RESTORE);
            windowMode = WindowMode::Windowed;
        }
        return true;
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

void Window::cancelClose()
{
    impl_->closeRequested = false;
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

void Window::setCursor(CursorShape shape)
{
    if (impl_->cursorShape == shape)
        return;
    impl_->cursorShape = shape;
    if (impl_->hwnd)
    {
        POINT cursor{};
        RECT client{};
        if (GetCursorPos(&cursor) && GetClientRect(impl_->hwnd, &client) && ScreenToClient(impl_->hwnd, &cursor) &&
            PtInRect(&client, cursor))
            SetCursor(Impl::cursorHandle(shape));
    }
}

void Window::setTitle(std::string_view title)
{
    if (!impl_->hwnd)
        return;
    const std::string text(title);
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (size <= 0)
        return;
    std::wstring wide(static_cast<std::size_t>(size), 0);
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wide.data(), size);
    SetWindowTextW(impl_->hwnd, wide.c_str());
}

bool Window::setMode(WindowMode mode)
{
    return impl_->setMode(mode);
}

WindowMode Window::mode() const
{
    return impl_->windowMode;
}

bool Window::isFullscreen() const
{
    return impl_->windowMode == WindowMode::Fullscreen;
}

bool Window::toggleFullscreen()
{
    return impl_->setMode(impl_->windowMode == WindowMode::Fullscreen ? WindowMode::Windowed : WindowMode::Fullscreen);
}

bool Window::setSize(std::uint32_t width, std::uint32_t height)
{
    if (!impl_->hwnd || width == 0 || height == 0)
        return false;
    if (impl_->windowMode != WindowMode::Windowed && !impl_->setMode(WindowMode::Windowed))
        return false;
    RECT rect{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    AdjustWindowRect(&rect, Impl::kStyle, FALSE);
    return SetWindowPos(impl_->hwnd, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
                        SWP_NOMOVE | SWP_NOZORDER | SWP_NOOWNERZORDER) != 0;
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

#include <algorithm>
#include <vector>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "zio/events/EventBus.h"
#include "zio/events/Types.h"

namespace zio::platform
{

namespace
{

events::KeyCode translateKey(int key)
{
    using events::KeyCode;
    switch (key)
    {
    case GLFW_KEY_LEFT_SHIFT:
        return KeyCode::LeftShift;
    case GLFW_KEY_RIGHT_SHIFT:
        return KeyCode::RightShift;
    case GLFW_KEY_LEFT_CONTROL:
        return KeyCode::LeftCtrl;
    case GLFW_KEY_RIGHT_CONTROL:
        return KeyCode::RightCtrl;
    case GLFW_KEY_LEFT_ALT:
        return KeyCode::LeftAlt;
    case GLFW_KEY_RIGHT_ALT:
        return KeyCode::RightAlt;
    case GLFW_KEY_ESCAPE:
        return KeyCode::Escape;
    case GLFW_KEY_TAB:
        return KeyCode::Tab;
    case GLFW_KEY_ENTER:
    case GLFW_KEY_KP_ENTER:
        return KeyCode::Enter;
    case GLFW_KEY_BACKSPACE:
        return KeyCode::Backspace;
    case GLFW_KEY_SPACE:
        return KeyCode::Space;
    case GLFW_KEY_DELETE:
        return KeyCode::Delete;
    case GLFW_KEY_INSERT:
        return KeyCode::Insert;
    case GLFW_KEY_CAPS_LOCK:
        return KeyCode::CapsLock;
    case GLFW_KEY_HOME:
        return KeyCode::Home;
    case GLFW_KEY_END:
        return KeyCode::End;
    case GLFW_KEY_PAGE_UP:
        return KeyCode::PageUp;
    case GLFW_KEY_PAGE_DOWN:
        return KeyCode::PageDown;
    case GLFW_KEY_LEFT:
        return KeyCode::Left;
    case GLFW_KEY_RIGHT:
        return KeyCode::Right;
    case GLFW_KEY_UP:
        return KeyCode::Up;
    case GLFW_KEY_DOWN:
        return KeyCode::Down;
    case GLFW_KEY_MINUS:
        return KeyCode::Minus;
    case GLFW_KEY_EQUAL:
        return KeyCode::Equal;
    case GLFW_KEY_LEFT_BRACKET:
        return KeyCode::LeftBracket;
    case GLFW_KEY_RIGHT_BRACKET:
        return KeyCode::RightBracket;
    case GLFW_KEY_BACKSLASH:
        return KeyCode::BackSlash;
    case GLFW_KEY_SEMICOLON:
        return KeyCode::Semicolon;
    case GLFW_KEY_APOSTROPHE:
        return KeyCode::Apostrophe;
    case GLFW_KEY_GRAVE_ACCENT:
        return KeyCode::Grave;
    case GLFW_KEY_COMMA:
        return KeyCode::Comma;
    case GLFW_KEY_PERIOD:
        return KeyCode::Period;
    case GLFW_KEY_SLASH:
        return KeyCode::Slash;
    default:
        break;
    }
    if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z)
        return static_cast<KeyCode>(static_cast<int>(KeyCode::A) + (key - GLFW_KEY_A));
    if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9)
        return static_cast<KeyCode>(static_cast<int>(KeyCode::Num0) + (key - GLFW_KEY_0));
    if (key >= GLFW_KEY_F1 && key <= GLFW_KEY_F12)
        return static_cast<KeyCode>(static_cast<int>(KeyCode::F1) + (key - GLFW_KEY_F1));
    return KeyCode::Unknown;
}

}

struct Window::Impl
{
    static constexpr int kMinClientWidth  = 320;
    static constexpr int kMinClientHeight = 240;

    GLFWwindow* window   = nullptr;
    std::uint32_t width  = 0;
    std::uint32_t height = 0;
    std::uint32_t notifiedWidth  = 0;
    std::uint32_t notifiedHeight = 0;
    bool closeRequested = false;
    bool minimized      = false;
    float lastMouseX    = 0.f;
    float lastMouseY    = 0.f;
    bool haveLastMouse  = false;
    CursorShape cursorShape = CursorShape::Arrow;
    GLFWcursor* cursors[5]  = {};
    std::vector<events::KeyCode> heldKeys;
    bool mouseButtonHeld[5] = {};
    bool fullscreen         = false;
    int savedX = 0, savedY = 0, savedW = 0, savedH = 0;

    WindowMode currentMode() const
    {
        if (fullscreen)
            return WindowMode::Fullscreen;
        return window && glfwGetWindowAttrib(window, GLFW_MAXIMIZED) ? WindowMode::Maximized : WindowMode::Windowed;
    }

    bool setMode(WindowMode target)
    {
        if (!window)
            return false;
        if (target == WindowMode::Fullscreen)
        {
            if (fullscreen)
                return true;
            GLFWmonitor* monitor = glfwGetPrimaryMonitor();
            const GLFWvidmode* video = monitor ? glfwGetVideoMode(monitor) : nullptr;
            if (!video)
                return false;
            glfwGetWindowPos(window, &savedX, &savedY);
            glfwGetWindowSize(window, &savedW, &savedH);
            glfwSetWindowMonitor(window, monitor, 0, 0, video->width, video->height, video->refreshRate);
            fullscreen = true;
            return true;
        }
        if (fullscreen)
        {
            glfwSetWindowMonitor(window, nullptr, savedX, savedY, savedW > 0 ? savedW : 1280,
                                 savedH > 0 ? savedH : 720, 0);
            fullscreen = false;
        }
        if (target == WindowMode::Maximized)
            glfwMaximizeWindow(window);
        else
            glfwRestoreWindow(window);
        return true;
    }

    static Impl* self(GLFWwindow* window) { return static_cast<Impl*>(glfwGetWindowUserPointer(window)); }

    static events::MouseButton translateButton(int button, bool& valid)
    {
        valid = true;
        switch (button)
        {
        case GLFW_MOUSE_BUTTON_LEFT:
            return events::MouseButton::Left;
        case GLFW_MOUSE_BUTTON_RIGHT:
            return events::MouseButton::Right;
        case GLFW_MOUSE_BUTTON_MIDDLE:
            return events::MouseButton::Middle;
        case GLFW_MOUSE_BUTTON_4:
            return events::MouseButton::Extra1;
        case GLFW_MOUSE_BUTTON_5:
            return events::MouseButton::Extra2;
        default:
            valid = false;
            return events::MouseButton::Left;
        }
    }

    static void onClose(GLFWwindow* w)
    {
        self(w)->closeRequested = true;
        glfwSetWindowShouldClose(w, GLFW_FALSE);
    }

    static void onKey(GLFWwindow* w, int key, int, int action, int)
    {
        Impl* me                     = self(w);
        const events::KeyCode code = translateKey(key);
        auto& bus                    = events::EventBus::instance();
        if (action == GLFW_RELEASE)
        {
            me->heldKeys.erase(std::remove(me->heldKeys.begin(), me->heldKeys.end(), code), me->heldKeys.end());
            bus.postKey(code, false, false);
            return;
        }
        if (code != events::KeyCode::Unknown &&
            std::find(me->heldKeys.begin(), me->heldKeys.end(), code) == me->heldKeys.end())
            me->heldKeys.push_back(code);
        bus.postKey(code, true, action == GLFW_REPEAT);
    }

    static void onChar(GLFWwindow*, unsigned int codepoint)
    {
        if (codepoint >= 32 && codepoint != 127)
            events::EventBus::instance().postText(static_cast<char32_t>(codepoint));
    }

    static void onMouseButton(GLFWwindow* w, int button, int action, int)
    {
        bool valid = false;
        const events::MouseButton mapped = translateButton(button, valid);
        if (!valid)
            return;
        Impl* me        = self(w);
        const bool down = action == GLFW_PRESS;
        me->mouseButtonHeld[static_cast<std::size_t>(mapped)] = down;
        events::EventBus::instance().postMouseButton(mapped, me->lastMouseX, me->lastMouseY, down);
    }

    static void onCursor(GLFWwindow* w, double x, double y)
    {
        Impl* me       = self(w);
        const float fx = static_cast<float>(x);
        const float fy = static_cast<float>(y);
        const float dx = me->haveLastMouse ? fx - me->lastMouseX : 0.f;
        const float dy = me->haveLastMouse ? fy - me->lastMouseY : 0.f;
        me->lastMouseX   = fx;
        me->lastMouseY   = fy;
        me->haveLastMouse = true;
        events::EventBus::instance().postMouseMove(fx, fy, dx, dy);
    }

    static void onScroll(GLFWwindow* w, double xOffset, double yOffset)
    {
        Impl* me = self(w);
        events::EventBus::instance().postMouseScroll(me->lastMouseX, me->lastMouseY, static_cast<float>(xOffset),
                                                     static_cast<float>(yOffset));
    }

    static void onFramebuffer(GLFWwindow* w, int newWidth, int newHeight)
    {
        Impl* me = self(w);
        if (newWidth <= 0 || newHeight <= 0)
            return;
        me->width  = static_cast<std::uint32_t>(newWidth);
        me->height = static_cast<std::uint32_t>(newHeight);
    }

    static void onIconify(GLFWwindow* w, int iconified)
    {
        self(w)->minimized = iconified != 0;
    }

    static void onFocus(GLFWwindow* w, int focused)
    {
        if (focused)
            return;
        Impl* me  = self(w);
        auto& bus = events::EventBus::instance();
        for (events::KeyCode code : me->heldKeys)
            bus.postKey(code, false, false);
        me->heldKeys.clear();
        for (std::size_t i = 0; i < 5; ++i)
            if (me->mouseButtonHeld[i])
            {
                me->mouseButtonHeld[i] = false;
                bus.postMouseButton(static_cast<events::MouseButton>(i), me->lastMouseX, me->lastMouseY, false);
            }
    }

    bool create(std::string_view title, std::uint32_t w, std::uint32_t h)
    {
        static bool initialized = false;
        if (!initialized)
        {
            if (!glfwInit())
                return false;
            initialized = true;
        }
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        window = glfwCreateWindow(static_cast<int>(w), static_cast<int>(h), std::string(title).c_str(), nullptr, nullptr);
        if (!window)
            return false;

        glfwSetWindowUserPointer(window, this);
        glfwSetWindowSizeLimits(window, kMinClientWidth, kMinClientHeight, GLFW_DONT_CARE, GLFW_DONT_CARE);
        glfwSetWindowCloseCallback(window, &Impl::onClose);
        glfwSetKeyCallback(window, &Impl::onKey);
        glfwSetCharCallback(window, &Impl::onChar);
        glfwSetMouseButtonCallback(window, &Impl::onMouseButton);
        glfwSetCursorPosCallback(window, &Impl::onCursor);
        glfwSetScrollCallback(window, &Impl::onScroll);
        glfwSetFramebufferSizeCallback(window, &Impl::onFramebuffer);
        glfwSetWindowIconifyCallback(window, &Impl::onIconify);
        glfwSetWindowFocusCallback(window, &Impl::onFocus);

        cursors[static_cast<int>(CursorShape::Arrow)]            = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
        cursors[static_cast<int>(CursorShape::IBeam)]            = glfwCreateStandardCursor(GLFW_IBEAM_CURSOR);
        cursors[static_cast<int>(CursorShape::ResizeHorizontal)] = glfwCreateStandardCursor(GLFW_HRESIZE_CURSOR);
        cursors[static_cast<int>(CursorShape::ResizeVertical)]   = glfwCreateStandardCursor(GLFW_VRESIZE_CURSOR);
        cursors[static_cast<int>(CursorShape::Hand)]             = glfwCreateStandardCursor(GLFW_HAND_CURSOR);

        int fbWidth = 0, fbHeight = 0;
        glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
        width          = fbWidth > 0 ? static_cast<std::uint32_t>(fbWidth) : w;
        height         = fbHeight > 0 ? static_cast<std::uint32_t>(fbHeight) : h;
        notifiedWidth  = width;
        notifiedHeight = height;
        minimized      = false;
        closeRequested = false;
        haveLastMouse  = false;
        heldKeys.clear();
        for (bool& held : mouseButtonHeld)
            held = false;
        return true;
    }

    void destroy()
    {
        if (window)
        {
            for (GLFWcursor*& cursor : cursors)
                if (cursor)
                {
                    glfwDestroyCursor(cursor);
                    cursor = nullptr;
                }
            glfwDestroyWindow(window);
            window     = nullptr;
            fullscreen = false;
        }
        width = height = notifiedWidth = notifiedHeight = 0;
        minimized                                      = false;
        heldKeys.clear();
        for (bool& held : mouseButtonHeld)
            held = false;
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
    return impl_->window != nullptr;
}

void Window::pumpMessages()
{
    if (impl_->window)
        glfwPollEvents();
}

void Window::cancelClose()
{
    impl_->closeRequested = false;
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

void Window::setCursor(CursorShape shape)
{
    if (impl_->cursorShape == shape)
        return;
    impl_->cursorShape = shape;
    if (impl_->window)
        glfwSetCursor(impl_->window, impl_->cursors[static_cast<int>(shape)]);
}

void Window::setTitle(std::string_view title)
{
    if (impl_->window)
        glfwSetWindowTitle(impl_->window, std::string(title).c_str());
}

bool Window::setMode(WindowMode mode)
{
    return impl_->setMode(mode);
}

WindowMode Window::mode() const
{
    return impl_->currentMode();
}

bool Window::isFullscreen() const
{
    return impl_->fullscreen;
}

bool Window::toggleFullscreen()
{
    return impl_->setMode(impl_->fullscreen ? WindowMode::Windowed : WindowMode::Fullscreen);
}

bool Window::setSize(std::uint32_t width, std::uint32_t height)
{
    if (!impl_->window || width == 0 || height == 0)
        return false;
    if (impl_->currentMode() != WindowMode::Windowed && !impl_->setMode(WindowMode::Windowed))
        return false;
    glfwSetWindowSize(impl_->window, static_cast<int>(width), static_cast<int>(height));
    return true;
}

void* Window::nativeHandle() const
{
    return impl_->window;
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

#endif
