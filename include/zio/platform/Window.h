#pragma once

#include <cstdint>
#include <memory>
#include <string_view>

#include "zio/core/Platform.h"

namespace zio::platform
{

enum class CursorShape
{
    Arrow,
    IBeam,
    ResizeHorizontal,
    ResizeVertical,
    Hand
};

class ZIO_API Window
{
public:
    Window();
    ~Window();

    Window(const Window&)            = delete;
    Window& operator=(const Window&) = delete;

    static Window& instance();

    bool create(std::string_view title, std::uint32_t width, std::uint32_t height);
    void destroy();
    bool isOpen() const;

    void pumpMessages();

    bool closeRequested() const;
    void cancelClose();

    bool isMinimized() const;

    bool consumeResize(std::uint32_t& outWidth, std::uint32_t& outHeight);

    void setCursor(CursorShape shape);

    void* nativeHandle() const;

    std::uint32_t width() const;
    std::uint32_t height() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
