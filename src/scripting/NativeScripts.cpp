#include "zio/scripting/NativeScripts.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>

#include "scripting/SdkData.h"
#include "zio/scripting/Script.h"

namespace fs = std::filesystem;

#if defined(ZIO_PLATFORM_WINDOWS)

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

namespace zio::scripting
{

namespace
{

std::mutex gSdkMutex;

std::wstring quoted(const fs::path& path)
{
    return L"\"" + path.wstring() + L"\"";
}

std::string slashes(const fs::path& path)
{
    return path.generic_string();
}

bool runHidden(std::wstring command, const fs::path& workDir, const std::function<void(const std::string&)>& onLine,
               DWORD* exitCode)
{
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE readPipe  = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &security, 0))
        return false;
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW startup{};
    startup.cb          = sizeof(startup);
    startup.dwFlags     = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdOutput  = writePipe;
    startup.hStdError   = writePipe;
    PROCESS_INFORMATION process{};
    const BOOL created = CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                                        workDir.empty() ? nullptr : workDir.c_str(), &startup, &process);
    CloseHandle(writePipe);
    if (!created)
    {
        CloseHandle(readPipe);
        return false;
    }

    std::string pending;
    char buffer[4096];
    DWORD read = 0;
    while (ReadFile(readPipe, buffer, sizeof(buffer), &read, nullptr) && read > 0)
    {
        pending.append(buffer, read);
        std::size_t newline;
        while ((newline = pending.find('\n')) != std::string::npos)
        {
            std::string line = pending.substr(0, newline);
            pending.erase(0, newline + 1);
            while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
                line.pop_back();
            if (!line.empty() && onLine)
                onLine(line);
        }
    }
    if (!pending.empty() && onLine)
        onLine(pending);
    CloseHandle(readPipe);

    WaitForSingleObject(process.hProcess, INFINITE);
    *exitCode = 1;
    GetExitCodeProcess(process.hProcess, exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

fs::path appDataDir()
{
    const char* appData = std::getenv("APPDATA");
    return (appData && *appData ? fs::path(appData) : fs::temp_directory_path()) / "ZioEditor";
}

fs::path exePath()
{
    wchar_t buffer[MAX_PATH];
    const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    return length == 0 || length >= MAX_PATH ? fs::path() : fs::path(buffer);
}

std::string sdkTag()
{
    std::error_code ec;
    const fs::path exe = exePath();
    std::uint64_t hash = 1469598103934665603ull;
    const auto mix     = [&](std::uint64_t value) { hash = (hash ^ value) * 1099511628211ull; };
    mix(static_cast<std::uint64_t>(fs::file_size(exe, ec)));
    mix(static_cast<std::uint64_t>(fs::last_write_time(exe, ec).time_since_epoch().count()));
    for (const wchar_t c : exe.wstring())
        mix(static_cast<std::uint64_t>(c));
    char text[32];
    std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(hash));
    return text;
}

bool writeExportsDef(const fs::path& target)
{
    const auto* base = reinterpret_cast<const unsigned char*>(GetModuleHandleW(nullptr));
    const auto* dos  = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt   = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    const IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (dir.VirtualAddress == 0)
        return false;
    const auto* exports = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(base + dir.VirtualAddress);
    const auto* names    = reinterpret_cast<const DWORD*>(base + exports->AddressOfNames);
    const auto* ordinals = reinterpret_cast<const WORD*>(base + exports->AddressOfNameOrdinals);
    const auto* funcs    = reinterpret_cast<const DWORD*>(base + exports->AddressOfFunctions);
    const IMAGE_SECTION_HEADER* sections = IMAGE_FIRST_SECTION(nt);

    std::ofstream out(target, std::ios::binary | std::ios::trunc);
    if (!out)
        return false;
    out << "LIBRARY \"" << exePath().filename().string() << "\"\nEXPORTS\n";
    for (DWORD i = 0; i < exports->NumberOfNames; ++i)
    {
        const DWORD rva = funcs[ordinals[i]];
        if (rva >= dir.VirtualAddress && rva < dir.VirtualAddress + dir.Size)
            continue;
        bool code = false;
        for (WORD s = 0; s < nt->FileHeader.NumberOfSections; ++s)
        {
            const IMAGE_SECTION_HEADER& section = sections[s];
            if (rva >= section.VirtualAddress && rva < section.VirtualAddress + section.Misc.VirtualSize)
            {
                code = (section.Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
                break;
            }
        }
        out << "    " << reinterpret_cast<const char*>(base + names[i]) << (code ? "" : " DATA") << "\n";
    }
    return static_cast<bool>(out);
}

bool ensureSdk(fs::path* sdkOut, std::string* error)
{
    std::lock_guard<std::mutex> lock(gSdkMutex);
    const fs::path dir = appDataDir() / "sdk" / sdkTag();
    std::error_code ec;
    if (!fs::exists(dir / "ready", ec))
    {
        fs::remove_all(dir, ec);
        int count            = 0;
        const SdkFile* files = sdkFiles(count);
        for (int i = 0; i < count; ++i)
        {
            const fs::path target = dir / "include" / files[i].path;
            fs::create_directories(target.parent_path(), ec);
            std::ofstream out(target, std::ios::binary | std::ios::trunc);
            if (!out)
            {
                if (error)
                    *error = "No se pudo escribir " + target.string();
                return false;
            }
            out.write(reinterpret_cast<const char*>(files[i].data), static_cast<std::streamsize>(files[i].size));
        }
        if (!writeExportsDef(dir / "zio_editor.def"))
        {
            if (error)
                *error = "El editor no exporta los simbolos del motor (compilalo con ENABLE_EXPORTS).";
            return false;
        }
        std::ofstream(dir / "ready").put('1');
    }
    *sdkOut = dir;
    return true;
}

std::vector<fs::path> sourceFiles(const fs::path& projectDir)
{
    std::vector<fs::path> out;
    std::error_code ec;
    const fs::path scripts = projectDir / "assets" / "scripts";
    if (!fs::is_directory(scripts, ec))
        return out;
    for (const auto& entry : fs::recursive_directory_iterator(scripts, ec))
    {
        const std::string ext = entry.path().extension().string();
        if (entry.is_regular_file() && (ext == ".cpp" || ext == ".cc"))
            out.push_back(entry.path());
    }
    std::sort(out.begin(), out.end());
    return out;
}

fs::path findVcvars()
{
    const char* programFiles = std::getenv("ProgramFiles(x86)");
    if (!programFiles || !*programFiles)
        return {};
    const fs::path vswhere = fs::path(programFiles) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe";
    std::error_code ec;
    if (!fs::exists(vswhere, ec))
        return {};
    std::string install;
    DWORD exitCode = 1;
    runHidden(quoted(vswhere) +
                  L" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath",
              {},
              [&](const std::string& line) {
                  if (install.empty())
                      install = line;
              },
              &exitCode);
    if (install.empty())
        return {};
    const fs::path vcvars = fs::path(install) / "VC" / "Auxiliary" / "Build" / "vcvars64.bat";
    return fs::exists(vcvars, ec) ? vcvars : fs::path();
}

}

struct NativeScripts::Impl
{
    struct Module
    {
        HMODULE handle = nullptr;
        fs::path file;
        std::vector<std::string> names;
    };

    mutable std::mutex mutex;
    Module current;
    std::vector<Module> retired;
    unsigned generation = 0;

    static void release(Module& module, std::vector<Module>& keep)
    {
        if (!module.handle)
            return;
        Registry& registry = Registry::instance();
        for (const std::string& name : module.names)
            registry.remove(name);
        if (registry.inUse(module.names))
        {
            keep.push_back(std::move(module));
        }
        else
        {
            FreeLibrary(module.handle);
            std::error_code ec;
            fs::remove(module.file, ec);
        }
        module = Module{};
    }

    void sweepRetired()
    {
        std::vector<Module> pending = std::move(retired);
        retired.clear();
        for (Module& module : pending)
        {
            if (Registry::instance().inUse(module.names))
            {
                retired.push_back(std::move(module));
                continue;
            }
            FreeLibrary(module.handle);
            std::error_code ec;
            fs::remove(module.file, ec);
        }
    }
};

NativeScripts::NativeScripts() : impl_(std::make_unique<Impl>()) {}
NativeScripts::~NativeScripts() = default;

NativeScripts& NativeScripts::instance()
{
    static NativeScripts* scripts = new NativeScripts;
    return *scripts;
}

bool NativeScripts::toolchainAvailable(std::string* problem)
{
    if (!findVcvars().empty())
        return true;
    if (problem)
        *problem = "Falta el compilador de C++ (Visual Studio Build Tools).";
    return false;
}

bool NativeScripts::installToolchain(std::string* error)
{
    const HINSTANCE result = ShellExecuteW(
        nullptr, L"open", L"cmd.exe",
        L"/k winget install --id Microsoft.VisualStudio.2022.BuildTools -e --override \"--passive --wait --add "
        L"Microsoft.VisualStudio.Workload.VCTools --includeRecommended\"",
        nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32)
    {
        if (error)
            *error = "No se pudo iniciar winget.";
        return false;
    }
    return true;
}

fs::path NativeScripts::scriptsDllPath(const fs::path& projectDir)
{
    return projectDir / ".zio" / "cpp" / "bin" / "Scripts.dll";
}

void NativeScripts::prepareProject(const fs::path& projectDir)
{
    fs::path sdk;
    if (!ensureSdk(&sdk, nullptr))
        return;
    std::error_code ec;
    const std::string include = slashes(sdk / "include");
    const std::string scripts = slashes(projectDir / "assets" / "scripts");
    {
        std::ofstream clangd(projectDir / ".clangd", std::ios::binary | std::ios::trunc);
        clangd << "CompileFlags:\n  Add: [-std=c++20, -DZIO_SHARED_LIB, \"-I" << include << "\", \"-I" << scripts << "\"]\n";
    }
    fs::create_directories(projectDir / ".vscode", ec);
    {
        std::ofstream vscode(projectDir / ".vscode" / "c_cpp_properties.json", std::ios::binary | std::ios::trunc);
        vscode << "{\n  \"version\": 4,\n  \"configurations\": [\n    {\n      \"name\": \"Zio\",\n      \"includePath\": [\""
               << include << "\", \"" << scripts
               << "\"],\n      \"defines\": [\"ZIO_SHARED_LIB\"],\n      \"cppStandard\": \"c++20\"\n    }\n  ]\n}\n";
    }
}

bool NativeScripts::buildScripts(const fs::path& projectDir, const std::function<void(const std::string&)>& onLine,
                                 std::string* error, const std::string& hostExecutable, fs::path* outputDll)
{
    const auto fail = [&](const std::string& message) {
        if (error)
            *error = message;
        return false;
    };
    const std::vector<fs::path> sources = sourceFiles(projectDir);
    if (sources.empty())
        return fail("El proyecto no tiene scripts de C++ en assets/scripts.");
    std::string problem;
    if (!toolchainAvailable(&problem))
        return fail(problem);
    const fs::path vcvars = findVcvars();

    fs::path sdk;
    if (!ensureSdk(&sdk, &problem))
        return fail(problem);

    const bool forHost  = !hostExecutable.empty();
    const fs::path work = projectDir / ".zio" / (forHost ? "cpp-export" : "cpp");
    std::error_code ec;
    fs::create_directories(work / "bin", ec);
    fs::create_directories(work / "obj", ec);
    fs::remove(work / "bin" / "Scripts.dll", ec);

    std::string libName = "zio_editor";
    if (forHost)
    {
        libName = "host_" + hostExecutable;
        for (char& c : libName)
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_')
                c = '_';
        std::ifstream baseDef(sdk / "zio_editor.def", std::ios::binary);
        std::string defText((std::istreambuf_iterator<char>(baseDef)), std::istreambuf_iterator<char>());
        const std::size_t firstLine = defText.find('\n');
        if (defText.empty() || firstLine == std::string::npos)
            return fail("No se pudo preparar la biblioteca de importacion del juego.");
        std::ofstream hostDef(sdk / (libName + ".def"), std::ios::binary | std::ios::trunc);
        hostDef << "LIBRARY \"" << hostExecutable << "\"" << defText.substr(firstLine);
        hostDef.close();
        fs::remove(sdk / (libName + ".lib"), ec);
    }

    {
        std::ofstream rsp(work / "build.rsp", std::ios::binary | std::ios::trunc);
        if (!rsp)
            return fail("No se pudo escribir build.rsp");
        rsp << "/nologo\n/std:c++20\n/EHsc\n/MD\n/O2\n/utf-8\n/W3\n/wd4251\n/DZIO_SHARED_LIB\n"
            << "/I\"" << slashes(sdk / "include") << "\"\n"
            << "/I\"" << slashes(projectDir / "assets" / "scripts") << "\"\n/LD\n";
        for (const fs::path& source : sources)
            rsp << "\"" << slashes(source) << "\"\n";
        rsp << "/Fo\"" << slashes(work / "obj") << "/\"\n"
            << "/Fe\"" << slashes(work / "bin" / "Scripts.dll") << "\"\n/link\n/nologo\n"
            << "\"" << slashes(sdk / (libName + ".lib")) << "\"\n"
            << "/IMPLIB:\"" << slashes(work / "obj" / "Scripts.lib") << "\"\n";
    }

    const fs::path lib = sdk / (libName + ".lib");
    const std::wstring command = L"cmd.exe /d /s /c \"" + quoted(vcvars) + L" >nul 2>&1 && (if not exist " + quoted(lib) +
                                 L" lib /nologo /machine:x64 /def:" + quoted(sdk / (libName + ".def")) + L" /out:" +
                                 quoted(lib) + L") && cl.exe @" + quoted(work / "build.rsp") + L"\"";
    DWORD exitCode = 1;
    if (!runHidden(command, work, onLine, &exitCode))
        return fail("No se pudo ejecutar el compilador de C++");
    if (exitCode != 0)
        return fail("La compilacion de los scripts de C++ fallo.");
    if (!fs::exists(work / "bin" / "Scripts.dll", ec))
        return fail("La compilacion termino pero no se genero Scripts.dll.");
    if (outputDll)
        *outputDll = work / "bin" / "Scripts.dll";
    return true;
}

bool NativeScripts::load(const fs::path& scriptsDll, std::string* error)
{
    const auto fail = [&](const std::string& message) {
        if (error)
            *error = message;
        return false;
    };
    std::error_code ec;
    if (!fs::exists(scriptsDll, ec))
        return fail("No existe " + scriptsDll.string());

    std::lock_guard<std::mutex> lock(impl_->mutex);
    Impl::release(impl_->current, impl_->retired);
    impl_->sweepRetired();

    const fs::path loadedDir = scriptsDll.parent_path().parent_path() / "loaded";
    fs::create_directories(loadedDir, ec);
    const fs::path copy = loadedDir / ("Scripts_" + std::to_string(++impl_->generation) + ".dll");
    fs::copy_file(scriptsDll, copy, fs::copy_options::overwrite_existing, ec);
    if (ec)
        return fail("No se pudo copiar Scripts.dll: " + ec.message());

    Registry& registry = Registry::instance();
    registry.beginRecording();
    HMODULE module                    = LoadLibraryW(copy.c_str());
    std::vector<std::string> recorded = registry.endRecording();
    if (!module)
    {
        fs::remove(copy, ec);
        return fail("No se pudo cargar Scripts.dll (error " + std::to_string(GetLastError()) + ").");
    }

    impl_->current.handle = module;
    impl_->current.file   = copy;
    for (std::string& name : recorded)
        if (std::find(impl_->current.names.begin(), impl_->current.names.end(), name) == impl_->current.names.end())
            impl_->current.names.push_back(std::move(name));
    registry.notifyChanged();
    return true;
}

void NativeScripts::unload()
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    Impl::release(impl_->current, impl_->retired);
    impl_->sweepRetired();
    Registry::instance().notifyChanged();
}

std::vector<std::string> NativeScripts::scriptNames() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->current.names;
}

bool NativeScripts::loaded() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->current.handle != nullptr;
}

}

#else

namespace zio::scripting
{

struct NativeScripts::Impl
{
};

NativeScripts::NativeScripts() : impl_(std::make_unique<Impl>()) {}
NativeScripts::~NativeScripts() = default;
NativeScripts& NativeScripts::instance()
{
    static NativeScripts* scripts = new NativeScripts;
    return *scripts;
}
bool NativeScripts::toolchainAvailable(std::string* problem)
{
    if (problem)
        *problem = "Los scripts de C++ del editor solo estan disponibles en Windows.";
    return false;
}
bool NativeScripts::installToolchain(std::string* error)
{
    if (error)
        *error = "Los scripts de C++ del editor solo estan disponibles en Windows.";
    return false;
}
fs::path NativeScripts::scriptsDllPath(const fs::path& projectDir) { return projectDir / ".zio" / "cpp" / "bin" / "Scripts.dll"; }
void NativeScripts::prepareProject(const fs::path&) {}
bool NativeScripts::buildScripts(const fs::path&, const std::function<void(const std::string&)>&, std::string* error,
                                 const std::string&, fs::path*)
{
    if (error)
        *error = "Los scripts de C++ del editor solo estan disponibles en Windows.";
    return false;
}
bool NativeScripts::load(const fs::path&, std::string* error)
{
    if (error)
        *error = "Los scripts de C++ del editor solo estan disponibles en Windows.";
    return false;
}
void NativeScripts::unload() {}
std::vector<std::string> NativeScripts::scriptNames() const { return {}; }
bool NativeScripts::loaded() const { return false; }

}

#endif
