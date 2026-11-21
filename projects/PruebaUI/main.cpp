#include <cmath>
#include <memory>
#include <string>

#include "zio/zio.h"

using namespace zio;

namespace
{

class Demo final : public core::Application
{
protected:
    bool onInit() override
    {
        context_ = std::make_unique<ui::Context>();
        if (auto font = ui::createSystemFont("Segoe UI", 17.f))
            context_->theme().font = font;
        host_ = std::make_unique<ui::EngineHost>(*context_);
        build();
        return true;
    }

    void onBeforeTick(float dt) override
    {
        time_ += dt;
        progress_->setValue(0.5f + 0.5f * std::sin(time_));
        host_->frame(dt);
    }

    void onRender(float) override
    {
        auto& device = render::Device::instance();
        device.beginFrame(render::ClearColor{0.059f, 0.067f, 0.090f, 1.f});

        renderer::BatchRenderer::instance().begin(config().windowWidth, config().windowHeight);
        host_->draw();
        renderer::BatchRenderer::instance().end();

        device.endFrame();
    }

    void onShutdown() override
    {
        host_.reset();
        context_.reset();
    }

private:
    ui::Panel* makePanel(ui::Widget* parent, ui::Color color)
    {
        auto* panel = parent->add<ui::Panel>();
        panel->setBackground(color);
        panel->setPadding(ui::Edges::all(10.f));
        return panel;
    }

    void build()
    {
        const ui::Theme& theme = context_->theme();
        auto* page             = context_->root().add<ui::VBox>();
        page->setSeparation(0.f);

        auto* top = makePanel(page, theme.deep);
        top->setPadding(ui::Edges::symmetric(10.f, 4.f));
        auto* topRow = top->add<ui::HBox>();
        for (const char* name : {"Proyecto", "Escena", "Editar", "Objeto", "Ver"})
        {
            auto* button = topRow->add<ui::Button>(std::string(name));
            button->setFlat(true);
        }
        auto* spacer = topRow->add<ui::Widget>();
        spacer->setExpand(true, false);
        auto* play = topRow->add<ui::Button>(std::string("Jugar"));
        play->setToggle(true);
        auto* stop = topRow->add<ui::Button>(std::string("Detener"));
        (void)stop;

        auto* body = page->add<ui::SplitContainer>(false);
        body->setExpand(true, true);
        body->setRatio(0.22f);

        auto* left = body->add<ui::SplitContainer>(true);
        left->setRatio(0.55f);

        auto* hierarchy = makePanel(left, theme.panel);
        auto* hierarchyColumn = hierarchy->add<ui::VBox>();
        hierarchyColumn->add<ui::Label>(std::string("Jerarquía"))->setColor(theme.textDim);
        auto* scroll = hierarchyColumn->add<ui::ScrollContainer>(true, false);
        scroll->setExpand(true, true);
        auto* nodes = scroll->add<ui::VBox>();
        nodes->setSeparation(2.f);
        for (int i = 0; i < 30; ++i)
        {
            auto* row = nodes->add<ui::Button>("Nodo " + std::to_string(i + 1));
            row->setFlat(true);
        }

        auto* files = left->add<ui::TabContainer>();
        auto* filesPage = files->addPage(std::make_unique<ui::Panel>(), "Archivos");
        auto* filesColumn = filesPage->add<ui::VBox>();
        filesColumn->add<ui::LineEdit>()->setPlaceholder("Buscar archivos...");
        for (const char* name : {"escenas", "modelos", "texturas", "audio", "scripts"})
            filesColumn->add<ui::Label>(std::string(name));
        auto* perfPage = files->addPage(std::make_unique<ui::Panel>(), "Rendimiento");
        perfPage->add<ui::Label>(std::string("60 FPS"));

        auto* middle = body->add<ui::SplitContainer>(false);
        middle->setRatio(0.74f);

        auto* viewport = makePanel(middle, ui::rgb(0x0f1117));
        auto* viewportLabel = viewport->add<ui::Label>(std::string("Aquí iría el visor 3D"));
        viewportLabel->setAlign(ui::Align::Center);
        viewportLabel->setColor(theme.textDim);

        auto* inspector = makePanel(middle, theme.panel);
        auto* inspectorColumn = inspector->add<ui::VBox>();
        inspectorColumn->add<ui::Label>(std::string("Inspector"))->setColor(theme.textDim);
        inspectorColumn->add<ui::Separator>();
        inspectorColumn->add<ui::Label>(std::string("Nombre"));
        auto* name = inspectorColumn->add<ui::LineEdit>();
        name->setText("Cubo");
        inspectorColumn->add<ui::Label>(std::string("Posición"));
        auto* position = inspectorColumn->add<ui::HBox>();
        for (const char* value : {"0.0", "1.5", "-2.0"})
            position->add<ui::LineEdit>()->setText(value);
        inspectorColumn->add<ui::CheckBox>(std::string("Proyecta sombra"))->setChecked(true);
        inspectorColumn->add<ui::CheckBox>(std::string("Visible"))->setChecked(true);
        progress_ = inspectorColumn->add<ui::ProgressBar>();

        auto* status = makePanel(page, theme.deep);
        status->setPadding(ui::Edges::symmetric(12.f, 3.f));
        status->add<ui::Label>(std::string("ProyectoEjemplo  /  escena.zscene3d"))->setColor(theme.textDim);
    }

    std::unique_ptr<ui::Context> context_;
    std::unique_ptr<ui::EngineHost> host_;
    ui::ProgressBar* progress_ = nullptr;
    float time_                = 0.f;
};

}

int main()
{
    core::AppConfig config;
    config.title        = "Prueba UI";
    config.windowWidth  = 1280;
    config.windowHeight = 720;
    config.targetFps    = 60;

    Demo app;
    app.run(config);
    return 0;
}
