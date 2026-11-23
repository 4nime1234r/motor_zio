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
        if (auto font = ui::createScaledSystemFont("Segoe UI", 17.f, false, platform::Window::instance().dpiScale()))
            context_->theme().font = font;
        if (auto bold = ui::createScaledSystemFont("Segoe UI", 17.f, true, platform::Window::instance().dpiScale()))
            context_->theme().fontBold = bold;
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
    using Item = ui::MenuItem;

    ui::Panel* makePanel(ui::Widget* parent, ui::Color color)
    {
        auto* panel = parent->add<ui::Panel>();
        panel->setBackground(color);
        panel->setPadding(ui::Edges::all(10.f));
        return panel;
    }

    std::vector<Item> sideMenu(const std::string& id)
    {
        std::vector<Item> items;
        const std::pair<const char*, ui::DockSide> sides[] = {{"Izquierda", ui::DockSide::Left},
                                                               {"Derecha", ui::DockSide::Right},
                                                               {"Abajo", ui::DockSide::Bottom}};
        for (const auto& [name, side] : sides)
            items.push_back(Item::check(name, dock_->sideOf(id) == side, [this, id, side = side]() { dock_->movePanel(id, side); }));
        return items;
    }

    std::vector<Item> viewMenu()
    {
        std::vector<Item> panels;
        panels.push_back(Item::submenuItem("Jerarquía", sideMenu("jerarquia")));
        panels.push_back(Item::submenuItem("Archivos", sideMenu("archivos")));
        panels.push_back(Item::submenuItem("Inspector", sideMenu("inspector")));
        panels.push_back(Item::submenuItem("Rendimiento", sideMenu("rendimiento")));
        return {Item::submenuItem("Posición de los paneles", std::move(panels)), Item::line(),
                Item::item("Restablecer disposición", [this]() {
                    dock_->movePanel("jerarquia", ui::DockSide::Left);
                    dock_->movePanel("archivos", ui::DockSide::Left);
                    dock_->movePanel("inspector", ui::DockSide::Right);
                    dock_->movePanel("rendimiento", ui::DockSide::Bottom);
                })};
    }

    std::vector<ui::IconItem> fileItems()
    {
        auto make = [](const char* name, ui::Color color, const char* badge, bool folder) {
            ui::IconItem item;
            item.text   = name;
            item.color  = color;
            item.badge  = badge;
            item.folder = folder;
            return item;
        };
        const ui::Color folder = ui::rgb(0xfacc15);
        return {make("escenas", folder, "", true),        make("modelos", folder, "", true),
                make("texturas", folder, "", true),       make("audio", folder, "", true),
                make("scripts", folder, "", true),        make("nivel1.zscene3d", ui::rgb(0x818cf8), "ESC", false),
                make("cubo.obj", ui::rgb(0x66d98c), "3D", false), make("suelo.png", ui::rgb(0x4dd0e1), "IMG", false),
                make("salto.wav", ui::rgb(0xf472b6), "SND", false), make("Jugador.cs", ui::rgb(0xc084fc), "C#", false),
                make("Enemigo.cpp", ui::rgb(0xc084fc), "C++", false), make("notas.txt", ui::rgb(0x9ca3af), "TXT", false)};
    }

    std::unique_ptr<ui::Widget> hierarchyPanel()
    {
        auto scroll = std::make_unique<ui::ScrollContainer>(true, false);
        auto* tree  = scroll->add<ui::TreeView>();
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
            auto popup = std::make_unique<ui::PopupMenu>(std::vector<Item>{
                Item::submenuItem("Añadir", {Item::item("Nodo vacío"), Item::item("Cubo")}), Item::line(),
                Item::item("Duplicar", {}, "Ctrl+D"), Item::item("Eliminar", {}, "Supr")});
            popup->setAnchor(position);
            context_->openPopup(std::move(popup));
        });
        return scroll;
    }

    std::unique_ptr<ui::Widget> filesPanel()
    {
        auto column = std::make_unique<ui::VBox>();
        column->setPadding(ui::Edges::all(8.f));
        auto* top    = column->add<ui::HBox>();
        auto* search = top->add<ui::LineEdit>();
        search->setPlaceholder("Buscar archivos...");
        search->setExpand(true, false);
        auto* mode = top->add<ui::Button>(std::string("Lista"));
        mode->setToggle(true);
        mode->setTooltip("Cambia entre iconos y lista");

        auto* scroll = column->add<ui::ScrollContainer>(true, false);
        scroll->setExpand(true, true);
        auto* grid = scroll->add<ui::IconGrid>();
        grid->setItems(fileItems());
        mode->setOnToggled([grid](bool list) { grid->setListMode(list); });
        return column;
    }

    std::unique_ptr<ui::Widget> inspectorPanel()
    {
        auto outer = std::make_unique<ui::Panel>();
        outer->setPadding(ui::Edges::all(10.f));
        auto* column = outer->add<ui::VBox>();
        auto* title  = column->add<ui::Label>(std::string("Cubo"));
        title->setBold(true);
        column->add<ui::Separator>();
        column->add<ui::Label>(std::string("Nombre"));
        column->add<ui::LineEdit>()->setText("Cubo");
        column->add<ui::Label>(std::string("Posición"));
        auto* position = column->add<ui::HBox>();
        const std::pair<const char*, ui::Color> axes[] = {{"X", ui::rgb(0xef4444)}, {"Y", ui::rgb(0x4ade80)}, {"Z", ui::rgb(0x60a5fa)}};
        double values[]                                = {0.0, 1.5, -2.0};
        for (int i = 0; i < 3; ++i)
        {
            auto* field = position->add<ui::NumberEdit>();
            field->setStep(0.1);
            field->setLabel(axes[i].first, axes[i].second);
            field->setValue(values[i]);
            field->setExpand(true, false);
        }
        column->add<ui::Label>(std::string("Color"));
        auto* color = column->add<ui::ColorButton>();
        color->setColor(ui::rgb(0x6366f1));
        column->add<ui::Label>(std::string("Calidad de sombras"));
        auto* quality = column->add<ui::OptionButton>();
        quality->setItems({"Baja", "Media", "Alta"});
        quality->setSelected(1);
        auto* shadow = column->add<ui::CheckBox>(std::string("Proyecta sombra"));
        shadow->setChecked(true);
        shadow->setTooltip("El objeto proyecta sombra sobre los demás");
        column->add<ui::Label>(std::string("Notas"));
        auto* notes = column->add<ui::TextEdit>();
        notes->setText("Primera línea\nSegunda línea\nTercera línea");
        notes->setExpand(true, true);
        progress_ = column->add<ui::ProgressBar>();
        return outer;
    }

    void build()
    {
        const ui::Theme& theme = context_->theme();
        auto* page             = context_->root().add<ui::VBox>();
        page->setSeparation(0.f);

        auto* top = makePanel(page, theme.deep);
        top->setPadding(ui::Edges::symmetric(6.f, 0.f));
        auto* topRow = top->add<ui::HBox>();
        auto* bar    = topRow->add<ui::MenuBar>();
        bar->addMenu("Proyecto", std::vector<Item>{Item::item("Nuevo proyecto..."), Item::item("Abrir proyecto...")});
        bar->addMenu("Escena", std::vector<Item>{Item::item("Guardar", {}, "Ctrl+S"), Item::item("Guardar como...")});
        bar->addMenu("Editar", std::vector<Item>{Item::item("Copiar", {}, "Ctrl+C"), Item::item("Pegar", {}, "Ctrl+V")});
        bar->addMenu("Ver", [this]() { return viewMenu(); });
        auto* spacer = topRow->add<ui::Widget>();
        spacer->setExpand(true, false);
        auto* play = topRow->add<ui::Button>(std::string("Jugar"));
        play->setToggle(true);
        play->setTooltip("Ejecuta la escena (F5)");
        topRow->add<ui::Button>(std::string("Detener"))->setTooltip("Detiene la escena (F5)");

        dock_ = page->add<ui::DockSpace>();
        dock_->setExpand(true, true);
        auto* viewport = new ui::ViewportWidget();
        viewport->setBackground(ui::rgb(0x0f1117));
        dock_->setCenter(std::unique_ptr<ui::Widget>(viewport));
        dock_->addPanel("jerarquia", "Jerarquía", hierarchyPanel(), ui::DockSide::Left);
        dock_->addPanel("archivos", "Archivos", filesPanel(), ui::DockSide::Left);
        dock_->addPanel("inspector", "Inspector", inspectorPanel(), ui::DockSide::Right);
        auto perf = std::make_unique<ui::Panel>();
        perf->setPadding(ui::Edges::all(10.f));
        perf->add<ui::Label>(std::string("60 FPS    171 MB en uso"));
        dock_->addPanel("rendimiento", "Rendimiento", std::move(perf), ui::DockSide::Bottom);
        dock_->setFractions(0.26f, 0.26f, 0.2f);

        auto* status = makePanel(page, theme.deep);
        status->setPadding(ui::Edges::symmetric(12.f, 3.f));
        status->add<ui::Label>(std::string("ProyectoEjemplo  /  escena.zscene3d"))->setColor(theme.textDim);
    }

    std::unique_ptr<ui::Context> context_;
    std::unique_ptr<ui::EngineHost> host_;
    ui::DockSpace* dock_       = nullptr;
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
