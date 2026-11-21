#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace zio_editor
{

class ProjectManager
{
public:
    static constexpr const char* kProjectFile    = "project.zio";
    static constexpr const char* kFormat         = "zio.project";
    static constexpr const char* kSceneExtension = ".zscene3d";

    static bool checkNewProject(const std::filesystem::path& parentDir, std::string_view name,
                                std::string* error = nullptr);
    bool createProject(const std::filesystem::path& parentDir, std::string_view name, std::string* error = nullptr);
    bool loadProject(const std::filesystem::path& path, std::string* error = nullptr);
    void close();

    bool isOpen() const { return open_; }
    const std::string& name() const { return name_; }
    const std::filesystem::path& rootDir() const { return root_; }

    std::filesystem::path assetsDir() const { return root_ / "assets"; }
    std::filesystem::path modelsDir() const { return root_ / "assets" / "models"; }
    std::filesystem::path texturesDir() const { return root_ / "assets" / "textures"; }
    std::filesystem::path scenesDir() const { return root_ / "assets" / "scenes"; }

    std::optional<std::filesystem::path> resolve(std::string_view relative) const;
    std::optional<std::string> toRelative(const std::filesystem::path& path) const;

    static bool isValidProjectName(std::string_view name, std::string* reason = nullptr);
    static bool isSceneFile(const std::filesystem::path& path);
    static bool isMeshFile(const std::filesystem::path& path);

    std::vector<std::string> listFiles(std::string_view relativeDir, std::string_view extension) const;

private:
    bool open_ = false;
    std::string name_;
    std::filesystem::path root_;
};

}
