#pragma once

#include <memory>
#include <string>
#include <vector>

#include "zio/core/Platform.h"
#include "zio/render/Types.h"
#include "zio/renderer/Color.h"
#include "zio/renderer/Texture.h"
#include "zio/scene2d/Node.h"

namespace zio::scene2d
{

struct SpriteAnimation
{
    std::string name;
    int firstFrame = 0;
    int lastFrame  = 0;
    float fps      = 10.f;
    bool loop      = true;
};

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

    void setSheet(int columns, int rows);
    int sheetColumns() const { return columns_; }
    int sheetRows() const { return rows_; }
    int frameCount() const { return columns_ * rows_; }

    void setFrame(int frame);
    int frame() const { return frame_; }

    void addAnimation(SpriteAnimation animation);
    void clearAnimations();
    const std::vector<SpriteAnimation>& animations() const { return animations_; }

    bool play(const std::string& name, bool restart = false);
    void stop();
    bool isPlaying() const { return playing_; }
    const std::string& currentAnimation() const { return current_; }
    void setAnimationSpeed(float speed) { speed_ = speed; }
    float animationSpeed() const { return speed_; }

    void update(float dt) override;

protected:
    void onDraw() override;

private:
    void applyFrameSize();

    int columns_ = 1;
    int rows_    = 1;
    int frame_   = 0;
    std::vector<SpriteAnimation> animations_;
    std::string current_;
    bool playing_      = false;
    float clock_       = 0.f;
    float speed_       = 1.f;
    std::shared_ptr<renderer::Texture> texture_;
    std::string texturePath_;
    renderer::Color color_          = renderer::Color::white();
    render::PipelineHandle pipeline_ = render::kInvalidHandle;
};

}
