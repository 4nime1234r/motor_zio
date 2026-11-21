#pragma once

#include <string>
#include <string_view>

#include "zio/core/Platform.h"
#include "zio/render/Types.h"
#include "zio/renderer/Color.h"
#include "zio/scene2d/Node.h"

namespace zio::scene2d
{

class ZIO_API Label final : public Node
{
public:
    Label();
    explicit Label(std::string text, std::string fontFamily = "Segoe UI", float fontSizePx = 24.f);
    ~Label() override;

    Label(const Label&)            = delete;
    Label& operator=(const Label&) = delete;

    void setText(std::string text);
    const std::string& text() const { return text_; }

    void setFont(std::string fontFamily, float fontSizePx);
    const std::string& fontFamily() const { return fontFamily_; }
    float fontSizePx() const { return fontSizePx_; }

    void setColor(renderer::Color color);
    renderer::Color color() const { return color_; }

protected:
    void onDraw() override;

private:
    void rebuildTexture();

    std::string text_;
    std::string fontFamily_ = "Segoe UI";
    float fontSizePx_       = 24.f;
    renderer::Color color_  = renderer::Color::white();

    render::TextureHandle texture_ = render::kInvalidHandle;
};

}
