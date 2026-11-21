#include "panels/ContentBrowserModel.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

namespace zio_editor
{

namespace
{

std::string lower(std::string s)
{
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool isUnderRoot(const std::string& relative)
{
    const std::string root = ContentBrowserModel::kRoot;
    return relative == root || (relative.size() > root.size() && relative.compare(0, root.size() + 1, root + "/") == 0);
}

}

std::string ContentBrowserModel::displayName(std::string_view parentDir, std::string_view name)
{
    if (parentDir == kRoot)
    {
        if (name == "models")
            return "modelos";
        if (name == "scenes")
            return "escenas";
        if (name == "textures")
            return "texturas";
    }
    return std::string(name);
}

bool ContentBrowserModel::goUp()
{
    if (!canGoUp())
        return false;
    const std::size_t slash = currentDir_.find_last_of('/');
    currentDir_             = slash == std::string::npos ? std::string(kRoot) : currentDir_.substr(0, slash);
    if (!isUnderRoot(currentDir_))
        currentDir_ = kRoot;
    return true;
}

bool ContentBrowserModel::goTo(const ProjectManager& project, std::string_view relativeDir)
{
    std::string normal = fs::path(relativeDir).lexically_normal().generic_string();
    while (normal.size() > 1 && normal.back() == '/')
        normal.pop_back();
    if (!isUnderRoot(normal))
        return false;

    const auto absolute = project.resolve(normal);
    std::error_code ec;
    if (!absolute || !fs::is_directory(*absolute, ec))
        return false;

    currentDir_ = normal;
    return true;
}

bool ContentBrowserModel::enter(const ProjectManager& project, std::string_view folderName)
{
    if (folderName.empty() || folderName == "." || folderName == ".." ||
        folderName.find_first_of("/\\") != std::string_view::npos)
        return false;
    return goTo(project, currentDir_ + "/" + std::string(folderName));
}

std::vector<std::pair<std::string, std::string>> ContentBrowserModel::breadcrumbs() const
{
    std::vector<std::pair<std::string, std::string>> out;
    std::string path;
    std::size_t start = 0;
    while (start <= currentDir_.size())
    {
        const std::size_t slash = currentDir_.find('/', start);
        const std::size_t end   = slash == std::string::npos ? currentDir_.size() : slash;
        const std::string part  = currentDir_.substr(start, end - start);
        if (!part.empty())
        {
            const std::string parent = path;
            path += (path.empty() ? "" : "/") + part;
            out.emplace_back(displayName(parent, part), path);
        }
        if (slash == std::string::npos)
            break;
        start = slash + 1;
    }
    return out;
}

std::vector<ContentBrowserModel::Entry> ContentBrowserModel::list(const ProjectManager& project,
                                                                  std::string* error) const
{
    std::vector<Entry> entries;
    const auto dir = project.resolve(currentDir_);
    if (!dir)
    {
        if (error)
            *error = "No hay un proyecto abierto";
        return entries;
    }

    std::error_code ec;
    fs::directory_iterator it(*dir, ec);
    if (ec)
    {
        if (error)
            *error = "No se puede leer " + dir->string() + ": " + ec.message();
        return entries;
    }

    for (const fs::directory_iterator end; it != end; it.increment(ec))
    {
        if (ec)
            break;
        std::error_code entryEc;
        Entry e;
        e.name         = it->path().filename().string();
        e.displayName  = displayName(currentDir_, e.name);
        e.relativePath = currentDir_ + "/" + e.name;
        if (!project.resolve(e.relativePath))
            continue;
        e.isDirectory  = it->is_directory(entryEc);
        e.isScene      = !e.isDirectory && ProjectManager::isSceneFile(it->path());
        e.isMesh       = !e.isDirectory && ProjectManager::isMeshFile(it->path());
        entries.push_back(std::move(e));
    }

    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        if (a.isDirectory != b.isDirectory)
            return a.isDirectory;
        return lower(a.displayName) < lower(b.displayName);
    });
    return entries;
}

}
