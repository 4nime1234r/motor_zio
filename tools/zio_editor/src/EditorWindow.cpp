#include "EditorWindow.h"

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

#include <string>

#include "imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace zio_editor
{

struct EditorWindow::Impl
{
    HWND hwnd            = nullptr;
    std::uint32_t width  = 0;
    std::uint32_t height = 0;
    bool closeRequested  = false;
    bool resized         = false;

    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
            return true;

        Impl* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (!self)
            return DefWindowProcW(hwnd, msg, wParam, lParam);

        switch (msg)
        {
        case WM_CLOSE:
            self->closeRequested = true;
            return 0;

        case WM_SIZE:
            if (wParam != SIZE_MINIMIZED)
            {
                const auto w = static_cast<std::uint32_t>(LOWORD(lParam));
                const auto h = static_cast<std::uint32_t>(HIWORD(lParam));
                if (w > 0 && h > 0 && (w != self->width || h != self->height))
                {
                    self->width   = w;
                    self->height  = h;
                    self->resized = true;
                }
            }
            return 0;

        default:
            break;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
};

EditorWindow::EditorWindow() : impl_(std::make_unique<Impl>()) {}
EditorWindow::~EditorWindow()
{
    destroy();
}

bool EditorWindow::create(std::string_view title, std::uint32_t width, std::uint32_t height)
{
    destroy();

    static const wchar_t* kClassName = L"ZioEditorWindow";
    static bool classRegistered      = false;
    if (!classRegistered)
    {
        WNDCLASSEXW wc{};
        wc.cbSize        = sizeof(wc);
        wc.style         = CS_OWNDC;
        wc.lpfnWndProc   = &Impl::wndProc;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kClassName;
        if (!RegisterClassExW(&wc))
            return false;
        classRegistered = true;
    }

    constexpr DWORD kStyle = WS_OVERLAPPEDWINDOW;

    RECT rect{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    AdjustWindowRect(&rect, kStyle, FALSE);

    std::wstring wideTitle(title.begin(), title.end());
    impl_->hwnd = CreateWindowExW(0, kClassName, wideTitle.c_str(), kStyle, CW_USEDEFAULT, CW_USEDEFAULT,
                                   rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr,
                                   GetModuleHandleW(nullptr), nullptr);
    if (!impl_->hwnd)
        return false;

    SetWindowLongPtrW(impl_->hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(impl_.get()));
    ShowWindow(impl_->hwnd, SW_SHOW);
    UpdateWindow(impl_->hwnd);

    impl_->width          = width;
    impl_->height         = height;
    impl_->closeRequested = false;
    impl_->resized        = false;
    return true;
}

void EditorWindow::destroy()
{
    if (impl_->hwnd)
    {
        DestroyWindow(impl_->hwnd);
        impl_->hwnd = nullptr;
    }
}

void EditorWindow::pumpMessages()
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

bool EditorWindow::closeRequested() const
{
    return impl_->closeRequested;
}

void EditorWindow::cancelClose()
{
    impl_->closeRequested = false;
}

void* EditorWindow::nativeHandle() const
{
    return impl_->hwnd;
}

std::uint32_t EditorWindow::width() const
{
    return impl_->width;
}

std::uint32_t EditorWindow::height() const
{
    return impl_->height;
}

bool EditorWindow::resized() const
{
    return impl_->resized;
}

void EditorWindow::consumeResized()
{
    impl_->resized = false;
}

}
