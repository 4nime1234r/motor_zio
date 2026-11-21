#include <cmath>
#include <cstdio>
#include <string>

#include "zio/ui/UI.h"

using namespace zio;
using namespace zio::ui;
using events::KeyCode;
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

bool near(float a, float b, float tolerance = 0.5f)
{
    return std::fabs(a - b) <= tolerance;
}

struct Rig
{
    Context ctx;

    explicit Rig(float w = 400.f, float h = 300.f)
    {
        ctx.setSize({w, h});
    }

    void update() { ctx.update(0.f); }
    void move(float x, float y) { ctx.mouseMove({x, y}); }
    void down(float x, float y, Modifiers m = {})
    {
        ctx.mouseMove({x, y});
        ctx.mouseButton(MouseButton::Left, true, {x, y}, m);
    }
    void up(float x, float y, Modifiers m = {}) { ctx.mouseButton(MouseButton::Left, false, {x, y}, m); }
    void click(float x, float y)
    {
        down(x, y);
        up(x, y);
    }
    void key(KeyCode code, Modifiers m = {}) { ctx.key(code, true, false, m); }
    void type(const std::string& s)
    {
        std::size_t i = 0;
        while (i < s.size())
            ctx.text(text::decode(s, i));
    }
};

Modifiers shift()
{
    Modifiers m;
    m.shift = true;
    return m;
}

Modifiers ctrl()
{
    Modifiers m;
    m.ctrl = true;
    return m;
}

struct Probe : Widget
{
    int enters = 0;
    int leaves = 0;
    int moves  = 0;
    int ups    = 0;
    Vec2 lastMove;

    bool onMouseDown(const MouseEvent&) override { return true; }
    bool onMouseUp(const MouseEvent&) override
    {
        ++ups;
        return true;
    }
    bool onMouseMove(Vec2 p) override
    {
        ++moves;
        lastMove = p;
        return true;
    }
    void onMouseEnter() override { ++enters; }
    void onMouseLeave() override { ++leaves; }
};

struct CountingPainter : Painter
{
    int clipsPushed = 0;
    int clipsPopped = 0;
    int rects       = 0;
    int rounds      = 0;
    int lines       = 0;
    int texts       = 0;

    void pushClip(const Rect&) override { ++clipsPushed; }
    void popClip() override { ++clipsPopped; }
    void fillRect(const Rect&, Color) override { ++rects; }
    void fillRoundRect(const Rect&, Color, float) override { ++rounds; }
    void line(Vec2, Vec2, Color, float) override { ++lines; }
    void triangle(Vec2, Vec2, Vec2, Color) override {}
    void text(Font&, Vec2, std::string_view, Color) override { ++texts; }
};

void verticalLayout()
{
    Rig rig;
    auto* box = rig.ctx.root().add<VBox>();
    auto* a   = box->add<Button>(std::string("a"));
    auto* b   = box->add<Button>(std::string("b"));
    auto* c   = box->add<Button>(std::string("c"));
    b->setExpand(false, true);
    rig.update();

    check(near(a->rect().y, 0.f) && near(a->rect().h, 30.f), "VBox: el primer botón mide su mínimo", a->rect().h);
    check(near(b->rect().y, 36.f) && near(b->rect().h, 228.f), "VBox: el botón expansible ocupa el resto", b->rect().h);
    check(near(c->rect().y, 270.f) && near(c->rect().h, 30.f), "VBox: el último queda abajo", c->rect().y);
    check(near(a->rect().w, 400.f), "VBox: los hijos llenan el ancho", a->rect().w);
}

void horizontalStretch()
{
    Rig rig(406.f, 100.f);
    auto* box = rig.ctx.root().add<HBox>();
    auto* a   = box->add<Panel>();
    auto* b   = box->add<Panel>();
    a->setExpand(true, false, 1.f);
    b->setExpand(true, false, 3.f);
    rig.update();
    check(near(a->rect().w, 100.f) && near(b->rect().w, 300.f), "HBox: el reparto respeta la proporción 1:3", b->rect().w);
}

void buttons()
{
    Rig rig;
    auto* box = rig.ctx.root().add<VBox>();
    auto* btn = box->add<Button>(std::string("Pulsar"));
    int clicks = 0;
    btn->setOnClick([&] { ++clicks; });
    rig.update();

    rig.click(50.f, 15.f);
    check(clicks == 1, "un botón responde a un clic", static_cast<float>(clicks));

    rig.down(50.f, 15.f);
    rig.up(300.f, 200.f);
    check(clicks == 1, "soltar fuera del botón cancela el clic", static_cast<float>(clicks));

    btn->setEnabled(false);
    rig.click(50.f, 15.f);
    check(clicks == 1, "un botón desactivado no responde", static_cast<float>(clicks));
    btn->setEnabled(true);

    rig.click(50.f, 15.f);
    rig.key(KeyCode::Space);
    check(clicks == 3, "Espacio activa el botón con foco", static_cast<float>(clicks));
}

void toggles()
{
    Rig rig;
    auto* box = rig.ctx.root().add<VBox>();
    auto* tog = box->add<Button>(std::string("Modo"));
    auto* chk = box->add<CheckBox>(std::string("Opción"));
    tog->setToggle(true);
    bool state = false;
    tog->setOnToggled([&](bool v) { state = v; });
    bool checked = false;
    chk->setOnToggled([&](bool v) { checked = v; });
    rig.update();

    rig.click(50.f, 15.f);
    check(tog->toggled() && state, "un botón de alternar se activa", 1.f);
    rig.click(50.f, 15.f);
    check(!tog->toggled() && !state, "y se desactiva con otro clic", 0.f);

    rig.click(8.f, chk->rect().y + chk->rect().h * 0.5f);
    check(chk->checked() && checked, "una casilla se marca con clic", 1.f);
}

void lineEdit()
{
    Rig rig;
    auto* box  = rig.ctx.root().add<VBox>();
    auto* edit = box->add<LineEdit>();
    std::string last;
    edit->setOnChanged([&](const std::string& t) { last = t; });
    rig.update();

    rig.click(20.f, 15.f);
    check(rig.ctx.focused() == edit, "un clic da foco al campo de texto");

    rig.type("Hola");
    check(edit->text() == "Hola" && last == "Hola", "escribir añade texto y avisa del cambio");

    rig.key(KeyCode::Backspace);
    check(edit->text() == "Hol", "Retroceso borra un carácter");

    rig.key(KeyCode::Left);
    rig.key(KeyCode::Left);
    rig.type("X");
    check(edit->text() == "HXol", "las flechas mueven el cursor", static_cast<float>(edit->caret()));

    rig.key(KeyCode::Home, shift());
    check(edit->hasSelection() && edit->selectedText() == "HX", "Mayús+Inicio selecciona hasta el principio");

    rig.key(KeyCode::A, ctrl());
    check(edit->selectedText() == "HXol", "Ctrl+A selecciona todo");

    rig.key(KeyCode::C, ctrl());
    rig.key(KeyCode::End);
    rig.key(KeyCode::V, ctrl());
    check(edit->text() == "HXolHXol", "Ctrl+C y Ctrl+V copian y pegan", 0.f);

    rig.type("Z");
    rig.key(KeyCode::A, ctrl());
    rig.type("q");
    check(edit->text() == "q", "escribir con selección la reemplaza");

    edit->setText("foo bar");
    rig.key(KeyCode::End);
    rig.key(KeyCode::Backspace, ctrl());
    check(edit->text() == "foo ", "Ctrl+Retroceso borra una palabra");

    edit->setText("");
    rig.ctx.text(U'ñ');
    check(edit->text() == "ñ" && edit->caret() == 2, "se admite texto no ASCII (ñ ocupa 2 bytes)", static_cast<float>(edit->caret()));
    rig.key(KeyCode::Backspace);
    check(edit->text().empty(), "borrar quita el carácter completo, no un byte");

    std::string submitted;
    edit->setOnSubmit([&](const std::string& t) { submitted = t; });
    rig.type("ok");
    rig.key(KeyCode::Enter);
    check(submitted == "ok", "Enter envía el texto");

    rig.key(KeyCode::Escape);
    check(rig.ctx.focused() == nullptr, "Escape quita el foco");
}

void lineEditMouse()
{
    Rig rig;
    auto* box  = rig.ctx.root().add<VBox>();
    auto* edit = box->add<LineEdit>();
    edit->setText("abcdef");
    rig.update();

    rig.down(8.f + 3.f * 7.f + 1.f, 15.f);
    rig.move(8.f + 5.f * 7.f, 15.f);
    rig.up(8.f + 5.f * 7.f, 15.f);
    check(edit->selectedText() == "de", "arrastrar con el ratón selecciona texto", static_cast<float>(edit->caret()));

    rig.down(8.f + 1.f * 7.f, 15.f);
    rig.up(8.f + 1.f * 7.f, 15.f);
    rig.down(8.f + 1.f * 7.f, 15.f);
    rig.up(8.f + 1.f * 7.f, 15.f);
    check(edit->selectedText() == "abcdef", "doble clic selecciona la palabra", 0.f);
}

void focusNavigation()
{
    Rig rig;
    auto* box = rig.ctx.root().add<VBox>();
    auto* e1  = box->add<LineEdit>();
    auto* e2  = box->add<LineEdit>();
    rig.update();

    rig.click(20.f, e1->rect().y + 10.f);
    check(rig.ctx.focused() == e1, "primer campo con foco");
    rig.key(KeyCode::Tab);
    check(rig.ctx.focused() == e2, "Tab pasa al siguiente campo");
    rig.key(KeyCode::Tab, shift());
    check(rig.ctx.focused() == e1, "Mayús+Tab vuelve al anterior");
    rig.click(399.f, 299.f);
    check(rig.ctx.focused() == nullptr, "un clic en el vacío quita el foco");
}

void hoverAndCapture()
{
    Rig rig;
    auto* box   = rig.ctx.root().add<VBox>();
    auto* probe = box->add<Probe>();
    probe->setMinSize({100.f, 50.f});
    probe->setExpand(false, false);
    rig.update();

    rig.move(500.f, 500.f);
    rig.move(10.f, 10.f);
    check(probe->enters == 1 && rig.ctx.hovered() == probe, "entrar en un widget lo marca como hover", static_cast<float>(probe->enters));

    rig.move(200.f, 200.f);
    check(probe->leaves == 1, "salir del widget avisa", static_cast<float>(probe->leaves));

    rig.down(10.f, 10.f);
    rig.move(350.f, 280.f);
    check(probe->moves >= 1 && near(probe->lastMove.x, 350.f), "un widget pulsado sigue recibiendo el ratón fuera", probe->lastMove.x);
    rig.up(350.f, 280.f);
    check(probe->ups == 1, "y recibe el soltar aunque esté fuera", static_cast<float>(probe->ups));

    check(!rig.ctx.mouseMove({399.f, 299.f}), "el ratón sobre el vacío no cuenta como interfaz");
    check(rig.ctx.mouseMove({10.f, 10.f}), "el ratón sobre un widget sí cuenta como interfaz");
}

void scrolling()
{
    Rig rig;
    auto* scroll = rig.ctx.root().add<ScrollContainer>(true, false);
    auto* list   = scroll->add<VBox>();
    for (int i = 0; i < 20; ++i)
        list->add<Button>(std::string("fila"));
    rig.update();

    check(near(scroll->contentSize().y, 714.f), "el contenido mide la suma de sus hijos", scroll->contentSize().y);
    check(near(list->rect().w, 390.f), "el contenido deja sitio a la barra", list->rect().w);

    rig.ctx.mouseScroll({10.f, 10.f}, {0.f, -1.f}, {});
    rig.update();
    check(near(scroll->scroll().y, 54.f), "la rueda desplaza el contenido", scroll->scroll().y);

    for (int i = 0; i < 30; ++i)
        rig.ctx.mouseScroll({10.f, 10.f}, {0.f, -1.f}, {});
    check(near(scroll->scroll().y, 414.f), "el desplazamiento se limita al final", scroll->scroll().y);

    for (int i = 0; i < 30; ++i)
        rig.ctx.mouseScroll({10.f, 10.f}, {0.f, 1.f}, {});
    check(near(scroll->scroll().y, 0.f), "y al principio", scroll->scroll().y);

    rig.down(395.f, 10.f);
    rig.move(395.f, 60.f);
    rig.up(395.f, 60.f);
    check(near(scroll->scroll().y, 119.f, 2.f), "arrastrar la barra desplaza el contenido", scroll->scroll().y);
}

void splitting()
{
    Rig rig(406.f, 200.f);
    auto* split = rig.ctx.root().add<SplitContainer>(false);
    auto* left  = split->add<Panel>();
    auto* right = split->add<Panel>();
    rig.update();

    check(near(left->rect().w, 200.f) && near(right->rect().x, 206.f), "la división reparte a partes iguales", left->rect().w);

    rig.move(203.f, 100.f);
    check(rig.ctx.cursor() == Cursor::ResizeHorizontal, "el cursor cambia sobre el divisor");

    rig.down(203.f, 100.f);
    rig.move(303.f, 100.f);
    rig.up(303.f, 100.f);
    rig.update();
    check(near(left->rect().w, 300.f), "arrastrar el divisor cambia el reparto", left->rect().w);

    rig.move(50.f, 100.f);
    check(rig.ctx.cursor() == Cursor::Arrow, "el cursor vuelve a flecha fuera del divisor");
}

void tabs()
{
    Rig rig;
    auto* tabs = rig.ctx.root().add<TabContainer>();
    auto* p0   = tabs->addPage(std::make_unique<Panel>(), "Uno");
    auto* p1   = tabs->addPage(std::make_unique<Panel>(), "Dos");
    std::size_t changed = 99;
    tabs->setOnChanged([&](std::size_t i) { changed = i; });
    rig.update();

    check(p0->visible() && !p1->visible(), "solo la primera pestaña está visible al inicio");
    rig.click(6.f + 45.f + 2.f + 10.f, 16.f);
    check(tabs->current() == 1 && !p0->visible() && p1->visible() && changed == 1, "un clic en una pestaña la activa", static_cast<float>(tabs->current()));
    check(near(p1->rect().y, 32.f), "la página queda bajo la cabecera", p1->rect().y);
}

void drawing()
{
    Rig rig;
    auto* tabs  = rig.ctx.root().add<TabContainer>();
    auto* page  = tabs->addPage(std::make_unique<Panel>(), "Principal");
    auto* split = page->add<SplitContainer>(false);
    auto* left  = split->add<ScrollContainer>(true, false);
    auto* list  = left->add<VBox>();
    list->add<Label>(std::string("Etiqueta"));
    list->add<Button>(std::string("Botón"));
    list->add<CheckBox>(std::string("Casilla"))->setChecked(true);
    list->add<LineEdit>()->setText("Texto");
    list->add<Separator>();
    list->add<ProgressBar>()->setValue(0.5f);
    split->add<Panel>();
    rig.update();

    CountingPainter painter;
    rig.ctx.draw(painter);
    check(painter.clipsPushed == painter.clipsPopped, "los recortes quedan equilibrados", static_cast<float>(painter.clipsPushed));
    check(painter.texts >= 5 && painter.rounds > 0 && painter.rects > 0 && painter.lines >= 2, "todos los widgets se dibujan", static_cast<float>(painter.texts));
}

void lifetime()
{
    Rig rig;
    auto* box  = rig.ctx.root().add<VBox>();
    auto* edit = box->add<LineEdit>();
    rig.update();
    rig.click(20.f, 15.f);
    check(rig.ctx.focused() == edit, "el campo tiene foco antes de borrarlo");
    box->removeChild(edit);
    check(rig.ctx.focused() == nullptr && rig.ctx.hovered() == nullptr, "quitar un widget limpia foco y hover sin dejar punteros colgando");
}

}

int main()
{
    verticalLayout();
    horizontalStretch();
    buttons();
    toggles();
    lineEdit();
    lineEditMouse();
    focusNavigation();
    hoverAndCapture();
    scrolling();
    splitting();
    tabs();
    drawing();
    lifetime();

    std::printf("\n%s: %d fallos\n", failures == 0 ? "TODO BIEN" : "HAY ERRORES", failures);
    return failures == 0 ? 0 : 1;
}
