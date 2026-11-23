#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "core/GameExport.h"

namespace fs = std::filesystem;

namespace
{

int failures = 0;

void check(bool condition, const char* name)
{
    std::printf("%s %s\n", condition ? "[ok]  " : "[FALLO]", name);
    if (!condition)
        ++failures;
}

std::wstring queryString(const std::vector<char>& block, const wchar_t* key)
{
    const std::wstring path = std::wstring(L"\\StringFileInfo\\040904B0\\") + key;
    void* value             = nullptr;
    UINT length             = 0;
    if (!VerQueryValueW(block.data(), path.c_str(), &value, &length) || !value)
        return {};
    return static_cast<const wchar_t*>(value);
}

}

int main(int, char** argv)
{
    const fs::path self = argv[0];
    const fs::path dir  = fs::temp_directory_path() / "zio_exe_resources_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    const fs::path copy = dir / "juego.exe";
    fs::copy_file(self, copy, fs::copy_options::overwrite_existing, ec);
    check(!ec, "se copia un ejecutable para probar");

    zio_editor::GameInfo info;
    info.title       = "Mi juego";
    info.author      = "4nime1234r";
    info.version     = "2.3.4";
    info.description = "Un juego de prueba";
    info.copyright   = "(c) 2026 4nime1234r";

    const fs::path icon = fs::path(ZIO_SOURCE_DIR) / "assets" / "icon" / "zio.ico";
    std::string error;
    check(zio_editor::applyExecutableResources(copy, icon, info, "juego", &error), "se aplican el icono y los datos");
    if (!error.empty())
        std::printf("error: %s\n", error.c_str());

    DWORD ignored = 0;
    const DWORD size = GetFileVersionInfoSizeW(copy.wstring().c_str(), &ignored);
    check(size > 0, "el ejecutable tiene informacion de version");
    if (size > 0)
    {
        std::vector<char> block(size);
        check(GetFileVersionInfoW(copy.wstring().c_str(), 0, size, block.data()) != 0, "se lee la informacion de version");
        {
            struct Translation { WORD language, codePage; } *translations = nullptr;
            UINT bytes = 0;
            if (VerQueryValueW(block.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&translations), &bytes) && translations)
                std::printf("traduccion %04x %04x (%u bytes)\n", translations->language, translations->codePage, bytes);
            else
                std::printf("sin traduccion\n");
        }
        check(queryString(block, L"CompanyName") == L"4nime1234r", "el autor queda como empresa");
        check(queryString(block, L"ProductName") == L"Mi juego", "el nombre del juego queda como producto");
        check(queryString(block, L"FileVersion") == L"2.3.4", "la version queda en el ejecutable");
        check(queryString(block, L"LegalCopyright") == L"(c) 2026 4nime1234r", "el copyright queda en el ejecutable");
        check(queryString(block, L"OriginalFilename") == L"juego.exe", "el nombre del archivo queda en el ejecutable");
        VS_FIXEDFILEINFO* fixed = nullptr;
        UINT fixedSize          = 0;
        const bool fixedOk      = VerQueryValueW(block.data(), L"\\", reinterpret_cast<void**>(&fixed), &fixedSize) && fixed;
        check(fixedOk && HIWORD(fixed->dwFileVersionMS) == 2 && LOWORD(fixed->dwFileVersionMS) == 3 &&
                  HIWORD(fixed->dwFileVersionLS) == 4,
              "los numeros de version son 2.3.4");
    }

    HMODULE module = LoadLibraryExW(copy.wstring().c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE);
    check(module != nullptr, "el ejecutable modificado se puede abrir");
    if (module)
    {
        check(FindResourceW(module, MAKEINTRESOURCEW(1), MAKEINTRESOURCEW(14)) != nullptr, "tiene un grupo de iconos");
        check(FindResourceW(module, MAKEINTRESOURCEW(1), MAKEINTRESOURCEW(3)) != nullptr, "tiene la imagen del icono");
        FreeLibrary(module);
    }

    check(!zio_editor::applyExecutableResources(copy, dir / "no_existe.ico", info, "juego", &error), "un icono que no existe da error");

    if (!std::getenv("ZIO_KEEP"))
        fs::remove_all(dir, ec);
    std::printf("%s\n", failures == 0 ? "TODO OK" : "HAY FALLOS");
    return failures == 0 ? 0 : 1;
}
