#pragma once

#include <filesystem>
#include <functional>
#include <string>

#include "project/ProjectManager.h"
#include "zio/pack/Package.h"

namespace zio_editor
{

struct ExportRequest
{
    std::filesystem::path projectRoot;
    std::filesystem::path outputDir;
    std::string projectName;
    GameInfo info;
    std::string executableName;
    zio::pack::Protection protection = zio::pack::Protection::Password;
    std::string password;
    bool sign           = true;
    bool includeSources = false;
};

struct ExportResult
{
    std::filesystem::path package;
    std::filesystem::path executable;
    std::filesystem::path keyFile;
    std::string fingerprint;
    bool signedPackage = false;
};

struct ImportRequest
{
    std::filesystem::path package;
    std::filesystem::path destinationParent;
    std::string projectName;
    zio::pack::OpenCredentials credentials;
};

std::string sanitizeExecutableName(const std::string& text);
bool applyExecutableResources(const std::filesystem::path& executable, const std::filesystem::path& icon,
                              const GameInfo& info, const std::string& fileName, std::string* error);
std::filesystem::path signingKeyPath();
std::filesystem::path editorExecutablePath();

bool exportGame(const ExportRequest& request, const std::function<void(const std::string&)>& log, ExportResult& result,
                std::string* error);

bool importGame(const ImportRequest& request, std::filesystem::path& projectRoot, zio::pack::PackageHeader& header,
                std::string* error);

}
