#include "project/ProjectManager.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <system_error>

#include "internal/Json.h"

namespace fs = std::filesystem;

namespace zio_editor
{

namespace
{

constexpr const char* kRequiredDirs[] = {"assets",          "assets/models", "assets/textures",
                                          "assets/scenes",   "assets/audio",  "assets/scripts"};
constexpr std::uintmax_t kMaxProjectFileBytes = 1024 * 1024;
constexpr std::size_t kMaxNameLength          = 64;

void setError(std::string* error, std::string message)
{
    if (error)
        *error = std::move(message);
}

std::string toUpper(std::string_view s)
{
    std::string out(s);
    for (char& c : out)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

bool isReservedName(std::string_view name)
{
    const std::string stem = toUpper(name.substr(0, name.find('.')));
    static const char* kReserved[] = {"CON",  "PRN",  "AUX",  "NUL",  "COM1", "COM2", "COM3", "COM4", "COM5",
                                      "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5",
                                      "LPT6", "LPT7", "LPT8", "LPT9"};
    return std::any_of(std::begin(kReserved), std::end(kReserved), [&](const char* r) { return stem == r; });
}

bool isInside(const fs::path& canonicalRoot, const fs::path& path)
{
    std::error_code ec;
    const fs::path canonical = fs::weakly_canonical(path, ec);
    if (ec)
        return false;
    const fs::path relative = canonical.lexically_relative(canonicalRoot);
    if (relative.empty())
        return false;
    return *relative.begin() != "..";
}

bool readSmallFile(const fs::path& file, std::string& out, std::string* error)
{
    std::error_code ec;
    const std::uintmax_t size = fs::file_size(file, ec);
    if (ec)
    {
        setError(error, "No se puede leer " + file.string());
        return false;
    }
    if (size > kMaxProjectFileBytes)
    {
        setError(error, "project.zio es demasiado grande");
        return false;
    }
    std::ifstream in(file, std::ios::binary);
    if (!in)
    {
        setError(error, "No se puede abrir " + file.string());
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

bool ensureProjectDirs(const fs::path& root, std::string* error)
{
    for (const char* rel : kRequiredDirs)
    {
        const fs::path dir = root / rel;
        std::error_code ec;
        if (fs::exists(dir, ec))
        {
            if (!fs::is_directory(dir, ec))
            {
                setError(error, "\"" + std::string(rel) + "\" existe pero no es una carpeta");
                return false;
            }
            continue;
        }
        fs::create_directories(dir, ec);
        if (ec)
        {
            setError(error, "No se pudo crear la carpeta " + dir.string() + ": " + ec.message());
            return false;
        }
    }
    return true;
}

std::string projectFileText(std::string_view name, const GameInfo& game = {})
{
    std::string out = "{\n  \"format\": ";
    zio::json::writeString(out, ProjectManager::kFormat);
    out += ",\n  \"version\": 1,\n  \"name\": ";
    zio::json::writeString(out, name);
    const bool hasGame = !game.title.empty() || !game.author.empty() || !game.description.empty() || !game.copyright.empty() ||
                         !game.website.empty() || !game.license.empty() || !game.id.empty() || !game.entryScene.empty() || !game.icon.empty() ||
                         game.allowImport || game.version != "1.0.0";
    if (hasGame)
    {
        out += ",\n  \"game\": {";
        bool first      = true;
        const auto text = [&](const char* key, const std::string& value) {
            out += first ? "\n    \"" : ",\n    \"";
            first = false;
            out += key;
            out += "\": ";
            zio::json::writeString(out, value);
        };
        text("title", game.title);
        text("author", game.author);
        text("version", game.version);
        text("description", game.description);
        text("copyright", game.copyright);
        text("website", game.website);
        text("license", game.license);
        text("id", game.id);
        text("entryScene", game.entryScene);
        text("icon", game.icon);
        out += ",\n    \"allowImport\": ";
        out += game.allowImport ? "true" : "false";
        out += "\n  }";
    }
    out += "\n}\n";
    return out;
}

}

bool ProjectManager::isValidProjectName(std::string_view name, std::string* reason)
{
    auto fail = [&](const char* why) {
        setError(reason, why);
        return false;
    };
    if (name.empty())
        return fail("el nombre está vacío");
    if (name.size() > kMaxNameLength)
        return fail("el nombre es demasiado largo (maximo 64 caracteres)");
    for (const char c : name)
    {
        const unsigned char u = static_cast<unsigned char>(c);
        if (!(std::isalnum(u) || c == ' ' || c == '_' || c == '-' || c == '.'))
            return fail("solo se permiten letras, números, espacio, '_', '-' y '.'");
    }
    if (name.front() == ' ' || name.back() == ' ')
        return fail("no puede empezar ni terminar con espacio");
    if (name.front() == '.' || name.back() == '.')
        return fail("no puede empezar ni terminar con punto");
    if (isReservedName(name))
        return fail("es un nombre reservado de Windows");
    return true;
}

bool ProjectManager::isSceneFile(const fs::path& path)
{
    return toUpper(path.extension().string()) == toUpper(kSceneExtension);
}

bool ProjectManager::isMeshFile(const fs::path& path)
{
    const std::string ext = toUpper(path.extension().string());
    return ext == ".OBJ" || ext == ".GLTF" || ext == ".GLB";
}

std::vector<std::string> ProjectManager::listFiles(std::string_view relativeDir, std::string_view extension) const
{
    constexpr std::size_t kMaxFiles = 2000;

    std::vector<std::string> out;
    const auto dir = resolve(relativeDir);
    std::error_code ec;
    if (!dir || !fs::is_directory(*dir, ec))
        return out;

    const std::string wanted = toUpper(extension);
    fs::recursive_directory_iterator it(*dir, fs::directory_options::skip_permission_denied, ec);
    for (const fs::recursive_directory_iterator end; !ec && it != end && out.size() < kMaxFiles; it.increment(ec))
    {
        std::error_code entryEc;
        if (!it->is_regular_file(entryEc) || toUpper(it->path().extension().string()) != wanted)
            continue;
        if (const auto relative = toRelative(it->path()))
            out.push_back(*relative);
    }

    std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) { return toUpper(a) < toUpper(b); });
    return out;
}

bool ProjectManager::checkNewProject(const fs::path& parentDir, std::string_view name, std::string* error)
{
    std::string reason;
    if (!isValidProjectName(name, &reason))
    {
        setError(error, "Nombre de proyecto no válido: " + reason);
        return false;
    }

    std::error_code ec;
    if (!fs::is_directory(parentDir, ec))
    {
        setError(error, "La carpeta padre no existe: " + parentDir.string());
        return false;
    }

    const fs::path root = parentDir / std::string(name);
    if (!fs::exists(root, ec))
        return true;
    if (!fs::is_directory(root, ec))
    {
        setError(error, "Ya existe un archivo con ese nombre: " + root.string());
        return false;
    }
    if (fs::exists(root / kProjectFile, ec))
    {
        setError(error, "Ya hay un proyecto en " + root.string());
        return false;
    }
    if (!fs::is_empty(root, ec))
    {
        setError(error, "La carpeta ya existe y no está vacía: " + root.string());
        return false;
    }
    return true;
}

bool ProjectManager::createProject(const fs::path& parentDir, std::string_view name, std::string* error)
{
    if (!checkNewProject(parentDir, name, error))
        return false;

    std::error_code ec;
    const fs::path root = parentDir / std::string(name);
    bool createdRoot    = false;
    if (!fs::exists(root, ec))
    {
        fs::create_directory(root, ec);
        if (ec)
        {
            setError(error, "No se pudo crear " + root.string() + ": " + ec.message());
            return false;
        }
        createdRoot = true;
    }

    auto undo = [&]() {
        std::error_code ignore;
        if (createdRoot)
        {
            fs::remove_all(root, ignore);
            return;
        }
        fs::remove_all(root / "assets", ignore);
        fs::remove(root / kProjectFile, ignore);
    };

    if (!ensureProjectDirs(root, error))
    {
        undo();
        return false;
    }

    {
        std::ofstream out(root / kProjectFile, std::ios::binary | std::ios::trunc);
        const std::string text = projectFileText(name);
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!out)
        {
            setError(error, "No se pudo escribir " + (root / kProjectFile).string());
            out.close();
            undo();
            return false;
        }
    }

    std::string loadError;
    if (!loadProject(root, &loadError))
    {
        setError(error, loadError);
        undo();
        return false;
    }
    return true;
}

bool ProjectManager::loadProject(const fs::path& path, std::string* error)
{
    std::error_code ec;
    fs::path file = path;
    if (fs::is_directory(path, ec))
        file = path / kProjectFile;

    if (file.filename() != kProjectFile)
    {
        setError(error, "Se esperaba un archivo project.zio o la carpeta que lo contiene");
        return false;
    }
    if (!fs::is_regular_file(file, ec))
    {
        setError(error, "No se encontró " + file.string());
        return false;
    }

    std::string text;
    if (!readSmallFile(file, text, error))
        return false;

    zio::json::Value doc;
    std::string parseError;
    if (!zio::json::Parser(text).parse(doc, parseError))
    {
        setError(error, "project.zio: " + parseError);
        return false;
    }
    if (!std::holds_alternative<zio::json::Object>(doc.v))
    {
        setError(error, "project.zio debe ser un objeto JSON");
        return false;
    }
    const zio::json::Value* format = doc.find("format");
    const auto* formatText         = format ? std::get_if<std::string>(&format->v) : nullptr;
    if (!formatText || *formatText != kFormat)
    {
        setError(error, "project.zio no es un proyecto de Zio (falta o es incorrecto \"format\")");
        return false;
    }
    const zio::json::Value* version = doc.find("version");
    const auto* versionNumber       = version ? std::get_if<double>(&version->v) : nullptr;
    if (!versionNumber || *versionNumber != 1.0)
    {
        setError(error, "project.zio: versión no soportada (este editor lee la versión 1)");
        return false;
    }
    const zio::json::Value* nameValue = doc.find("name");
    const auto* nameText              = nameValue ? std::get_if<std::string>(&nameValue->v) : nullptr;
    if (!nameText || nameText->empty())
    {
        setError(error, "project.zio: falta \"name\"");
        return false;
    }

    const fs::path root = fs::canonical(file.parent_path(), ec);
    if (ec)
    {
        setError(error, "No se pudo resolver la carpeta del proyecto: " + ec.message());
        return false;
    }
    if (!ensureProjectDirs(root, error))
        return false;

    GameInfo game;
    if (const zio::json::Value* gameValue = doc.find("game"))
    {
        const auto read = [&](const char* key, std::string& target) {
            if (const zio::json::Value* field = gameValue->find(key))
                if (const auto* text = std::get_if<std::string>(&field->v))
                    target = text->substr(0, 4096);
        };
        read("title", game.title);
        read("author", game.author);
        read("version", game.version);
        read("description", game.description);
        read("copyright", game.copyright);
        read("website", game.website);
        read("license", game.license);
        read("id", game.id);
        read("entryScene", game.entryScene);
        read("icon", game.icon);
        if (const zio::json::Value* field = gameValue->find("allowImport"))
            if (const auto* flag = std::get_if<bool>(&field->v))
                game.allowImport = *flag;
    }

    root_ = root;
    name_ = *nameText;
    game_ = std::move(game);
    open_ = true;
    return true;
}

bool ProjectManager::saveGameInfo(const GameInfo& info, std::string* error)
{
    if (!open_)
    {
        setError(error, "No hay un proyecto abierto.");
        return false;
    }
    const fs::path file = root_ / kProjectFile;
    const fs::path temp = file.string() + ".tmp";
    const std::string text = projectFileText(name_, info);
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!out)
        {
            setError(error, "No se pudo escribir " + temp.string());
            return false;
        }
    }
    std::error_code ec;
    fs::remove(file, ec);
    fs::rename(temp, file, ec);
    if (ec)
    {
        setError(error, "No se pudo guardar " + file.string());
        return false;
    }
    game_ = info;
    return true;
}

void ProjectManager::close()
{
    open_ = false;
    name_.clear();
    root_.clear();
    game_ = GameInfo{};
}

std::optional<fs::path> ProjectManager::resolve(std::string_view relative) const
{
    if (!open_ || relative.empty() || relative.find('\0') != std::string_view::npos)
        return std::nullopt;

    const fs::path rel(relative);
    if (rel.has_root_name() || rel.has_root_directory())
        return std::nullopt;

    const fs::path normal = rel.lexically_normal();
    if (normal.empty() || *normal.begin() == "..")
        return std::nullopt;

    const fs::path absolute = (root_ / normal).lexically_normal();
    if (!isInside(root_, absolute))
        return std::nullopt;
    return absolute;
}

std::optional<std::string> ProjectManager::toRelative(const fs::path& path) const
{
    if (!open_ || path.empty())
        return std::nullopt;

    std::error_code ec;
    const fs::path absolute = path.is_absolute() ? path : fs::absolute(path, ec);
    if (ec)
        return std::nullopt;
    if (!isInside(root_, absolute))
        return std::nullopt;

    const fs::path canonical = fs::weakly_canonical(absolute, ec);
    if (ec)
        return std::nullopt;
    return canonical.lexically_relative(root_).generic_string();
}

}
