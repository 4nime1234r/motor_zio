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

bool near(double a, double b, double tolerance = 0.01)
{
    return std::fabs(a - b) <= tolerance;
}

struct Rig
{
    Context ctx;

    explicit Rig(float w = 400.f, float h = 300.f) { ctx.setSize({w, h}); }

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

void numberEdit()
{
    Rig rig;
    auto* box = rig.ctx.root().add<VBox>();
    auto* num = box->add<NumberEdit>();
    num->setRange(-10.0, 10.0);
    num->setStep(0.5);
    num->setDecimals(2);
    num->setValue(1.0);
    double notified = 99.0;
    int notifications = 0;
    num->setOnChanged([&](double v) {
        notified = v;
        ++notifications;
    });
    rig.update();

    check(num->formatted() == "1.00", "el número se muestra con sus decimales");

    rig.down(100.f, 15.f);
    rig.move(140.f, 15.f);
    rig.up(140.f, 15.f);
    check(near(num->value(), 6.0) && !num->editing(), "arrastrar en horizontal cambia el valor (4 px por paso)", static_cast<float>(num->value()));
    check(near(notified, 6.0) && notifications >= 1, "y avisa del cambio", static_cast<float>(notified));

    rig.down(100.f, 15.f);
    rig.move(400.f, 15.f);
    rig.up(400.f, 15.f);
    check(near(num->value(), 10.0), "el valor se limita al máximo", static_cast<float>(num->value()));

    rig.down(100.f, 15.f);
    rig.move(-300.f, 15.f);
    rig.up(-300.f, 15.f);
    check(near(num->value(), -10.0), "y al mínimo", static_cast<float>(num->value()));

    rig.click(100.f, 15.f);
    check(num->editing() && rig.ctx.focused() != nullptr, "un clic sin arrastrar entra en modo edición");

    rig.type("3.25");
    rig.key(KeyCode::Enter);
    check(near(num->value(), 3.25) && !num->editing(), "escribir un número y pulsar Enter lo aplica", static_cast<float>(num->value()));

    rig.click(100.f, 15.f);
    rig.type("7");
    rig.key(KeyCode::Escape);
    check(near(num->value(), 3.25) && !num->editing(), "Escape cancela la edición sin cambiar el valor", static_cast<float>(num->value()));

    rig.click(100.f, 15.f);
    rig.key(KeyCode::A, [] { Modifiers m; m.ctrl = true; return m; }());
    rig.type("abc");
    rig.key(KeyCode::Enter);
    check(near(num->value(), 3.25), "un texto que no es número se ignora", static_cast<float>(num->value()));

    rig.click(100.f, 15.f);
    rig.type("-2");
    rig.click(200.f, 250.f);
    check(near(num->value(), -2.0) && !num->editing(), "perder el foco confirma lo escrito", static_cast<float>(num->value()));

    rig.click(100.f, 15.f);
    rig.key(KeyCode::Escape);
    rig.click(10.f, 15.f);
    rig.key(KeyCode::Escape);
    rig.update();
    rig.ctx.setFocus(num);
    rig.key(KeyCode::Up);
    check(near(num->value(), -1.5), "con foco, la flecha arriba suma un paso", static_cast<float>(num->value()));
    Modifiers shift;
    shift.shift = true;
    rig.key(KeyCode::Down, shift);
    check(near(num->value(), -6.5), "Mayús multiplica el paso por 10", static_cast<float>(num->value()));

    rig.ctx.setFocus(nullptr);
    rig.ctx.setFocus(num);
    rig.type("5");
    check(num->editing(), "escribir un dígito con el campo enfocado empieza la edición");
    rig.key(KeyCode::Escape);
}


bool sameColor(Color a, Color b, float tolerance = 0.01f)
{
    return std::fabs(a.r - b.r) <= tolerance && std::fabs(a.g - b.g) <= tolerance && std::fabs(a.b - b.b) <= tolerance &&
           std::fabs(a.a - b.a) <= tolerance;
}

void colorMath()
{
    const Hsv red = toHsv(rgb(0xff0000));
    check(near(red.h, 0.0) && near(red.s, 1.0) && near(red.v, 1.0), "el rojo es matiz 0, saturación 1, valor 1");
    const Hsv cyan = toHsv(rgb(0x00ffff));
    check(near(cyan.h, 0.5), "el cian es matiz 0.5", cyan.h);
    const Hsv gray = toHsv(rgb(0x808080));
    check(near(gray.s, 0.0), "un gris no tiene saturación");

    bool roundTrip = true;
    for (std::uint32_t value : {0x6366f1u, 0xef4444u, 0x15171fu, 0xfacc15u, 0x66d98cu, 0xffffffu, 0x000000u})
        roundTrip = roundTrip && sameColor(fromHsv(toHsv(rgb(value)), 1.f), rgb(value), 0.002f);
    check(roundTrip, "pasar de RGB a HSV y volver conserva el color");

    check(toHex(rgb(0x6366f1), false) == "#6366F1", "el hexadecimal se escribe en mayúsculas");
    check(toHex(rgb(0x6366f1, 0.5f), true) == "#6366F180", "y con transparencia");

    Color parsed;
    check(parseHex("#336699", parsed) && sameColor(parsed, rgb(0x336699)), "se lee un hexadecimal con #");
    check(parseHex("33669980", parsed) && near(parsed.a, 128.0 / 255.0, 0.01), "o sin # y con transparencia", parsed.a);
    check(!parseHex("#12345", parsed) && !parseHex("zzzzzz", parsed), "un hexadecimal inválido se rechaza");
}

void colorPicker()
{
    Rig rig;
    auto* picker = rig.ctx.root().add<ColorPicker>();
    Color last{};
    int changes = 0;
    picker->setOnChanged([&](Color c) {
        last = c;
        ++changes;
    });
    picker->setColor(rgb(0xff0000));
    rig.update();

    const Rect sq = picker->squareRect();
    rig.click(sq.right() - 1.f, sq.y + 1.f);
    check(sameColor(picker->color(), rgb(0xff0000), 0.03f) && changes >= 1, "la esquina superior derecha del cuadro es el color puro", picker->color().r);

    rig.click(sq.x + 1.f, sq.bottom() - 1.f);
    check(picker->color().r < 0.03f && picker->color().g < 0.03f && picker->color().b < 0.03f, "la esquina inferior es negro", picker->color().r);

    rig.click(sq.right() - 1.f, sq.y + 1.f);
    const Rect hue = picker->hueRect();
    rig.click(hue.x + 4.f, hue.y + hue.h * 0.5f);
    check(near(picker->hsv().h, 0.5, 0.02) && sameColor(picker->color(), rgb(0x00ffff), 0.05f), "la barra de matiz cambia el color base (mitad = cian)", picker->hsv().h);

    const Rect alpha = picker->alphaRect();
    rig.click(alpha.x + alpha.w * 0.25f, alpha.y + 4.f);
    check(near(picker->color().a, 0.25, 0.02), "la barra de transparencia ajusta el alfa", picker->color().a);

    picker->setAlphaEnabled(false);
    check(near(picker->color().a, 1.0), "sin transparencia el alfa es 1");
    picker->setAlphaEnabled(true);

    picker->setColor(rgb(0x808080));
    const float hueBefore = picker->hsv().h;
    picker->setColor(rgb(0x000000));
    check(near(picker->hsv().h, hueBefore), "un color sin saturación no pierde el matiz que había", picker->hsv().h);

    picker->setColor(rgb(0x336699));
    const Rect box = picker->rect();
    rig.click(box.x + 30.f, picker->alphaRect().bottom() + 10.f + 15.f);
    Modifiers ctrl;
    ctrl.ctrl = true;
    rig.key(KeyCode::A, ctrl);
    rig.type("#FF8800");
    rig.key(KeyCode::Enter);
    check(sameColor(picker->color(), rgb(0xff8800), 0.02f), "escribir un hexadecimal y pulsar Enter cambia el color", picker->color().r);
}

void colorButton()
{
    Rig rig;
    auto* box = rig.ctx.root().add<VBox>();
    auto* btn = box->add<ColorButton>();
    btn->setColor(rgb(0x6366f1));
    Color picked{};
    btn->setOnChanged([&](Color c) { picked = c; });
    rig.update();

    rig.click(20.f, 15.f);
    rig.update();
    check(rig.ctx.hasPopups(), "pulsar el botón de color abre el selector");

    auto* panel = dynamic_cast<PopupPanel*>(rig.ctx.topPopup());
    ColorPicker* picker = panel && !panel->children().empty() ? dynamic_cast<ColorPicker*>(panel->children()[0].get()) : nullptr;
    check(picker != nullptr && sameColor(picker->color(), rgb(0x6366f1), 0.01f), "el selector empieza con el color del botón");
    if (!picker)
        return;

    const Rect sq = picker->squareRect();
    rig.click(sq.right() - 2.f, sq.y + 2.f);
    check(sameColor(btn->color(), picked, 0.001f) && btn->color().r != 0x63 / 255.f, "elegir en el selector cambia el botón y avisa", btn->color().r);
    check(rig.ctx.hasPopups(), "el selector sigue abierto al elegir");

    rig.click(390.f, 290.f);
    check(!rig.ctx.hasPopups(), "un clic fuera lo cierra");

    rig.click(20.f, 15.f);
    rig.update();
    rig.key(KeyCode::Escape);
    check(!rig.ctx.hasPopups(), "Escape también lo cierra");
}

void viewport()
{
    Rig rig;
    auto* vp = rig.ctx.root().add<ViewportWidget>();
    int downs = 0, ups = 0, moves = 0, scrolls = 0, keysDown = 0, keysUp = 0;
    Vec2 lastDelta;
    Vec2 lastPos;
    vp->setOnMouseDown([&](const MouseEvent&) { ++downs; });
    vp->setOnMouseUp([&](const MouseEvent&) { ++ups; });
    vp->setOnMouseMove([&](Vec2 p, Vec2 d) {
        ++moves;
        lastPos   = p;
        lastDelta = d;
    });
    vp->setOnScroll([&](Vec2, Modifiers) { ++scrolls; });
    vp->setOnKey([&](const KeyEvent&, bool down) {
        down ? ++keysDown : ++keysUp;
        return true;
    });
    rig.update();

    check(near(vp->viewportRect().w, 400.0) && near(vp->localPosition({50.f, 60.f}).y, 60.0), "el visor ocupa su hueco y convierte posiciones");

    rig.down(100.f, 100.f);
    check(downs == 1 && vp->dragging(), "pulsar en el visor lo marca como arrastrando");
    rig.move(150.f, 130.f);
    check(near(lastDelta.x, 50.0) && near(lastDelta.y, 30.0), "el movimiento llega con su desplazamiento", lastDelta.x);
    rig.move(900.f, 900.f);
    check(near(lastPos.x, 900.0), "sigue recibiendo el ratón aunque salga del visor", lastPos.x);
    rig.up(900.f, 900.f);
    check(ups == 1 && !vp->dragging(), "y el soltar aunque esté fuera");

    rig.ctx.mouseButton(MouseButton::Right, true, {200.f, 200.f}, {});
    check(vp->dragging() && vp->dragButton() == MouseButton::Right, "con el botón derecho también arrastra (para girar la cámara)");
    rig.ctx.mouseButton(MouseButton::Right, false, {200.f, 200.f}, {});

    rig.ctx.mouseScroll({100.f, 100.f}, {0.f, 1.f}, {});
    check(scrolls == 1, "la rueda llega al visor");

    rig.click(50.f, 50.f);
    rig.key(KeyCode::W);
    rig.ctx.key(KeyCode::W, false, false, {});
    check(keysDown == 1 && keysUp == 1, "las teclas llegan al visor con foco");
    check(moves >= 2, "se recibieron movimientos", static_cast<float>(moves));
}

void textEdit()
{
    Rig rig;
    auto* edit = rig.ctx.root().add<TextEdit>();
    edit->setText("uno\ndos\ntres");
    std::string last;
    edit->setOnChanged([&](const std::string& t) { last = t; });
    rig.update();
    check(edit->lineCount() == 3, "el texto se divide en líneas", static_cast<float>(edit->lineCount()));

    rig.click(20.f, 6.f + 20.2f + 5.f);
    check(edit->caretLine() == 1, "un clic coloca el cursor en esa línea", static_cast<float>(edit->caretLine()));

    rig.key(KeyCode::Home);
    rig.type("X");
    check(edit->text() == "uno\nXdos\ntres" && last == edit->text(), "se escribe en la posición del cursor");

    rig.key(KeyCode::Enter);
    check(edit->lineCount() == 4 && edit->text() == "uno\nX\ndos\ntres", "Enter parte la línea", static_cast<float>(edit->lineCount()));

    rig.key(KeyCode::Backspace);
    check(edit->text() == "uno\nXdos\ntres", "Retroceso al principio de una línea la une con la anterior");

    rig.key(KeyCode::Up);
    rig.key(KeyCode::End);
    check(edit->caretLine() == 0 && edit->caret() == 3, "Arriba y Fin llevan al final de la primera línea", static_cast<float>(edit->caret()));

    rig.key(KeyCode::Down, [] { Modifiers m; m.shift = true; return m; }());
    check(edit->hasSelection() && edit->selectedText().find('\n') != std::string::npos, "Mayús+Abajo selecciona entre líneas");

    rig.key(KeyCode::A, [] { Modifiers m; m.ctrl = true; return m; }());
    rig.key(KeyCode::C, [] { Modifiers m; m.ctrl = true; return m; }());
    rig.key(KeyCode::End, [] { Modifiers m; m.ctrl = true; return m; }());
    rig.key(KeyCode::V, [] { Modifiers m; m.ctrl = true; return m; }());
    check(edit->text() == "uno\nXdos\ntresuno\nXdos\ntres", "copiar y pegar conserva los saltos de línea");

    rig.key(KeyCode::Home, [] { Modifiers m; m.ctrl = true; return m; }());
    rig.key(KeyCode::Tab);
    check(edit->text().rfind("    uno", 0) == 0, "Tab inserta cuatro espacios en vez de cambiar el foco");

    std::string many;
    for (int i = 0; i < 100; ++i)
        many += "linea " + std::to_string(i) + "\n";
    edit->setText(many);
    rig.update();
    rig.click(30.f, 20.f);
    rig.key(KeyCode::End, [] { Modifiers m; m.ctrl = true; return m; }());
    check(edit->scrollY() > 1000.f && edit->caretLine() == 100, "al ir al final el texto se desplaza", edit->scrollY());

    rig.key(KeyCode::Home, [] { Modifiers m; m.ctrl = true; return m; }());
    check(near(edit->scrollY(), 0.0), "y al principio vuelve arriba");
    rig.ctx.mouseScroll({30.f, 30.f}, {0.f, -2.f}, {});
    check(edit->scrollY() > 50.f, "la rueda desplaza el texto", edit->scrollY());

    edit->setText("hola mundo");
    rig.update();
    rig.down(14.f, 16.f);
    rig.up(14.f, 16.f);
    rig.down(14.f, 16.f);
    rig.up(14.f, 16.f);
    check(edit->selectedText() == "hola", "doble clic selecciona la palabra", 0.f);
}

void iconGrid()
{
    Rig rig;
    auto* scroll = rig.ctx.root().add<ScrollContainer>(true, false);
    auto* grid   = scroll->add<IconGrid>();
    std::vector<IconItem> items;
    for (int i = 0; i < 20; ++i)
    {
        IconItem item;
        item.text   = "archivo" + std::to_string(i);
        item.color  = rgb(0x818cf8);
        item.badge  = "C#";
        item.folder = i == 0;
        items.push_back(item);
    }
    grid->setItems(items);
    rig.update();
    rig.update();

    check(grid->columns() == 4, "la rejilla reparte las columnas según el ancho", static_cast<float>(grid->columns()));
    check(near(grid->itemRect(5).x, grid->rect().x + 96.0) && grid->itemRect(5).y > grid->itemRect(0).y, "los elementos se colocan en filas", grid->itemRect(5).x);

    const Vec2 c2 = grid->itemRect(2).center();
    rig.click(c2.x, c2.y);
    check(grid->current() == 2 && grid->selection().size() == 1, "un clic selecciona un elemento");

    Modifiers ctrl;
    ctrl.ctrl = true;
    const Vec2 c3 = grid->itemRect(3).center();
    rig.down(c3.x, c3.y, ctrl);
    rig.up(c3.x, c3.y, ctrl);
    check(grid->selection().size() == 2, "Ctrl+clic añade a la selección");

    int activated = -1;
    int contextIndex = -2;
    grid->setOnActivated([&](int i) { activated = i; });
    grid->setOnContextMenu([&](int i, Vec2) { contextIndex = i; });
    const Vec2 c4 = grid->itemRect(4).center();
    rig.click(c4.x, c4.y);
    rig.click(c4.x, c4.y);
    check(activated == 4, "doble clic activa el elemento", static_cast<float>(activated));

    rig.ctx.mouseButton(MouseButton::Right, true, c2, {});
    rig.ctx.mouseButton(MouseButton::Right, false, c2, {});
    check(contextIndex == 2, "clic derecho pide el menú contextual del elemento");

    grid->select(0, false);
    rig.key(KeyCode::Down);
    check(grid->current() == 4, "la flecha abajo salta una fila entera", static_cast<float>(grid->current()));
    rig.key(KeyCode::Right);
    check(grid->current() == 5, "la flecha derecha pasa al siguiente");

    rig.key(KeyCode::End);
    rig.update();
    check(scroll->scroll().y > 0.f && grid->current() == 19, "al ir al final la rejilla se desplaza", scroll->scroll().y);

    grid->setListMode(true);
    rig.update();
    rig.update();
    check(grid->columns() == 1 && grid->itemRect(1).y > grid->itemRect(0).y && grid->itemRect(1).h < 40.f, "el modo lista pone un elemento por fila");
}

void boldFont()
{
    Rig rig;
    rig.ctx.theme().fontBold = createFixedFont(20.f);
    auto* box    = rig.ctx.root().add<VBox>();
    auto* normal = box->add<Label>(std::string("Titulo"));
    auto* bold   = box->add<Label>(std::string("Titulo"));
    bold->setBold(true);
    rig.update();
    check(bold->minSize().x > normal->minSize().x, "una etiqueta en negrita usa la fuente en negrita", bold->minSize().x);
}
}

int main()
{
    numberEdit();
    colorMath();
    colorPicker();
    colorButton();
    viewport();
    textEdit();
    iconGrid();
    boldFont();

    std::printf("\n%s: %d fallos\n", failures == 0 ? "TODO BIEN" : "HAY ERRORES", failures);
    return failures == 0 ? 0 : 1;
}
