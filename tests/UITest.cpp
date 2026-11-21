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

Vec2 centerOf(const Rect& r)
{
    return r.center();
}

void menus()
{
    Rig rig;
    auto* box = rig.ctx.root().add<VBox>();
    auto* bar = box->add<MenuBar>();
    int saved = 0;
    int inner = 0;
    int disabledRuns = 0;
    MenuItem disabled = MenuItem::item("Bloqueado", [&] { ++disabledRuns; });
    disabled.enabled  = false;
    bar->addMenu("Archivo", std::vector<MenuItem>{MenuItem::item("Guardar", [&] { ++saved; }, "Ctrl+S"), MenuItem::line(),
                                                    MenuItem::submenuItem("Más", {MenuItem::item("Dentro", [&] { ++inner; })}),
                                                    disabled});
    bar->addMenu("Editar", std::vector<MenuItem>{MenuItem::item("Copiar")});
    rig.update();

    const Vec2 title0 = centerOf(bar->titleRect(0));
    rig.click(title0.x, title0.y);
    rig.update();
    check(rig.ctx.hasPopups() && bar->openIndex() == 0, "un clic en un título abre su menú", static_cast<float>(bar->openIndex()));

    const Vec2 title1 = centerOf(bar->titleRect(1));
    rig.move(title1.x, title1.y);
    rig.update();
    check(bar->openIndex() == 1 && rig.ctx.popupCount() == 1, "con un menú abierto, pasar a otro título lo cambia", static_cast<float>(bar->openIndex()));

    rig.move(title0.x, title0.y);
    rig.update();
    auto* popup = dynamic_cast<PopupMenu*>(rig.ctx.topPopup());
    check(popup != nullptr, "el menú emergente existe");
    if (!popup)
        return;

    const Vec2 save = centerOf(popup->itemRect(0));
    rig.click(save.x, save.y);
    check(saved == 1 && !rig.ctx.hasPopups() && bar->openIndex() == -1, "elegir una opción la ejecuta y cierra el menú", static_cast<float>(saved));

    rig.click(title0.x, title0.y);
    rig.update();
    popup = dynamic_cast<PopupMenu*>(rig.ctx.topPopup());
    const Vec2 blocked = centerOf(popup->itemRect(3));
    rig.click(blocked.x, blocked.y);
    check(disabledRuns == 0 && rig.ctx.hasPopups(), "una opción desactivada no se ejecuta ni cierra el menú");

    const Vec2 more = centerOf(popup->itemRect(2));
    rig.move(more.x, more.y);
    rig.update();
    check(rig.ctx.popupCount() == 2, "pasar sobre una opción con submenú lo abre", static_cast<float>(rig.ctx.popupCount()));
    auto* sub = dynamic_cast<PopupMenu*>(rig.ctx.topPopup());
    if (sub)
    {
        const Vec2 inside = centerOf(sub->itemRect(0));
        rig.move(inside.x, inside.y);
        rig.click(inside.x, inside.y);
        check(inner == 1 && !rig.ctx.hasPopups(), "una opción del submenú se ejecuta y cierra todo", static_cast<float>(inner));
    }

    rig.click(title0.x, title0.y);
    rig.update();
    check(rig.ctx.mouseButton(MouseButton::Left, true, {300.f, 250.f}, {}) && !rig.ctx.hasPopups(),
          "un clic fuera cierra el menú y no pasa al widget de debajo");
    rig.up(300.f, 250.f);

    rig.click(title0.x, title0.y);
    rig.update();
    rig.key(KeyCode::Down);
    rig.key(KeyCode::Enter);
    check(saved == 2 && !rig.ctx.hasPopups(), "el menú se maneja con teclado: Abajo y Enter", static_cast<float>(saved));

    rig.click(title0.x, title0.y);
    rig.update();
    rig.key(KeyCode::Escape);
    check(!rig.ctx.hasPopups(), "Escape cierra el menú");
}

void optionButton()
{
    Rig rig;
    auto* box = rig.ctx.root().add<VBox>();
    auto* opt = box->add<OptionButton>();
    opt->setItems({"Bajo", "Medio", "Alto"});
    int picked = -1;
    opt->setOnSelected([&](int i) { picked = i; });
    rig.update();
    check(opt->selected() == 0, "la lista desplegable empieza en la primera opción");

    rig.click(40.f, 15.f);
    rig.update();
    auto* popup = dynamic_cast<PopupMenu*>(rig.ctx.topPopup());
    check(popup != nullptr && popup->items().size() == 3, "pulsar la lista muestra las opciones");
    if (!popup)
        return;
    const Vec2 third = centerOf(popup->itemRect(2));
    rig.click(third.x, third.y);
    check(opt->selected() == 2 && picked == 2 && !rig.ctx.hasPopups(), "elegir una opción la selecciona y avisa", static_cast<float>(picked));
}

void tooltips()
{
    Rig rig;
    auto* box = rig.ctx.root().add<VBox>();
    auto* btn = box->add<Button>(std::string("Ayuda"));
    btn->setTooltip("Texto de ayuda");
    rig.update();

    rig.move(30.f, 15.f);
    rig.ctx.update(0.3f);
    rig.ctx.update(0.3f);
    check(!rig.ctx.tooltipVisible(), "el tooltip no aparece enseguida");
    rig.ctx.update(0.3f);
    check(rig.ctx.tooltipVisible(), "el tooltip aparece tras medio segundo");
    rig.move(300.f, 250.f);
    rig.ctx.update(0.f);
    check(!rig.ctx.tooltipVisible(), "el tooltip desaparece al salir del widget");
}

void tree()
{
    Rig rig;
    auto* scroll = rig.ctx.root().add<ScrollContainer>(true, false);
    auto* view   = scroll->add<TreeView>();
    TreeItem* a  = view->root()->add("A");
    TreeItem* a1 = a->add("A1");
    TreeItem* a2 = a->add("A2");
    TreeItem* b  = view->root()->add("B");
    int selectionEvents   = 0;
    TreeItem* activated   = nullptr;
    TreeItem* contextItem = nullptr;
    view->setOnSelectionChanged([&] { ++selectionEvents; });
    view->setOnActivated([&](TreeItem* i) { activated = i; });
    view->setOnContextMenu([&](TreeItem* i, Vec2) { contextItem = i; });
    rig.update();

    check(view->visibleRowCount() == 4, "el árbol muestra los nodos desplegados", static_cast<float>(view->visibleRowCount()));

    Vec2 row3 = centerOf(view->rowRect(3));
    rig.click(row3.x + 40.f, row3.y);
    check(b->selected && !a->selected && view->current() == b && selectionEvents >= 1, "un clic selecciona una fila");

    Vec2 row0 = centerOf(view->rowRect(0));
    Modifiers withCtrl;
    withCtrl.ctrl = true;
    rig.down(row0.x + 60.f, row0.y, withCtrl);
    rig.up(row0.x + 60.f, row0.y, withCtrl);
    check(a->selected && b->selected, "Ctrl+clic añade a la selección", static_cast<float>(view->selection().size()));

    const Vec2 arrow = centerOf(view->arrowRect(0));
    rig.click(arrow.x, arrow.y);
    rig.update();
    check(!a->expanded && view->visibleRowCount() == 2, "pulsar la flecha pliega el nodo", static_cast<float>(view->visibleRowCount()));

    rig.click(arrow.x, arrow.y);
    rig.update();
    check(a->expanded && view->visibleRowCount() == 4, "y la flecha lo vuelve a desplegar");

    row0 = centerOf(view->rowRect(0));
    rig.click(row0.x + 60.f, row0.y);
    rig.click(row0.x + 60.f, row0.y);
    check(activated == a && !a->expanded, "doble clic activa el nodo y alterna su despliegue");
    a->expanded = true;
    rig.update();
    view->select(a, false);
    rig.key(KeyCode::Down);
    check(view->current() == a1 && a1->selected && !a->selected, "las flechas mueven la selección");

    rig.key(KeyCode::Down, shift());
    check(a1->selected && a2->selected, "Mayús+flecha amplía la selección");

    view->select(a, false);
    rig.key(KeyCode::Left);
    rig.update();
    check(!a->expanded, "Izquierda pliega un nodo desplegado");
    rig.key(KeyCode::Right);
    rig.update();
    check(a->expanded, "Derecha lo despliega");

    row3 = centerOf(view->rowRect(3));
    rig.ctx.mouseMove({row3.x + 40.f, row3.y});
    rig.ctx.mouseButton(MouseButton::Right, true, {row3.x + 40.f, row3.y}, {});
    rig.ctx.mouseButton(MouseButton::Right, false, {row3.x + 40.f, row3.y}, {});
    check(contextItem == b && b->selected, "clic derecho selecciona y pide el menú contextual");

    view->root()->clear();
    for (int i = 0; i < 40; ++i)
        view->root()->add("Fila " + std::to_string(i));
    rig.update();
    rig.click(100.f, 10.f);
    rig.key(KeyCode::End);
    rig.update();
    check(scroll->scroll().y > 100.f, "al ir al final el árbol se desplaza para mostrar la fila", scroll->scroll().y);
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
    menus();
    optionButton();
    tooltips();
    tree();
    drawing();
    lifetime();

    std::printf("\n%s: %d fallos\n", failures == 0 ? "TODO BIEN" : "HAY ERRORES", failures);
    return failures == 0 ? 0 : 1;
}
