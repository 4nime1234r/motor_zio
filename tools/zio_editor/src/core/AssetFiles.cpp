#include "core/AssetFiles.h"

#include <algorithm>
#include <cctype>
#include <fstream>

#include "zio/scene3d/Scene3D.h"
#include "zio/scene3d/SceneIO3D.h"

namespace fs = std::filesystem;

namespace zio_editor
{

namespace
{

std::string lower(std::string_view text)
{
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

std::string extensionOf(std::string_view name)
{
    const auto dot = name.find_last_of('.');
    return dot == std::string_view::npos ? std::string() : lower(name.substr(dot + 1));
}

bool oneOf(const std::string& value, std::initializer_list<const char*> list)
{
    for (const char* item : list)
        if (value == item)
            return true;
    return false;
}

}

std::vector<AssetEntry> AssetFiles::scan(const fs::path& projectRoot)
{
    std::vector<AssetEntry> out;
    const fs::path assets = projectRoot / "assets";
    std::error_code ec;
    if (!fs::is_directory(assets, ec))
        return out;

    fs::recursive_directory_iterator it(assets, fs::directory_options::skip_permission_denied, ec);
    for (const fs::recursive_directory_iterator end; !ec && it != end && out.size() < kMaxEntries; it.increment(ec))
    {
        std::error_code entryEc;
        if (!it->is_regular_file(entryEc))
            continue;
        const std::string name = it->path().filename().string();
        if (name.empty() || name.front() == '.')
            continue;
        AssetEntry entry;
        entry.name      = name;
        entry.extension = extensionOf(name);
        entry.relative  = "assets/" + fs::relative(it->path(), assets, entryEc).generic_string();
        out.push_back(std::move(entry));
    }
    std::sort(out.begin(), out.end(),
              [](const AssetEntry& a, const AssetEntry& b) { return lower(a.relative) < lower(b.relative); });
    return out;
}

std::string AssetFiles::folderFor(std::string_view fileName)
{
    const std::string ext = extensionOf(fileName);
    if (ext == "zscene3d")
        return "assets/scenes";
    if (oneOf(ext, {"obj", "gltf", "glb", "bin"}))
        return "assets/models";
    if (oneOf(ext, {"png", "jpg", "jpeg", "bmp", "tga"}))
        return "assets/textures";
    if (oneOf(ext, {"wav", "ogg", "mp3", "flac"}))
        return "assets/audio";
    if (oneOf(ext, {"cs", "cpp", "cc", "c", "h", "hpp", "lua"}))
        return "assets/scripts";
    return "assets";
}

bool AssetFiles::isValidName(std::string_view name, std::string* reason)
{
    const auto fail = [&](const char* message) {
        if (reason)
            *reason = message;
        return false;
    };
    if (name.empty())
        return fail("Escribe un nombre.");
    if (name.size() > 120)
        return fail("El nombre es demasiado largo.");
    for (const unsigned char c : name)
        if (c < 32 || std::string_view("<>:\"/\\|?*").find(static_cast<char>(c)) != std::string_view::npos)
            return fail("El nombre tiene caracteres no permitidos.");
    if (name.front() == '.' || name.back() == '.' || name.back() == ' ')
        return fail("El nombre no puede empezar con punto ni terminar con punto o espacio.");
    if (name.find_last_of('.') == std::string_view::npos)
        return fail("Escribe el nombre con su extensión, por ejemplo jugador.cs.");
    return true;
}

bool AssetFiles::create(const ProjectManager& project, std::string_view name, std::string* relativeOut,
                        std::string* error)
{
    const auto fail = [&](const std::string& message) {
        if (error)
            *error = message;
        return false;
    };
    if (!project.isOpen())
        return fail("No hay un proyecto abierto.");
    std::string reason;
    if (!isValidName(name, &reason))
        return fail(reason);

    const std::string relative = folderFor(name) + "/" + std::string(name);
    const auto absolute        = project.resolve(relative);
    if (!absolute)
        return fail("Ruta inválida.");

    std::error_code ec;
    if (fs::exists(*absolute, ec))
        return fail("Ya existe un archivo con ese nombre.");
    fs::create_directories(absolute->parent_path(), ec);
    if (ec)
        return fail("No se pudo crear la carpeta de destino.");

    std::ofstream out(*absolute, std::ios::binary);
    if (!out)
        return fail("No se pudo crear el archivo.");
    if (extensionOf(name) == "zscene3d")
    {
        zio::scene3d::Scene3D blank;
        out << zio::scene3d::serializeScene3D(blank);
    }
    out.close();
    if (!out)
        return fail("No se pudo escribir el archivo.");

    if (relativeOut)
        *relativeOut = relative;
    return true;
}

bool AssetFiles::remove(const ProjectManager& project, std::string_view relative, std::string* error)
{
    const auto fail = [&](const std::string& message) {
        if (error)
            *error = message;
        return false;
    };
    if (!project.isOpen())
        return fail("No hay un proyecto abierto.");
    if (relative.rfind("assets/", 0) != 0)
        return fail("Solo se pueden eliminar archivos de assets.");
    const auto absolute = project.resolve(relative);
    if (!absolute)
        return fail("Ruta inválida.");
    std::error_code ec;
    const fs::path inside = fs::weakly_canonical(*absolute, ec).lexically_relative(fs::weakly_canonical(project.assetsDir(), ec));
    if (ec || inside.empty() || *inside.begin() == "..")
        return fail("Solo se pueden eliminar archivos de assets.");
    if (!fs::is_regular_file(*absolute, ec))
        return fail("El archivo ya no existe.");
    if (!fs::remove(*absolute, ec) || ec)
        return fail("No se pudo eliminar el archivo.");
    return true;
}

}
