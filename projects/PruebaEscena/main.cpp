#include <string>

#include "zio/zio.h"

using namespace zio;

namespace
{

class Escena final : public scene2d::Scene2D
{
};

}

class Prueba final : public core::Application
{
protected:
    bool onInit() override
    {
        auto* escena = scene::SceneStack::instance().run<Escena>();
        auto& raiz   = escena->root();

        auto* grupo = raiz.addChild<scene2d::Node>();
        grupo->setName("grupo");
        grupo->setPosition({200.f, 200.f});

        auto* a = grupo->addChild<scene2d::Label>(std::string("Hijo A"), std::string("Segoe UI"), 32.f);
        a->setName("hijoA");
        a->setColor({1.f, 0.8f, 0.2f, 1.f});

        auto* b = grupo->addChild<scene2d::Label>(std::string("Hijo B (rotado)"), std::string("Segoe UI"), 24.f);
        b->setName("hijoB");
        b->setPosition({0.f, 80.f});
        b->setRotation(10.f);

        const std::string antes = scene2d::serializeScene(raiz);
        std::string err;
        if (!scene2d::saveSceneFile(raiz, "escena.json", &err))
        {
            estado_ = "ERROR al guardar: " + err;
            return true;
        }

        raiz.removeAllChildren();
        if (!scene2d::loadSceneFile(raiz, "escena.json", &err))
        {
            estado_ = "ERROR al cargar: " + err;
            return true;
        }

        const bool igual = (scene2d::serializeScene(raiz) == antes);
        estado_ = igual ? "Escena cargada desde escena.json: OK (identica a la guardada)"
                        : "Escena cargada pero DIFERENTE a la guardada";

        auto* t = raiz.addChild<scene2d::Label>(estado_, std::string("Segoe UI"), 22.f);
        t->setName("estado");
        t->setAnchorPoint({0.f, 0.f});
        return true;
    }

    void onRender(float) override
    {
        auto& device = render::Device::instance();
        device.beginFrame(render::ClearColor{0.1f, 0.1f, 0.15f, 1.f});

        renderer::BatchRenderer::instance().begin(config().windowWidth, config().windowHeight);
        if (auto* s = dynamic_cast<scene2d::Scene2D*>(scene::SceneStack::instance().current()))
            s->draw();
        renderer::BatchRenderer::instance().end();

        device.endFrame();
    }

    void onAfterTick(float) override
    {
        if (input::InputManager::instance().isKeyPressed(events::KeyCode::Escape))
            requestExit();
    }

private:
    std::string estado_;
};

int main()
{
    core::AppConfig config;
    config.title        = "Prueba de escena";
    config.windowWidth  = 800;
    config.windowHeight = 600;
    config.targetFps    = 60;

    Prueba app;
    app.run(config);
    return 0;
}
