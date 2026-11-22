#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "zio/core/Platform.h"

namespace zio::platform
{

struct FileFilter
{
    std::string name;
    std::string pattern;
};

struct FileDialogOptions
{
    std::string title;
    std::string initialDirectory;
    std::string defaultName;
    std::vector<FileFilter> filters;
};

ZIO_API bool fileDialogAvailable();

ZIO_API std::optional<std::string> openFileDialog(const FileDialogOptions& options);

ZIO_API std::optional<std::string> saveFileDialog(const FileDialogOptions& options);

ZIO_API std::optional<std::string> pickFolderDialog(const FileDialogOptions& options);

}
