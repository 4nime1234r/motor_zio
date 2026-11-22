#include "core/GameExport.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "zio/scripting/Language.h"
#include "zio/scripting/NativeScripts.h"

#ifndef ZIO_VERSION
#define ZIO_VERSION "0.0.0"
#endif

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace fs = std::filesystem;

namespace zio_editor
{

namespace
{

void fail(std::string* error, const std::string& message)
{
    if (error)
        *error = message;
}

std::string lowerCase(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string generic(const fs::path& relative)
{
    return relative.generic_string();
}

}

std::string sanitizeExecutableName(const std::string& text)
{
    std::string out;
    for (const unsigned char c : text)
    {
        if (std::isalnum(c) || c == '_' || c == '-')
            out.push_back(static_cast<char>(c));
        else if (c == ' ')
            out.push_back('_');
    }
    if (out.size() > 60)
        out.resize(60);
    if (out == "zio_editor")
        out += "_juego";
    return out;
}

fs::path signingKeyPath()
{
    const char* appData = std::getenv("APPDATA");
    if (!appData || !*appData)
        return {};
    return fs::path(appData) / "ZioEditor" / "firma.key";
}

fs::path editorExecutablePath()
{
    wchar_t buffer[MAX_PATH];
    const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    return length == 0 || length >= MAX_PATH ? fs::path() : fs::path(buffer);
}

bool exportGame(const ExportRequest& request, const std::function<void(const std::string&)>& log, ExportResult& result,
                std::string* error)
{
    using namespace zio::pack;
    namespace scripting = zio::scripting;

    const std::string exe = sanitizeExecutableName(request.executableName);
    if (exe.empty())
    {
        fail(error, "Escribe un nombre de archivo para el juego (letras, números, guion o guion bajo).");
        return false;
    }
    if (request.protection == Protection::Password && request.password.size() < 8)
    {
        fail(error, "La contraseña debe tener al menos 8 caracteres.");
        return false;
    }
    std::error_code ec;
    fs::create_directories(request.outputDir, ec);
    if (ec)
    {
        fail(error, "No se pudo crear la carpeta de salida: " + request.outputDir.string());
        return false;
    }
    const fs::path self = editorExecutablePath();
    if (self.empty())
    {
        fail(error, "No se pudo localizar el ejecutable del editor.");
        return false;
    }

    std::vector<FileEntry> files;
    files.push_back({request.projectRoot / "project.zio", "project.zio"});

    const fs::path assets  = request.projectRoot / "assets";
    const fs::path scripts = assets / "scripts";
    if (fs::is_directory(assets, ec))
    {
        for (fs::recursive_directory_iterator it(assets, ec), end; it != end && !ec; it.increment(ec))
        {
            if (!it->is_regular_file(ec))
                continue;
            const fs::path relative = fs::relative(it->path(), request.projectRoot, ec);
            if (ec)
                continue;
            const std::string name = lowerCase(it->path().filename().string());
            if (name.size() > 4 && name.compare(name.size() - 4, 4, ".tmp") == 0)
                continue;
            const bool inScripts = !fs::relative(it->path(), scripts, ec).string().starts_with("..");
            if (inScripts && !request.includeSources)
                continue;
            files.push_back({it->path(), generic(relative)});
        }
    }

    for (const auto& language : scripting::Languages::instance().all())
    {
        if (!language->hasSources(request.projectRoot))
            continue;
        if (language->id() == "cpp")
        {
            log("Compilando los scripts de C++ para el juego...");
            fs::path dll;
            std::string problem;
            if (!scripting::NativeScripts::buildScripts(request.projectRoot, log, &problem, exe + ".exe", &dll))
            {
                fail(error, problem);
                return false;
            }
            files.push_back({dll, ".zio/cpp/bin/Scripts.dll"});
            continue;
        }
        if (language->needsBuild(request.projectRoot))
        {
            log("Compilando los scripts de " + language->displayName() + "...");
            std::string problem;
            if (!language->build(request.projectRoot, log, &problem))
            {
                fail(error, problem);
                return false;
            }
        }
        const fs::path output = language->outputPath(request.projectRoot);
        if (!fs::exists(output, ec))
        {
            fail(error, "Faltan los scripts compilados de " + language->displayName() + ".");
            return false;
        }
        files.push_back({output, generic(fs::relative(output, request.projectRoot, ec))});
    }

    PackageInfo info;
    info.name          = request.info.title.empty() ? request.projectName : request.info.title;
    info.author        = request.info.author;
    info.version       = request.info.version;
    info.description   = request.info.description;
    info.copyright     = request.info.copyright;
    info.website       = request.info.website;
    info.license       = request.info.license;
    info.id            = request.info.id;
    info.engineVersion = ZIO_VERSION;
    info.entryScene    = request.info.entryScene;
    info.executable    = exe + ".exe";
    info.createdUnix   = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
                                                       std::chrono::system_clock::now().time_since_epoch())
                                                       .count());
    info.allowImport   = request.info.allowImport;

    WriteOptions options;
    options.protection = request.protection;
    options.password   = request.password;
    if (request.protection == Protection::KeyFile)
        options.key = generateKey();

    if (request.sign)
    {
        const fs::path keyPath = signingKeyPath();
        std::optional<Key32> seed;
        if (!keyPath.empty())
        {
            std::ifstream in(keyPath, std::ios::binary);
            std::stringstream text;
            text << in.rdbuf();
            seed = keyFromText(text.str());
        }
        if (!seed)
        {
            seed = generateKey();
            if (!keyPath.empty())
            {
                fs::create_directories(keyPath.parent_path(), ec);
                std::ofstream out(keyPath, std::ios::binary | std::ios::trunc);
                out << keyToText(*seed);
                log("Se creó tu clave de firma de autor en " + keyPath.string() + ". Guárdala: es la prueba de que los juegos son tuyos.");
            }
        }
        options.signer       = signingKeyFromSeed(*seed);
        result.fingerprint   = fingerprint(options.signer->publicKey);
        result.signedPackage = true;
    }

    log("Cifrando y empaquetando " + std::to_string(files.size()) + " archivos...");
    result.package = request.outputDir / (exe + ".zgame");
    if (!writePackage(files, result.package, info, options, error))
        return false;

    result.executable = request.outputDir / (exe + ".exe");
    fs::copy_file(self, result.executable, fs::copy_options::overwrite_existing, ec);
    if (ec)
    {
        fail(error, "No se pudo copiar el ejecutable del juego: " + ec.message());
        return false;
    }

    if (request.protection == Protection::KeyFile)
    {
        result.keyFile = request.outputDir / (exe + ".zkey");
        std::ofstream out(result.keyFile, std::ios::binary | std::ios::trunc);
        out << keyToText(options.key) << "\n";
        if (!out)
        {
            fail(error, "No se pudo escribir el archivo de clave.");
            return false;
        }
    }
    return true;
}

bool importGame(const ImportRequest& request, fs::path& projectRoot, zio::pack::PackageHeader& header, std::string* error)
{
    using namespace zio::pack;
    if (!readHeader(request.package, header, error))
        return false;
    if (!header.info.allowImport)
    {
        fail(error, "El autor no permitió abrir este juego en el editor.");
        return false;
    }
    if (header.signedPackage && !verifySignature(request.package, error))
        return false;

    std::string reason;
    if (!ProjectManager::checkNewProject(request.destinationParent, request.projectName, &reason))
    {
        fail(error, reason);
        return false;
    }
    const fs::path root = request.destinationParent / request.projectName;
    if (!extractPackage(request.package, root, request.credentials, &header, error))
        return false;

    std::error_code ec;
    if (!fs::exists(root / ProjectManager::kProjectFile, ec))
    {
        fs::remove_all(root, ec);
        fail(error, "El paquete no contiene un proyecto de Zio.");
        return false;
    }
    projectRoot = root;
    return true;
}

}
