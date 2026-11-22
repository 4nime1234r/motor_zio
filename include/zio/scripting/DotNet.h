#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "zio/core/Platform.h"

namespace zio::scripting
{

class ZIO_API DotNetRuntime
{
public:
    static DotNetRuntime& instance();

    static bool embeddedBridgeAvailable();

    bool start(const std::filesystem::path& workDir, std::string* error = nullptr);
    bool running() const;

    bool loadScripts(const std::filesystem::path& scriptsDll, std::string* error = nullptr);
    std::vector<std::string> scriptNames() const;
    unsigned generation() const;

    std::filesystem::path bridgePath() const;

    static bool dotnetSdkAvailable();
    static bool buildScripts(const std::filesystem::path& projectDir, const std::function<void(const std::string&)>& onLine,
                             std::string* error = nullptr);
    static std::filesystem::path scriptsDllPath(const std::filesystem::path& projectDir);
    static bool installSdk(std::string* error = nullptr);
    static void prepareProject(const std::filesystem::path& projectDir);

    static std::vector<std::string> takeLog();
    static std::string takeSceneRequest();

    int createScript(const std::string& name);
    void startScript(int handle, void* node);
    void updateScript(int handle, float dt);
    void destroyScript(int handle);

private:
    DotNetRuntime();
    ~DotNetRuntime();

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
