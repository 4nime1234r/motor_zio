#include "zio/platform/FileDialog.h"

#include "zio/core/Platform.h"

#if defined(ZIO_PLATFORM_WINDOWS)

#include <windows.h>

#include <shobjidl.h>
#include <wrl/client.h>

#include <filesystem>

using Microsoft::WRL::ComPtr;

namespace zio::platform
{

namespace
{

std::wstring widen(std::string_view text)
{
    if (text.empty())
        return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), size);
    return wide;
}

std::string narrow(const wchar_t* text)
{
    const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1)
        return {};
    std::string out(static_cast<std::size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), size, nullptr, nullptr);
    return out;
}

struct ComScope
{
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    ~ComScope()
    {
        if (SUCCEEDED(hr))
            CoUninitialize();
    }
    bool usable() const { return SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE; }
};

std::optional<std::string> runDialog(bool save, bool folder, const FileDialogOptions& options)
{
    ComScope com;
    if (!com.usable())
        return std::nullopt;

    ComPtr<IFileDialog> dialog;
    const CLSID clsid = save ? CLSID_FileSaveDialog : CLSID_FileOpenDialog;
    const IID iid     = save ? __uuidof(IFileSaveDialog) : __uuidof(IFileOpenDialog);
    if (FAILED(CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, iid, reinterpret_cast<void**>(dialog.GetAddressOf()))))
        return std::nullopt;

    DWORD flags = 0;
    dialog->GetOptions(&flags);
    flags |= FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR;
    if (folder)
        flags |= FOS_PICKFOLDERS;
    else if (save)
        flags |= FOS_OVERWRITEPROMPT;
    else
        flags |= FOS_FILEMUSTEXIST;
    dialog->SetOptions(flags);

    if (!options.title.empty())
        dialog->SetTitle(widen(options.title).c_str());

    std::vector<std::wstring> names, patterns;
    std::vector<COMDLG_FILTERSPEC> specs;
    if (!folder && !options.filters.empty())
    {
        names.reserve(options.filters.size());
        patterns.reserve(options.filters.size());
        for (const FileFilter& filter : options.filters)
        {
            names.push_back(widen(filter.name));
            patterns.push_back(widen(filter.pattern));
        }
        for (std::size_t i = 0; i < names.size(); ++i)
            specs.push_back({names[i].c_str(), patterns[i].c_str()});
        dialog->SetFileTypes(static_cast<UINT>(specs.size()), specs.data());
        dialog->SetFileTypeIndex(1);
    }

    if (!options.defaultName.empty())
        dialog->SetFileName(widen(options.defaultName).c_str());

    if (!options.initialDirectory.empty())
    {
        std::error_code ec;
        std::filesystem::path directory = std::filesystem::u8path(options.initialDirectory);
        if (std::filesystem::is_regular_file(directory, ec))
            directory = directory.parent_path();
        ComPtr<IShellItem> item;
        if (SUCCEEDED(SHCreateItemFromParsingName(directory.wstring().c_str(), nullptr, IID_PPV_ARGS(&item))))
            dialog->SetFolder(item.Get());
    }

    if (FAILED(dialog->Show(GetActiveWindow())))
        return std::nullopt;

    ComPtr<IShellItem> result;
    if (FAILED(dialog->GetResult(&result)))
        return std::nullopt;
    PWSTR path = nullptr;
    if (FAILED(result->GetDisplayName(SIGDN_FILESYSPATH, &path)))
        return std::nullopt;
    std::string text = narrow(path);
    CoTaskMemFree(path);
    if (text.empty())
        return std::nullopt;
    return text;
}

}

bool fileDialogAvailable()
{
    return true;
}

std::optional<std::string> openFileDialog(const FileDialogOptions& options)
{
    return runDialog(false, false, options);
}

std::optional<std::string> saveFileDialog(const FileDialogOptions& options)
{
    return runDialog(true, false, options);
}

std::optional<std::string> pickFolderDialog(const FileDialogOptions& options)
{
    return runDialog(false, true, options);
}

}

#else

namespace zio::platform
{

bool fileDialogAvailable()
{
    return false;
}

std::optional<std::string> openFileDialog(const FileDialogOptions&)
{
    return std::nullopt;
}

std::optional<std::string> saveFileDialog(const FileDialogOptions&)
{
    return std::nullopt;
}

std::optional<std::string> pickFolderDialog(const FileDialogOptions&)
{
    return std::nullopt;
}

}

#endif
