#include <string>

#include "zio/zio.h"

using namespace zio;
using scene3d::MeshNode;
using scene3d::Node3D;

namespace
{

Node3D* findByName(Node3D& node, const std::string& name)
{
    if (node.name() == name)
        return &node;
    for (auto& child : node.children())
        if (Node3D* found = findByName(*child, name))
            return found;
    return nullptr;
}

class Escena final : public scene3d::Scene3D
{
public:
    void update(float dt) override
    {
        Scene3D::update(dt);
        angulo_ += 60.f * dt;
        if (Node3D* grupo = findByName(root(), "grupo"))
            grupo->setRotation(math::Quaternion::fromEulerDegrees({0.f, angulo_, 0.f}));
        if (Node3D* cubo = findByName(root(), "girando"))
            cubo->setRotation(math::Quaternion::fromEulerDegrees({angulo_ * 2.f, angulo_ * 2.f, 0.f}));
    }

private:
    float angulo_ = 0.f;
};

}

class Prueba final : public core::Application
{
protected:
    bool onInit() override
    {
        auto* escena = scene::SceneStack::instance().run<Escena>();

        escena->camera().position = {5.f, 4.f, 7.f};
        escena->camera().target   = {0.f, 0.5f, 0.f};

        escena->light().direction     = {-0.5f, -1.f, -0.4f};
        escena->light().shadows       = true;
        escena->light().shadowOpacity = 0.5f;

        auto& raiz = escena->root();

        auto* suelo = raiz.addChild<MeshNode>(std::string("cube:1"));
        suelo->setName("suelo");
        suelo->setPosition({0.f, -0.55f, 0.f});
        suelo->setScale({12.f, 0.1f, 12.f});
        suelo->setTexturePath("checker.png");
        suelo->setUvScale({6.f, 6.f});
        suelo->setCastShadow(false);

        auto* cerca = raiz.addChild<MeshNode>(std::string("cube:1.5"));
        cerca->setName("cerca");
        cerca->setPosition({1.2f, 0.25f, 1.5f});
        cerca->setColor({0.9f, 0.25f, 0.2f, 1.f});

        auto* lejos = raiz.addChild<MeshNode>(std::string("cube:1.5"));
        lejos->setName("lejos");
        lejos->setPosition({0.4f, 0.25f, -0.6f});
        lejos->setRotation(math::Quaternion::fromEulerDegrees({0.f, 30.f, 0.f}));
        lejos->setColor({0.2f, 0.45f, 0.95f, 1.f});

        auto* grupo = raiz.addChild<Node3D>();
        grupo->setName("grupo");
        grupo->setPosition({-2.2f, 0.3f, 0.f});
        auto* centro = grupo->addChild<MeshNode>(std::string("cube:0.8"));
        centro->setName("centro");
        centro->setColor({0.95f, 0.8f, 0.2f, 1.f});
        auto* girando = grupo->addChild<MeshNode>(std::string("cube:0.5"));
        girando->setName("girando");
        girando->setPosition({1.6f, 0.f, 0.f});
        girando->setColor({0.3f, 0.85f, 0.4f, 1.f});

        auto* vidrioCerca = raiz.addChild<MeshNode>(std::string("cube:1.1"));
        vidrioCerca->setName("vidrioCerca");
        vidrioCerca->setPosition({2.6f, 0.05f, 2.4f});
        vidrioCerca->setColor({0.95f, 0.3f, 0.7f, 0.45f});
        auto* vidrioLejos = raiz.addChild<MeshNode>(std::string("cube:1.1"));
        vidrioLejos->setName("vidrioLejos");
        vidrioLejos->setPosition({2.2f, 0.05f, 1.6f});
        vidrioLejos->setColor({0.2f, 0.9f, 0.9f, 0.45f});

        const std::string antes = scene3d::serializeScene3D(*escena);
        std::string err;
        std::string estado;
        if (!scene3d::saveScene3DFile(*escena, "escena3d.json", &err))
            estado = "ERROR al guardar: " + err;
        else
        {
            raiz.removeAllChildren();
            if (!scene3d::loadScene3DFile(*escena, "escena3d.json", &err))
                estado = "ERROR al cargar: " + err;
            else if (scene3d::serializeScene3D(*escena) == antes)
                estado = "Escena 3D cargada desde escena3d.json: OK (identica a la guardada)";
            else
                estado = "Escena 3D cargada pero DIFERENTE a la guardada";
        }

        auto* t = hud_.root().addChild<scene2d::Label>(estado, std::string("Segoe UI"), 22.f);
        t->setAnchorPoint({0.f, 0.f});
        t->setPosition({20.f, 16.f});
        return true;
    }

    void onRender(float) override
    {
        auto& device = render::Device::instance();
        device.beginFrame(render::ClearColor{0.55f, 0.7f, 0.9f, 1.f});

        if (auto* s = dynamic_cast<scene3d::Scene3D*>(scene::SceneStack::instance().current()))
            s->draw(config().windowWidth, config().windowHeight);

        renderer::BatchRenderer::instance().begin(config().windowWidth, config().windowHeight);
        hud_.draw();
        renderer::BatchRenderer::instance().end();

        device.endFrame();
    }

    void onAfterTick(float dt) override
    {
        hud_.update(dt);
        if (input::InputManager::instance().isKeyPressed(events::KeyCode::Escape))
            requestExit();
    }

private:
    scene2d::Scene2D hud_;
};

int main()
{
    core::AppConfig config;
    config.title        = "Prueba 3D";
    config.windowWidth  = 960;
    config.windowHeight = 600;
    config.targetFps    = 60;

    Prueba app;
    app.run(config);
    return 0;
}
