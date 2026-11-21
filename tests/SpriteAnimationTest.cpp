#include <cstdio>
#include <memory>

#include "zio/scene2d/Node.h"
#include "zio/scene2d/SceneIO.h"
#include "zio/scene2d/Sprite.h"

using namespace zio::scene2d;

namespace
{

int failures = 0;

void check(bool condition, const char* name)
{
    std::printf("%s %s\n", condition ? "[ok]  " : "[FALLO]", name);
    if (!condition)
        ++failures;
}

}

int main()
{
    Sprite sprite;
    sprite.setSheet(4, 2);
    check(sprite.frameCount() == 8, "la hoja tiene 8 cuadros");
    sprite.setFrame(99);
    check(sprite.frame() == 7, "el cuadro se limita al rango");
    sprite.setFrame(0);

    sprite.addAnimation({"correr", 2, 5, 10.f, true});
    sprite.addAnimation({"golpe", 4, 6, 10.f, false});
    check(!sprite.play("no_existe"), "una animacion inexistente no se reproduce");
    check(sprite.play("correr") && sprite.isPlaying(), "reproduce correr");
    check(sprite.frame() == 2, "empieza en el primer cuadro");

    sprite.update(0.11f);
    check(sprite.frame() == 3, "avanza un cuadro a 10 fps");
    sprite.update(0.3f);
    for (int i = 0; i < 100; ++i)
    {
        sprite.update(0.037f);
        if (sprite.frame() < 2 || sprite.frame() > 5)
        {
            check(false, "el bucle se sale del rango");
            break;
        }
    }
    check(sprite.isPlaying(), "el bucle sigue reproduciendo");

    check(sprite.play("golpe") && sprite.frame() == 4, "cambia a golpe");
    sprite.update(0.15f);
    check(sprite.frame() == 5, "golpe avanza");
    sprite.update(1.f);
    check(sprite.frame() == 6 && !sprite.isPlaying(), "sin bucle termina en el ultimo cuadro y se detiene");

    sprite.play("correr");
    sprite.setAnimationSpeed(2.f);
    sprite.update(0.06f);
    check(sprite.frame() == 3, "la velocidad doble acelera la animacion");
    sprite.stop();
    const int frozen = sprite.frame();
    sprite.update(1.f);
    check(sprite.frame() == frozen, "detenida no avanza");

    Sprite saved;
    saved.setName("heroe");
    saved.setSheet(4, 2);
    saved.setFrame(3);
    saved.addAnimation({"correr", 2, 5, 12.f, true});
    saved.addAnimation({"golpe", 4, 6, 8.f, false});
    saved.play("correr");

    Node root;
    root.addChild(std::make_unique<Sprite>());
    auto* first = static_cast<Sprite*>(root.children()[0].get());
    first->setName("heroe");
    first->setSheet(4, 2);
    first->setFrame(3);
    first->addAnimation({"correr", 2, 5, 12.f, true});
    first->addAnimation({"golpe", 4, 6, 8.f, false});
    first->play("correr");

    const std::string text = serializeScene(root);
    Node loaded;
    std::string error;
    check(deserializeScene(loaded, text, &error), "la escena con animaciones se vuelve a leer");
    check(loaded.children().size() == 1, "un hijo tras leer");
    if (loaded.children().size() == 1)
    {
        const auto* back = dynamic_cast<const Sprite*>(loaded.children()[0].get());
        check(back != nullptr, "el hijo es un Sprite");
        if (back)
        {
            check(back->sheetColumns() == 4 && back->sheetRows() == 2, "se conserva la hoja");
            check(back->animations().size() == 2, "se conservan las animaciones");
            check(back->animations().size() == 2 && back->animations()[1].name == "golpe" &&
                      back->animations()[1].lastFrame == 6 && !back->animations()[1].loop &&
                      back->animations()[0].fps == 12.f,
                  "se conservan los datos de cada animacion");
            check(back->isPlaying() && back->currentAnimation() == "correr", "se conserva la animacion en curso");
        }
    }

    Node broken;
    check(!deserializeScene(broken,
                            R"({"type":"Node","children":[{"type":"Sprite","animations":[5]}]})", &error),
          "una animacion mal formada da error");

    std::printf("%s\n", failures == 0 ? "TODO OK" : "HAY FALLOS");
    return failures == 0 ? 0 : 1;
}
