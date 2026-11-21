#include <cmath>
#include <cstdio>
#include <string>

#include "zio/ui/UI.h"

using namespace zio;
using namespace zio::ui;
using events::MouseButton;

namespace
{

int failures = 0;

void check(bool condition, const char* name, float value = 0.f)
{
    std::printf("%s %s (%.3f)\n", condition ? "[ok]  " : "[FALLO]", name, value);
    if (!condition)
        ++failures;
}

bool near(float a, float b, float tolerance = 1.f)
{
    return std::fabs(a - b) <= tolerance;
}

struct Rig
{
    Context ctx;
    DockSpace* dock = nullptr;
    Panel* center   = nullptr;
    Button* tree    = nullptr;
    Label* files    = nullptr;
    Label* props    = nullptr;
    Label* perf     = nullptr;
    int treeClicks  = 0;

    Rig()
    {
        ctx.setSize({1000.f, 600.f});
        dock = ctx.root().add<DockSpace>();

        auto centerPanel = std::make_unique<Panel>();
        center           = centerPanel.get();
        dock->setCenter(std::move(centerPanel));

        auto treeButton = std::make_unique<Button>(std::string("Arbol"));
        tree            = treeButton.get();
        tree->setOnClick([this] { ++treeClicks; });
        dock->addPanel("jerarquia", "Jerarquía", std::move(treeButton), DockSide::Left);

        auto filesLabel = std::make_unique<Label>(std::string("archivos"));
        files           = filesLabel.get();
        dock->addPanel("archivos", "Archivos", std::move(filesLabel), DockSide::Left);

        auto propsLabel = std::make_unique<Label>(std::string("inspector"));
        props           = propsLabel.get();
        dock->addPanel("inspector", "Inspector", std::move(propsLabel), DockSide::Right);

        auto perfLabel = std::make_unique<Label>(std::string("rendimiento"));
        perf           = perfLabel.get();
        dock->addPanel("rendimiento", "Rendimiento", std::move(perfLabel), DockSide::Bottom);
        update();
    }

    void update() { ctx.update(0.f); }
    void click(float x, float y)
    {
        ctx.mouseMove({x, y});
        ctx.mouseButton(MouseButton::Left, true, {x, y}, {});
        ctx.mouseButton(MouseButton::Left, false, {x, y}, {});
    }
};

TabContainer* tabsOf(Widget* content)
{
    return content ? dynamic_cast<TabContainer*>(content->parent()) : nullptr;
}

void positions()
{
    Rig rig;
    check(rig.tree->rect().right() <= rig.center->rect().x + 1.f, "el panel izquierdo queda a la izquierda del centro", rig.tree->rect().right());
    check(rig.props->rect().x >= rig.center->rect().right() - 1.f, "el panel derecho queda a la derecha del centro", rig.props->rect().x);
    check(rig.perf->rect().y >= rig.center->rect().bottom() - 1.f, "el panel inferior queda debajo del centro", rig.perf->rect().y);
    check(rig.files->rect().y > rig.tree->rect().y, "dos paneles en un lado se apilan en vertical", rig.files->rect().y);
    check(near(rig.center->rect().x, rig.tree->rect().x + 218.f, 12.f), "el lado izquierdo ocupa su fracción", rig.center->rect().x);
    check(dynamic_cast<PanelFrame*>(rig.tree->parent()) != nullptr, "cada panel lateral va en un marco con título");
    check(tabsOf(rig.perf) != nullptr, "el panel inferior va en una pestaña");
}

void moving()
{
    Rig rig;
    rig.dock->movePanel("archivos", DockSide::Bottom);
    rig.update();

    TabContainer* tabs = tabsOf(rig.files);
    check(tabs != nullptr && tabsOf(rig.perf) == tabs && tabs->children().size() == 2, "mover un panel abajo lo une a las pestañas", tabs ? static_cast<float>(tabs->children().size()) : 0.f);
    check(rig.dock->sideOf("archivos") == DockSide::Bottom, "el panel recuerda su nuevo lado");
    check(rig.tree->rect().h > 400.f, "el panel que queda a la izquierda ocupa toda la altura", rig.tree->rect().h);
    check(rig.files->text() == "archivos", "el contenido del panel sobrevive al cambio");

    rig.dock->movePanel("jerarquia", DockSide::Right);
    rig.update();
    check(rig.tree->rect().x >= rig.center->rect().right() - 1.f, "mover un panel a la derecha lo coloca allí");
    check(near(rig.center->rect().x, 0.f), "sin paneles a la izquierda el centro ocupa desde el borde", rig.center->rect().x);
    check(rig.props->rect().y != rig.tree->rect().y, "los dos paneles de la derecha se apilan", rig.props->rect().y);
}

void interaction()
{
    Rig rig;
    rig.dock->movePanel("jerarquia", DockSide::Right);
    rig.update();
    const Vec2 target = rig.tree->rect().center();
    rig.click(target.x, target.y);
    check(rig.treeClicks == 1, "un botón sigue respondiendo después de mover su panel", static_cast<float>(rig.treeClicks));
}

void fractions()
{
    Rig rig;
    rig.dock->setFractions(0.3f, 0.2f, 0.3f);
    rig.update();
    check(near(rig.center->rect().x, 0.3f * 994.f + 6.f, 8.f), "se pueden fijar las proporciones por código", rig.center->rect().x);

    const float before = rig.dock->leftFraction();
    rig.dock->movePanel("inspector", DockSide::Bottom);
    rig.update();
    check(near(rig.dock->leftFraction(), before, 0.01f), "mover un panel conserva la proporción del lado izquierdo", rig.dock->leftFraction());
}

void dragging()
{
    Rig rig;
    const float dividerX = rig.center->rect().x - 3.f;
    rig.ctx.mouseMove({dividerX, 200.f});
    rig.ctx.mouseButton(MouseButton::Left, true, {dividerX, 200.f}, {});
    rig.ctx.mouseMove({dividerX + 100.f, 200.f});
    rig.ctx.mouseButton(MouseButton::Left, false, {dividerX + 100.f, 200.f}, {});
    rig.update();
    check(rig.center->rect().x > 300.f, "arrastrar el divisor ensancha el lado izquierdo", rig.center->rect().x);

    const float fraction = rig.dock->leftFraction();
    rig.dock->movePanel("rendimiento", DockSide::Right);
    rig.update();
    check(near(rig.dock->leftFraction(), fraction, 0.01f), "lo arrastrado se conserva al reorganizar", rig.dock->leftFraction());
}

void persistence()
{
    Rig rig;
    rig.dock->movePanel("archivos", DockSide::Bottom);
    rig.dock->movePanel("inspector", DockSide::Left);
    rig.dock->setFractions(0.28f, 0.18f, 0.35f);
    rig.update();
    const std::string saved = rig.dock->saveLayout();

    Rig other;
    other.dock->loadLayout(saved);
    other.update();
    check(other.dock->sideOf("archivos") == DockSide::Bottom && other.dock->sideOf("inspector") == DockSide::Left &&
              other.dock->sideOf("rendimiento") == DockSide::Bottom,
          "guardar y cargar la disposición restablece los lados");
    check(near(other.dock->leftFraction(), 0.28f, 0.02f) && near(other.dock->bottomFraction(), 0.35f, 0.02f),
          "y las proporciones", other.dock->leftFraction());
    check(saved.find("panel:archivos=abajo") != std::string::npos, "el texto guardado es legible");
}

void drawing()
{
    Rig rig;
    struct Counter : Painter
    {
        int pushed = 0, popped = 0, texts = 0;
        void pushClip(const Rect&) override { ++pushed; }
        void popClip() override { ++popped; }
        void fillRect(const Rect&, Color) override {}
        void fillRoundRect(const Rect&, Color, float) override {}
        void line(Vec2, Vec2, Color, float) override {}
        void triangle(Vec2, Vec2, Vec2, Color) override {}
        void text(Font&, Vec2, std::string_view, Color) override { ++texts; }
    } painter;
    rig.ctx.draw(painter);
    check(painter.pushed == painter.popped && painter.texts >= 4, "el área acoplable se dibuja con títulos y contenidos", static_cast<float>(painter.texts));
}

}

int main()
{
    positions();
    moving();
    interaction();
    fractions();
    dragging();
    persistence();
    drawing();

    std::printf("\n%s: %d fallos\n", failures == 0 ? "TODO BIEN" : "HAY ERRORES", failures);
    return failures == 0 ? 0 : 1;
}
