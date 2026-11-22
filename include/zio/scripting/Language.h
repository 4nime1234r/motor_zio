#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "zio/core/Platform.h"

namespace zio::scripting
{

class ZIO_API ScriptLanguage
{
public:
    virtual ~ScriptLanguage() = default;

    virtual std::string id() const                                         = 0;
    virtual std::string displayName() const                                = 0;
    virtual std::vector<std::string> sourceExtensions() const              = 0;
    virtual std::string newFileExtension() const                           = 0;
    virtual std::string sourceTemplate(const std::string& className) const = 0;

    virtual bool toolsAvailable(std::string* problem) const { return true; }
    virtual bool installTools(std::string* error) { return false; }

    virtual std::filesystem::path outputPath(const std::filesystem::path& projectDir) const = 0;
    virtual void prepareProject(const std::filesystem::path& projectDir) {}
    virtual bool build(const std::filesystem::path& projectDir, const std::function<void(const std::string&)>& onLine,
                       std::string* error)                                                  = 0;
    virtual bool load(const std::filesystem::path& projectDir, std::string* error)          = 0;
    virtual void unload() {}
    virtual std::vector<std::string> loadedNames() const { return {}; }

    bool ownsExtension(const std::string& extension) const;
    bool hasSources(const std::filesystem::path& projectDir) const;
    bool needsBuild(const std::filesystem::path& projectDir) const;
    std::filesystem::file_time_type newestSource(const std::filesystem::path& projectDir) const;
};

class ZIO_API Languages
{
public:
    static Languages& instance();
    static void registerBuiltin();

    Languages()                            = default;
    Languages(const Languages&)            = delete;
    Languages& operator=(const Languages&) = delete;

    ScriptLanguage* add(std::unique_ptr<ScriptLanguage> language);
    const std::vector<std::unique_ptr<ScriptLanguage>>& all() const { return languages_; }
    ScriptLanguage* forExtension(const std::string& extension) const;
    bool isScriptExtension(const std::string& extension) const;

private:
    std::vector<std::unique_ptr<ScriptLanguage>> languages_;
};

}
