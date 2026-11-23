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

namespace
{

void putWord(std::vector<std::uint8_t>& out, std::uint16_t v)
{
    out.push_back(static_cast<std::uint8_t>(v & 0xFF));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
}

void putDword(std::vector<std::uint8_t>& out, std::uint32_t v)
{
    putWord(out, static_cast<std::uint16_t>(v & 0xFFFF));
    putWord(out, static_cast<std::uint16_t>(v >> 16));
}

void align4(std::vector<std::uint8_t>& out)
{
    while (out.size() % 4)
        out.push_back(0);
}

void putWide(std::vector<std::uint8_t>& out, const std::wstring& text)
{
    for (const wchar_t c : text)
        putWord(out, static_cast<std::uint16_t>(c));
    putWord(out, 0);
}

std::wstring toWide(const std::string& text)
{
    if (text.empty())
        return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), size);
    return wide;
}

std::size_t beginBlock(std::vector<std::uint8_t>& out, std::uint16_t valueLength, std::uint16_t type, const std::wstring& key)
{
    align4(out);
    const std::size_t start = out.size();
    putWord(out, 0);
    putWord(out, valueLength);
    putWord(out, type);
    putWide(out, key);
    align4(out);
    return start;
}

void endBlock(std::vector<std::uint8_t>& out, std::size_t start)
{
    const std::uint16_t length = static_cast<std::uint16_t>(out.size() - start);
    out[start]                 = static_cast<std::uint8_t>(length & 0xFF);
    out[start + 1]             = static_cast<std::uint8_t>(length >> 8);
}

std::vector<std::uint8_t> buildVersionResource(const GameInfo& info, const std::string& fileName)
{
    unsigned parts[4] = {1, 0, 0, 0};
    {
        std::size_t index = 0;
        std::size_t pos   = 0;
        while (index < 4 && pos < info.version.size())
        {
            std::size_t used = 0;
            try
            {
                parts[index++] = static_cast<unsigned>(std::stoul(info.version.substr(pos), &used));
            }
            catch (...)
            {
                break;
            }
            pos += used;
            if (pos < info.version.size() && info.version[pos] == '.')
                ++pos;
            else
                break;
        }
    }

    std::vector<std::uint8_t> out;
    const std::size_t root = beginBlock(out, 52, 0, L"VS_VERSION_INFO");
    putDword(out, 0xFEEF04BD);
    putDword(out, 0x00010000);
    putDword(out, (parts[0] << 16) | (parts[1] & 0xFFFF));
    putDword(out, (parts[2] << 16) | (parts[3] & 0xFFFF));
    putDword(out, (parts[0] << 16) | (parts[1] & 0xFFFF));
    putDword(out, (parts[2] << 16) | (parts[3] & 0xFFFF));
    putDword(out, 0x3F);
    putDword(out, 0);
    putDword(out, 0x00040004);
    putDword(out, 1);
    putDword(out, 0);
    putDword(out, 0);
    putDword(out, 0);

    const std::size_t stringInfo = beginBlock(out, 0, 1, L"StringFileInfo");
    const std::size_t table      = beginBlock(out, 0, 1, L"040904B0");
    const std::string title      = info.title.empty() ? fileName : info.title;
    const std::pair<const wchar_t*, std::string> entries[] = {
        {L"CompanyName", info.author},
        {L"FileDescription", info.description.empty() ? title : info.description},
        {L"FileVersion", info.version},
        {L"InternalName", fileName},
        {L"LegalCopyright", info.copyright},
        {L"OriginalFilename", fileName + ".exe"},
        {L"ProductName", title},
        {L"ProductVersion", info.version},
    };
    for (const auto& [key, value] : entries)
    {
        const std::wstring wide = toWide(value);
        const std::size_t item  = beginBlock(out, static_cast<std::uint16_t>(wide.size() + 1), 1, key);
        putWide(out, wide);
        endBlock(out, item);
    }
    endBlock(out, table);
    endBlock(out, stringInfo);

    const std::size_t varInfo = beginBlock(out, 0, 1, L"VarFileInfo");
    const std::size_t var     = beginBlock(out, 4, 0, L"Translation");
    putWord(out, 0x0409);
    putWord(out, 1200);
    endBlock(out, var);
    endBlock(out, varInfo);
    endBlock(out, root);
    return out;
}

WORD existingLanguage(HMODULE module, LPCWSTR type, LPCWSTR name)
{
    WORD language = 0;
    EnumResourceLanguagesW(
        module, type, name,
        [](HMODULE, LPCWSTR, LPCWSTR, WORD lang, LONG_PTR param) -> BOOL {
            *reinterpret_cast<WORD*>(param) = lang;
            return FALSE;
        },
        reinterpret_cast<LONG_PTR>(&language));
    return language;
}

}

bool applyExecutableResources(const fs::path& executable, const fs::path& icon, const GameInfo& info, const std::string& fileName,
                              std::string* error)
{
    WORD language = 0;
    int oldIcons  = 0;
    if (HMODULE module = LoadLibraryExW(executable.wstring().c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE))
    {
        language = existingLanguage(module, MAKEINTRESOURCEW(14), MAKEINTRESOURCEW(1));
        if (HRSRC group = FindResourceExW(module, MAKEINTRESOURCEW(14), MAKEINTRESOURCEW(1), language))
            if (HGLOBAL loaded = LoadResource(module, group))
                if (const auto* data = static_cast<const std::uint8_t*>(LockResource(loaded)))
                    oldIcons = static_cast<int>(data[4]) | (static_cast<int>(data[5]) << 8);
        FreeLibrary(module);
    }

    std::vector<std::uint8_t> ico;
    if (!icon.empty())
    {
        std::ifstream in(icon, std::ios::binary);
        if (!in)
        {
            fail(error, "No se puede leer el icono " + icon.string());
            return false;
        }
        ico.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        if (ico.size() < 6 || ico[0] != 0 || ico[1] != 0 || ico[2] != 1 || ico[3] != 0)
        {
            fail(error, "El icono debe ser un archivo .ico válido.");
            return false;
        }
    }

    HANDLE update = BeginUpdateResourceW(executable.wstring().c_str(), FALSE);
    if (!update)
    {
        fail(error, "No se pudo abrir el ejecutable para cambiar su icono y datos.");
        return false;
    }
    bool ok = true;
    if (!ico.empty())
    {
        const int count = ico[4] | (ico[5] << 8);
        if (ico.size() < 6 + static_cast<std::size_t>(count) * 16 || count == 0)
        {
            EndUpdateResourceW(update, TRUE);
            fail(error, "El icono está dañado.");
            return false;
        }
        for (int i = 1; i <= oldIcons; ++i)
            UpdateResourceW(update, MAKEINTRESOURCEW(3), MAKEINTRESOURCEW(i), language, nullptr, 0);

        std::vector<std::uint8_t> group;
        putWord(group, 0);
        putWord(group, 1);
        putWord(group, static_cast<std::uint16_t>(count));
        for (int i = 0; i < count && ok; ++i)
        {
            const std::uint8_t* entry = ico.data() + 6 + static_cast<std::size_t>(i) * 16;
            const std::uint32_t size   = entry[8] | (entry[9] << 8) | (entry[10] << 16) | (static_cast<std::uint32_t>(entry[11]) << 24);
            const std::uint32_t offset = entry[12] | (entry[13] << 8) | (entry[14] << 16) | (static_cast<std::uint32_t>(entry[15]) << 24);
            if (static_cast<std::size_t>(offset) + size > ico.size())
            {
                ok = false;
                break;
            }
            ok = UpdateResourceW(update, MAKEINTRESOURCEW(3), MAKEINTRESOURCEW(i + 1), language,
                                 const_cast<std::uint8_t*>(ico.data()) + offset, size) != 0;
            group.insert(group.end(), entry, entry + 12);
            putWord(group, static_cast<std::uint16_t>(i + 1));
        }
        ok = ok && UpdateResourceW(update, MAKEINTRESOURCEW(14), MAKEINTRESOURCEW(1), language, group.data(),
                                   static_cast<DWORD>(group.size())) != 0;
    }
    std::vector<std::uint8_t> version = buildVersionResource(info, fileName);
    ok = ok && UpdateResourceW(update, MAKEINTRESOURCEW(16), MAKEINTRESOURCEW(1), 0x0409, version.data(),
                               static_cast<DWORD>(version.size())) != 0;
    if (!EndUpdateResourceW(update, ok ? FALSE : TRUE) || !ok)
    {
        fail(error, "No se pudo escribir el icono y los datos en el ejecutable.");
        return false;
    }
    return true;
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

    {
        fs::path iconFile;
        if (!request.info.icon.empty())
            iconFile = request.projectRoot / fs::u8path(request.info.icon);
        std::string resourceError;
        if (!applyExecutableResources(result.executable, iconFile, request.info, exe, &resourceError))
            log("Aviso: " + resourceError);
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
