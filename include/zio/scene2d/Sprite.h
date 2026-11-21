#pragma once

#include <memory>
#include <string>

#include "zio/core/Platform.h"
#include "zio/render/Types.h"
#include "zio/renderer/Color.h"
#include "zio/renderer/Texture.h"
#include "zio/scene2d/Node.h"

namespace zio::scene2d
{

class ZIO_API Sprite final : public Node
{
public:
    Sprite();
    explicit Sprite(std::string_view path);

    void setTexture(std::shared_ptr<renderer::Texture> texture);
    const std::shared_ptr<renderer::Texture>& texture() const { return texture_; }

    void setTexturePath(std::string path);
    const std::string& texturePath() const { return texturePath_; }

    void setColor(renderer::Color color) { color_ = color; }
    renderer::Color color() const { return color_; }

    void setPipeline(render::PipelineHandle pipeline) { pipeline_ = pipeline; }
    render::PipelineHandle pipeline() const { return pipeline_; }

protected:
    void onDraw() override;

private:
    std::shared_ptr<renderer::Texture> texture_;
    std::string texturePath_;
    renderer::Color color_          = renderer::Color::white();
    render::PipelineHandle pipeline_ = render::kInvalidHandle;
};

}
