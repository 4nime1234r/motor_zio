#include <cmath>

#include "zio/ui/Font.h"

namespace zio::ui
{

namespace
{

class ScaledFont final : public AtlasFont
{
public:
    ScaledFont(std::shared_ptr<AtlasFont> inner, float scale) : inner_(std::move(inner)), scale_(scale) {}

    float size() const override { return inner_->size() / scale_; }
    float lineHeight() const override { return inner_->lineHeight() / scale_; }
    float ascent() const override { return inner_->ascent() / scale_; }
    float advance(char32_t codepoint) override { return inner_->advance(codepoint) / scale_; }

    bool glyph(char32_t codepoint, Glyph& out) override
    {
        if (!inner_->glyph(codepoint, out))
            return false;
        out.width /= scale_;
        out.height /= scale_;
        out.offsetX /= scale_;
        out.offsetY /= scale_;
        out.advance /= scale_;
        return true;
    }

    render::TextureHandle atlasTexture() override { return inner_->atlasTexture(); }
    void uploadIfDirty() override { inner_->uploadIfDirty(); }

private:
    std::shared_ptr<AtlasFont> inner_;
    float scale_;
};

}

std::shared_ptr<AtlasFont> createScaledSystemFont(std::string_view family, float sizePx, bool bold, float scale)
{
    if (!(scale > 1.01f))
        return createSystemFont(family, sizePx, bold);
    auto inner = createSystemFont(family, sizePx * scale, bold);
    if (!inner)
        return nullptr;
    return std::make_shared<ScaledFont>(std::move(inner), scale);
}

}
