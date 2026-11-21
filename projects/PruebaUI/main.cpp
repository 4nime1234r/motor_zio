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

    std::vector<ui::MenuItem> objectMenu()
    {
        using ui::MenuItem;
        return {MenuItem::submenuItem("Añadir", {MenuItem::item("Nodo vacío"), MenuItem::item("Cubo"),
                                                  MenuItem::item("Malla (.obj)")}),
                MenuItem::line(), MenuItem::item("Duplicar", {}, "Ctrl+D"), MenuItem::item("Eliminar", {}, "Supr")};
    }

    void build()
    {
        using ui::MenuItem;
        const ui::Theme& theme = context_->theme();
        auto* page             = context_->root().add<ui::VBox>();
        page->setSeparation(0.f);

        auto* top = makePanel(page, theme.deep);
        top->setPadding(ui::Edges::symmetric(6.f, 0.f));
        auto* topRow = top->add<ui::HBox>();
        auto* bar    = topRow->add<ui::MenuBar>();
        bar->addMenu("Proyecto", std::vector<MenuItem>{MenuItem::item("Nuevo proyecto..."), MenuItem::item("Abrir proyecto...")});
        bar->addMenu("Escena", std::vector<MenuItem>{MenuItem::item("Guardar", {}, "Ctrl+S"), MenuItem::item("Guardar como...")});
        bar->addMenu("Editar", std::vector<MenuItem>{MenuItem::item("Copiar", {}, "Ctrl+C"), MenuItem::item("Pegar", {}, "Ctrl+V"),
                                                      MenuItem::line(), MenuItem::item("Deshacer", {}, "Ctrl+Z")});
        bar->addMenu("Objeto", [this]() { return objectMenu(); });
        bar->addMenu("Ver", [this]() {
            return std::vector<MenuItem>{MenuItem::check("Interfaz fija", fixed_, [this]() { fixed_ = !fixed_; }),
                                         MenuItem::line(), MenuItem::item("Restablecer disposición")};
        });
        auto* spacer = topRow->add<ui::Widget>();
        spacer->setExpand(true, false);
        auto* play = topRow->add<ui::Button>(std::string("Jugar"));
        play->setToggle(true);
        play->setTooltip("Ejecuta la escena (F5)");
        auto* stop = topRow->add<ui::Button>(std::string("Detener"));
        stop->setTooltip("Detiene la escena (F5)");

        auto* body = page->add<ui::SplitContainer>(false);
        body->setExpand(true, true);
        body->setRatio(0.22f);

        auto* left = body->add<ui::SplitContainer>(true);
        left->setRatio(0.55f);

        auto* hierarchy       = makePanel(left, theme.panel);
        auto* hierarchyColumn = hierarchy->add<ui::VBox>();
        hierarchyColumn->add<ui::Label>(std::string("Jerarquía"))->setColor(theme.textDim);
        auto* scroll = hierarchyColumn->add<ui::ScrollContainer>(true, false);
        scroll->setExpand(true, true);
        auto* tree = scroll->add<ui::TreeView>();
        auto* scene = tree->root()->add("Escena");
        scene->iconColor = ui::rgb(0x818cf8);
        scene->add("Cámara")->iconColor = ui::rgb(0x4dd0e1);
        scene->add("Luz")->iconColor    = ui::rgb(0xfacc15);
        scene->add("Suelo")->iconColor  = ui::rgb(0x66d98c);
        auto* group = scene->add("Grupo");
        group->iconColor = ui::rgb(0x9ca3af);
        group->add("Cubo")->iconColor   = ui::rgb(0x66d98c);
        group->add("Esfera")->iconColor = ui::rgb(0x66d98c);
        tree->setOnContextMenu([this](ui::TreeItem*, ui::Vec2 position) {
            auto popup = std::make_unique<ui::PopupMenu>(objectMenu());
            popup->setAnchor(position);
            context_->openPopup(std::move(popup));
        });

        auto* files       = left->add<ui::TabContainer>();
        auto* filesPage   = files->addPage(std::make_unique<ui::Panel>(), "Archivos");
        auto* filesColumn = filesPage->add<ui::VBox>();
        filesColumn->setPadding(ui::Edges::all(8.f));
        filesColumn->add<ui::LineEdit>()->setPlaceholder("Buscar archivos...");
        auto* filesScroll = filesColumn->add<ui::ScrollContainer>(true, false);
        filesScroll->setExpand(true, true);
        auto* filesTree = filesScroll->add<ui::TreeView>();
        auto* assets    = filesTree->root()->add("assets");
        assets->iconColor = ui::rgb(0xfacc15);
        auto* scenes = assets->add("escenas");
        scenes->iconColor = ui::rgb(0xfacc15);
        scenes->add("nivel1.zscene3d")->iconColor = ui::rgb(0x818cf8);
        scenes->add("menu.zscene3d")->iconColor   = ui::rgb(0x818cf8);
        auto* scripts = assets->add("scripts");
        scripts->iconColor = ui::rgb(0xfacc15);
        scripts->add("Jugador.cs")->iconColor  = ui::rgb(0xc084fc);
        scripts->add("Enemigo.cpp")->iconColor = ui::rgb(0xc084fc);
        assets->add("modelos")->iconColor   = ui::rgb(0xfacc15);
        assets->add("texturas")->iconColor  = ui::rgb(0xfacc15);
        files->addPage(std::make_unique<ui::Panel>(), "Rendimiento")->add<ui::Label>(std::string("60 FPS"));

        auto* middle = body->add<ui::SplitContainer>(false);
        middle->setRatio(0.74f);

        auto* viewport      = makePanel(middle, ui::rgb(0x0f1117));
        auto* viewportLabel = viewport->add<ui::Label>(std::string("Aquí iría el visor 3D"));
        viewportLabel->setAlign(ui::Align::Center);
        viewportLabel->setColor(theme.textDim);

        auto* inspector       = makePanel(middle, theme.panel);
        auto* inspectorColumn = inspector->add<ui::VBox>();
        inspectorColumn->add<ui::Label>(std::string("Inspector"))->setColor(theme.textDim);
        inspectorColumn->add<ui::Separator>();
        inspectorColumn->add<ui::Label>(std::string("Nombre"));
        inspectorColumn->add<ui::LineEdit>()->setText("Cubo");
        inspectorColumn->add<ui::Label>(std::string("Posición"));
        auto* position = inspectorColumn->add<ui::HBox>();
        for (const char* value : {"0.0", "1.5", "-2.0"})
            position->add<ui::LineEdit>()->setText(value);
        inspectorColumn->add<ui::Label>(std::string("Calidad de sombras"));
        auto* quality = inspectorColumn->add<ui::OptionButton>();
        quality->setItems({"Baja", "Media", "Alta"});
        quality->setSelected(1);
        auto* shadow = inspectorColumn->add<ui::CheckBox>(std::string("Proyecta sombra"));
        shadow->setChecked(true);
        shadow->setTooltip("El objeto proyecta sombra sobre los demás");
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
    bool fixed_                = true;
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
