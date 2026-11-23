#pragma once

#include <memory>

#include "zio/core/Platform.h"
#include "zio/ui/Painter.h"

namespace zio::ui
{

class ZIO_API BatchPainter final : public Painter
{
public:
    BatchPainter();
    ~BatchPainter() override;

    BatchPainter(const BatchPainter&)            = delete;
    BatchPainter& operator=(const BatchPainter&) = delete;

    bool begin(float viewportWidth, float viewportHeight);
    void setScale(float scale);

    void pushClip(const Rect& rect) override;
    void popClip() override;
    void fillRect(const Rect& rect, Color color) override;
    void fillRoundRect(const Rect& rect, Color color, float radius) override;
    void line(Vec2 from, Vec2 to, Color color, float thickness) override;
    void triangle(Vec2 a, Vec2 b, Vec2 c, Color color) override;
    void image(std::uint32_t texture, const Rect& rect, Color tint) override;
    void text(Font& font, Vec2 topLeft, std::string_view utf8, Color color) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
