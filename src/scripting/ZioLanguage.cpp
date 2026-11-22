#include "zio/scripting/Language.h"

namespace zio::scripting
{

#if defined(ZIO_ENABLE_ZIO_LANGUAGE)

namespace
{

class ZioLanguage final : public ScriptLanguage
{
public:
    std::string id() const override { return "zio"; }
    std::string displayName() const override { return "Zio"; }
    std::vector<std::string> sourceExtensions() const override { return {"zl"}; }
    std::string newFileExtension() const override { return "zl"; }
    std::string sourceTemplate(const std::string& className) const override { return "script " + className + "\n"; }

    std::filesystem::path outputPath(const std::filesystem::path& projectDir) const override
    {
        return projectDir / ".zio" / "zl" / "scripts.bin";
    }

    bool build(const std::filesystem::path& projectDir, const std::function<void(const std::string&)>& onLine,
               std::string* error) override
    {
        if (error)
            *error = "El lenguaje de Zio todavia no esta implementado.";
        return false;
    }

    bool load(const std::filesystem::path& projectDir, std::string* error) override
    {
        if (error)
            *error = "El lenguaje de Zio todavia no esta implementado.";
        return false;
    }
};

}

std::unique_ptr<ScriptLanguage> makeZioLanguage()
{
    return std::make_unique<ZioLanguage>();
}

#else

std::unique_ptr<ScriptLanguage> makeZioLanguage()
{
    return nullptr;
}

#endif

}
