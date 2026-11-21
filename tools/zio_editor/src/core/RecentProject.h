#pragma once

#include <filesystem>
#include <optional>

namespace zio_editor
{

std::filesystem::path recentProjectFile();

std::optional<std::filesystem::path> readRecentProject(const std::filesystem::path& file);

bool writeRecentProject(const std::filesystem::path& file, const std::filesystem::path& projectRoot);

}
