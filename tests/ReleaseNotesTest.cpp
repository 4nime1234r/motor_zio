#include <cstdio>
#include <string>

#include "update/ReleaseNotes.h"

using namespace zio_editor;

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
    check(releaseBodyFromJson(R"({"tag_name":"v1","body":"Hola\nmundo"})") == "Hola\nmundo", "lee el cuerpo de la release");
    check(releaseBodyFromJson(R"({"tag_name":"v1"})").empty(), "sin cuerpo da vacio");
    check(releaseBodyFromJson("no es json").empty(), "json roto da vacio");

    const std::string md = "## Novedades\n\n- **Animación** de huesos\n* Otra `cosa` con [enlace](http://x.y)\n\n\n"
                           "Texto normal que es bastante largo y debe partirse en varias lineas para que quepa";
    const auto lines = formatReleaseNotes(md, 30);
    check(!lines.empty() && lines[0].heading && lines[0].text == "Novedades", "el titulo se marca como encabezado");
    check(lines.size() > 3, "se generan varias lineas");

    bool bullets = false, markup = false, longLine = false, doubleBlank = false;
    for (std::size_t i = 0; i < lines.size(); ++i)
    {
        if (lines[i].text.rfind("\xE2\x80\xA2 Animaci", 0) == 0)
            bullets = true;
        if (lines[i].text.find("**") != std::string::npos || lines[i].text.find('`') != std::string::npos ||
            lines[i].text.find("](") != std::string::npos)
            markup = true;
        if (lines[i].text.size() > 40)
            longLine = true;
        if (i > 0 && lines[i].text.empty() && lines[i - 1].text.empty())
            doubleBlank = true;
    }
    check(bullets, "las vinetas se convierten en puntos");
    check(!markup, "se quita el formato de markdown");
    check(!longLine, "las lineas se parten al ancho pedido");
    check(!doubleBlank, "no quedan lineas vacias seguidas");

    bool link = false;
    for (const NoteLine& l : lines)
        if (l.text.find("enlace") != std::string::npos && l.text.find("http") == std::string::npos)
            link = true;
    check(link, "los enlaces dejan solo el texto");

    check(formatReleaseNotes("", 40).empty(), "un texto vacio no genera lineas");
    std::string huge;
    for (int i = 0; i < 1000; ++i)
        huge += "- linea " + std::to_string(i) + "\n";
    check(formatReleaseNotes(huge, 40).size() <= 200, "se limita el numero de lineas");

    std::printf("%s\n", failures == 0 ? "TODO OK" : "HAY FALLOS");
    return failures == 0 ? 0 : 1;
}
