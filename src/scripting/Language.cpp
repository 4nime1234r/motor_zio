#include "zio/scripting/Language.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

#include "zio/scripting/DotNet.h"
#include "zio/scripting/NativeScripts.h"

namespace fs = std::filesystem;

namespace zio::scripting
{

std::unique_ptr<ScriptLanguage> makeZioLanguage();

namespace
{

std::string lowerCase(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string extensionOf(const fs::path& path)
{
    std::string ext = path.extension().string();
    if (!ext.empty())
        ext.erase(0, 1);
    return ext;
}

class CppLanguage final : public ScriptLanguage
{
public:
    std::string id() const override { return "cpp"; }
    std::string displayName() const override { return "C++"; }
    std::vector<std::string> sourceExtensions() const override { return {"cpp", "cc", "h", "hpp"}; }
    std::string newFileExtension() const override { return "cpp"; }

    std::string sourceTemplate(const std::string& className) const override
    {
        return "#include \"zio/scene3d/Node3D.h\"\n#include \"zio/scripting/Script.h\"\n\nclass " + className +
               " : public zio::scripting::Script\n{\npublic:\n    void onStart() override\n    {\n    }\n\n    void "
               "onUpdate(float dt) override\n    {\n    }\n};\n\nZIO_SCRIPT(" +
               className + ")\n";
    }

    bool toolsAvailable(std::string* problem) const override { return NativeScripts::toolchainAvailable(problem); }
    bool installTools(std::string* error) override { return NativeScripts::installToolchain(error); }
    fs::path outputPath(const fs::path& projectDir) const override { return NativeScripts::scriptsDllPath(projectDir); }
    void prepareProject(const fs::path& projectDir) override { NativeScripts::prepareProject(projectDir); }

    bool build(const fs::path& projectDir, const std::function<void(const std::string&)>& onLine, std::string* error) override
    {
        return NativeScripts::buildScripts(projectDir, onLine, error);
    }

    bool load(const fs::path& projectDir, std::string* error) override
    {
        return NativeScripts::instance().load(NativeScripts::scriptsDllPath(projectDir), error);
    }

    void unload() override { NativeScripts::instance().unload(); }
    std::vector<std::string> loadedNames() const override { return NativeScripts::instance().scriptNames(); }
};

class CSharpLanguage final : public ScriptLanguage
{
public:
    std::string id() const override { return "csharp"; }
    std::string displayName() const override { return "C#"; }
    std::vector<std::string> sourceExtensions() const override { return {"cs"}; }
    std::string newFileExtension() const override { return "cs"; }

    std::string sourceTemplate(const std::string& className) const override
    {
        return "using Zio;\n\npublic class " + className +
               " : Script\n{\n    public override void OnStart()\n    {\n    }\n\n    public override void "
               "OnUpdate(float dt)\n    {\n    }\n}\n";
    }

    bool toolsAvailable(std::string* problem) const override
    {
        if (!DotNetRuntime::embeddedBridgeAvailable())
        {
            if (problem)
                *problem = "Esta compilacion de Zio no incluye el puente de C#.";
            return false;
        }
        if (!DotNetRuntime::dotnetSdkAvailable())
        {
            if (problem)
                *problem = "Falta el SDK de .NET 8 o superior.";
            return false;
        }
        return start(problem);
    }

    bool installTools(std::string* error) override { return DotNetRuntime::installSdk(error); }
    fs::path outputPath(const fs::path& projectDir) const override { return DotNetRuntime::scriptsDllPath(projectDir); }

    void prepareProject(const fs::path& projectDir) override
    {
        if (start(nullptr))
            DotNetRuntime::prepareProject(projectDir);
    }

    bool build(const fs::path& projectDir, const std::function<void(const std::string&)>& onLine, std::string* error) override
    {
        return DotNetRuntime::buildScripts(projectDir, onLine, error);
    }

    bool load(const fs::path& projectDir, std::string* error) override
    {
        if (!start(error))
            return false;
        return DotNetRuntime::instance().loadScripts(DotNetRuntime::scriptsDllPath(projectDir), error);
    }

    std::vector<std::string> loadedNames() const override { return DotNetRuntime::instance().scriptNames(); }

private:
    static bool start(std::string* error)
    {
        auto& runtime = DotNetRuntime::instance();
        if (runtime.running())
            return true;
        const char* appData = std::getenv("APPDATA");
        const fs::path dir  = (appData && *appData ? fs::path(appData) : fs::temp_directory_path()) / "ZioEditor" / "dotnet";
        return runtime.start(dir, error);
    }
};

}

bool ScriptLanguage::ownsExtension(const std::string& extension) const
{
    const std::string wanted = lowerCase(extension);
    for (const std::string& own : sourceExtensions())
        if (own == wanted)
            return true;
    return false;
}

bool ScriptLanguage::hasSources(const fs::path& projectDir) const
{
    std::error_code ec;
    const fs::path scripts = projectDir / "assets" / "scripts";
    if (!fs::is_directory(scripts, ec))
        return false;
    for (const auto& entry : fs::recursive_directory_iterator(scripts, ec))
        if (entry.is_regular_file() && ownsExtension(extensionOf(entry.path())))
            return true;
    return false;
}

std::filesystem::file_time_type ScriptLanguage::newestSource(const fs::path& projectDir) const
{
    std::error_code ec;
    fs::file_time_type newest{};
    const fs::path scripts = projectDir / "assets" / "scripts";
    if (!fs::is_directory(scripts, ec))
        return newest;
    for (const auto& entry : fs::recursive_directory_iterator(scripts, ec))
    {
        if (!entry.is_regular_file() || !ownsExtension(extensionOf(entry.path())))
            continue;
        const auto time = entry.last_write_time(ec);
        if (time > newest)
            newest = time;
    }
    return newest;
}

bool ScriptLanguage::needsBuild(const fs::path& projectDir) const
{
    if (!hasSources(projectDir))
        return false;
    std::error_code ec;
    const fs::path output = outputPath(projectDir);
    if (!fs::exists(output, ec))
        return true;
    return newestSource(projectDir) > fs::last_write_time(output, ec);
}

Languages& Languages::instance()
{
    static Languages* languages = new Languages;
    return *languages;
}

void Languages::registerBuiltin()
{
    Languages& languages = instance();
    if (!languages.all().empty())
        return;
    languages.add(std::make_unique<CppLanguage>());
    languages.add(std::make_unique<CSharpLanguage>());
    if (std::unique_ptr<ScriptLanguage> own = makeZioLanguage())
        languages.add(std::move(own));
}

ScriptLanguage* Languages::add(std::unique_ptr<ScriptLanguage> language)
{
    if (!language)
        return nullptr;
    languages_.push_back(std::move(language));
    return languages_.back().get();
}

ScriptLanguage* Languages::forExtension(const std::string& extension) const
{
    for (const auto& language : languages_)
        if (language->ownsExtension(extension))
            return language.get();
    return nullptr;
}

bool Languages::isScriptExtension(const std::string& extension) const
{
    return forExtension(extension) != nullptr;
}

}
