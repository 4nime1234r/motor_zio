#include <string>

#include "zio/zio.h"

using namespace zio;

namespace
{

class MainScene final : public scene2d::Scene2D
{
public:
    void onEnter() override
    {
        label_ = root().addChild<scene2d::Label>(std::string("Hola, puto"), std::string("Segoe UI"),
                                                    24.f);
        label_->setPosition({400.f, 300.f});
    }

    void update(float dt) override
    {
        Scene2D::update(dt);

        auto& input           = input::InputManager::instance();
        constexpr float speed  = 200.f;
        math::Vec2 delta       = math::Vec2::zero();
        if (input.isKeyDown(events::KeyCode::Left))
            delta.x -= speed * dt;
        if (input.isKeyDown(events::KeyCode::Right))
            delta.x += speed * dt;
        if (input.isKeyDown(events::KeyCode::Up))
            delta.y -= speed * dt;
        if (input.isKeyDown(events::KeyCode::Down))
            delta.y += speed * dt;

        label_->setPosition(label_->position() + delta);
    }

private:
    scene2d::Label* label_ = nullptr;
};

}

class GameApp final : public core::Application
{
protected:
    bool onInit() override
    {
        scene::SceneStack::instance().run<MainScene>();
        return true;
    }

    void onRender(float dt) override
    {
        auto& device = render::Device::instance();
        device.beginFrame(render::ClearColor{0.1f, 0.1f, 0.15f, 1.f});

        renderer::BatchRenderer::instance().begin(config().windowWidth, config().windowHeight);
        if (auto* scene = dynamic_cast<scene2d::Scene2D*>(scene::SceneStack::instance().current()))
            scene->draw();
        renderer::BatchRenderer::instance().end();

        device.endFrame();
    }

    void onAfterTick(float dt) override
    {
        if (input::InputManager::instance().isKeyPressed(events::KeyCode::Escape))
            requestExit();
    }
};

int main()
{
    core::AppConfig config;
    config.title        = "CLI";
    config.windowWidth   = 800;
    config.windowHeight  = 600;
    config.targetFps     = 60;

    GameApp game;
    game.run(config);
    return 0;
}
