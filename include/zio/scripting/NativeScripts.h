#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "zio/core/Platform.h"

namespace zio::scripting
{

class ZIO_API NativeScripts
{
public:
    static NativeScripts& instance();

    static bool toolchainAvailable(std::string* problem = nullptr);
    static bool installToolchain(std::string* error = nullptr);
    static std::filesystem::path scriptsDllPath(const std::filesystem::path& projectDir);
    static void prepareProject(const std::filesystem::path& projectDir);

    static bool buildScripts(const std::filesystem::path& projectDir, const std::function<void(const std::string&)>& onLine,
                             std::string* error = nullptr, const std::string& hostExecutable = {},
                             std::filesystem::path* outputDll = nullptr);

    bool load(const std::filesystem::path& scriptsDll, std::string* error = nullptr);
    void unload();
    std::vector<std::string> scriptNames() const;
    bool loaded() const;

private:
    NativeScripts();
    ~NativeScripts();

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
