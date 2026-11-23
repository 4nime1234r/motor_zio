#include "PlayerApp.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "zio/scripting/DotNet.h"
#include "zio/scripting/Language.h"
#include "zio/zio.h"

#if defined(ZIO_PLATFORM_WINDOWS)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace zio_player
{

namespace
{

fs::path selfPath()
{
#if defined(ZIO_PLATFORM_WINDOWS)
    wchar_t buffer[MAX_PATH];
    const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    return length == 0 || length >= MAX_PATH ? fs::path() : fs::path(buffer);
#else
    std::error_code ec;
    return fs::read_symlink("/proc/self/exe", ec);
#endif
}

std::vector<std::string> wrap(const std::string& text, std::size_t width)
{
    std::vector<std::string> lines;
    std::istringstream words(text);
    std::string word;
    std::string line;
    while (words >> word)
    {
        if (!line.empty() && line.size() + 1 + word.size() > width)
        {
            lines.push_back(line);
            line.clear();
        }
        line += (line.empty() ? "" : " ") + word;
    }
    if (!line.empty())
        lines.push_back(line);
    return lines;
}

void removeTree(const fs::path& dir)
{
    std::error_code ec;
    std::vector<fs::path> files;
    for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end; it != end && !ec;
         it.increment(ec))
        if (it->is_regular_file(ec))
            files.push_back(it->path());
    for (const fs::path& file : files)
        fs::remove(file, ec);
    fs::remove_all(dir, ec);
}

void sweepStale()
{
    std::error_code ec;
    const fs::path temp = fs::temp_directory_path(ec);
    if (ec)
        return;
    const auto limit = fs::file_time_type::clock::now() - std::chrono::hours(12);
    for (const auto& entry : fs::directory_iterator(temp, ec))
    {
        const std::string name = entry.path().filename().string();
        if (name.rfind("ZioGame_", 0) == 0 && entry.is_directory(ec) && entry.last_write_time(ec) < limit)
            removeTree(entry.path());
    }
}

}

class PlayerApp final : public zio::core::Application
{
public:
    PlayerApp(LaunchArgs args, zio::pack::PackageHeader header, bool headerOk, std::string headerError)
        : args_(std::move(args)), header_(std::move(header)), headerOk_(headerOk), headerError_(std::move(headerError))
    {
    }

    int exitCode() const { return exitCode_; }

protected:
    bool onInit() override
    {
        zio::scripting::Languages::registerBuiltin();
        sweepStale();
        if (const char* smoke = std::getenv("ZIO_PLAYER_SMOKE"); smoke && *smoke && *smoke != '0')
        {
            smoke_ = true;
            if (const char* pw = std::getenv("ZIO_PLAYER_PASSWORD"))
                smokePassword_ = pw;
        }

        ui_ = std::make_unique<zio::ui::Context>();
        if (auto font = zio::ui::createScaledSystemFont("Segoe UI", 17.f, false, zio::platform::Window::instance().dpiScale()))
            ui_->theme().font = font;
        if (auto bold = zio::ui::createScaledSystemFont("Segoe UI", 17.f, true, zio::platform::Window::instance().dpiScale()))
            ui_->theme().fontBold = bold;
        host_ = std::make_unique<zio::ui::EngineHost>(*ui_);

        if (!headerOk_)
        {
            showMessage("No se puede abrir el juego", headerError_);
            return true;
        }
        if (header_.signedPackage)
        {
            std::string signatureError;
            signatureOk_ = zio::pack::verifySignature(args_.package, &signatureError);
            if (!signatureOk_)
            {
                showMessage("Este juego no es seguro", signatureError + " No se abrirá.");
                return true;
            }
        }
        if (header_.protection == zio::pack::Protection::Password)
        {
            if (smoke_)
            {
                unlock(smokePassword_);
                return true;
            }
            showUnlock();
            return true;
        }
        unlock({});
        return true;
    }

    void onBeforeTick(float dt) override
    {
        if (pendingExit_)
        {
            requestExit();
            return;
        }
        if (phase_ != Phase::Playing)
        {
            host_->frame(dt);
            return;
        }
        elapsed_ += dt;
        hud_.update(dt);
        if (smoke_ && ++smokeFrames_ >= 10)
            requestExit();
    }

    void onAfterTick(float) override
    {
        const std::string request = zio::scripting::DotNetRuntime::takeSceneRequest();
        if (request.empty() || phase_ != Phase::Playing)
            return;
        if (!zio::pack::isSafeArchivePath(request))
            return;
        zio::physics3d::World3D::instance().clear();
        auto* scene = zio::scene::SceneStack::instance().run<zio::scene3d::Scene3D>();
        std::string sceneError;
        if (!zio::scene3d::loadScene3DFile(*scene, request, &sceneError))
            std::fprintf(stderr, "No se pudo cargar la escena %s: %s\n", request.c_str(), sceneError.c_str());
    }

    void onRender(float) override
    {
        auto& device = zio::render::Device::instance();
        auto& window = zio::platform::Window::instance();
        if (phase_ == Phase::Playing)
        {
            device.beginFrame(zio::render::ClearColor{0.05f, 0.06f, 0.08f, 1.f});
            if (auto* scene = dynamic_cast<zio::scene3d::Scene3D*>(zio::scene::SceneStack::instance().current()))
                scene->draw(window.width(), window.height());
            if (elapsed_ < 5.f)
            {
                zio::renderer::BatchRenderer::instance().begin(window.width(), window.height());
                hud_.draw();
                zio::renderer::BatchRenderer::instance().end();
            }
            device.endFrame();
            return;
        }
        device.beginFrame(zio::render::ClearColor{0.059f, 0.067f, 0.090f, 1.f});
        zio::renderer::BatchRenderer::instance().begin(window.width(), window.height());
        host_->draw();
        zio::renderer::BatchRenderer::instance().end();
        device.endFrame();
    }

    void onShutdown() override
    {
        zio::scripting::Languages::registerBuiltin();
        for (const auto& language : zio::scripting::Languages::instance().all())
            language->unload();
        if (!extractDir_.empty())
        {
            std::error_code ec;
            fs::current_path(fs::temp_directory_path(ec), ec);
            removeTree(extractDir_);
        }
        host_.reset();
        ui_.reset();
    }

private:
    enum class Phase
    {
        Dialog,
        Playing
    };

    void showMessage(const std::string& title, const std::string& text)
    {
        using namespace zio::ui;
        std::fprintf(stderr, "%s: %s\n", title.c_str(), text.c_str());
        exitCode_ = 3;
        if (smoke_)
        {
            pendingExit_ = true;
            return;
        }
        auto dialog  = std::make_unique<Dialog>(title);
        auto content = std::make_unique<VBox>();
        for (const std::string& line : wrap(text, 62))
            content->add<Label>(line);
        dialog->setContent(std::move(content));
        dialog->setWidth(520.f);
        Dialog* raw = dialog.get();
        dialog->addButton("Salir", [this, raw]() {
            raw->close();
            requestExit();
        }, true);
        dialog->setOnCancel([this]() { requestExit(); });
        ui_->openPopup(std::move(dialog));
    }

    void showUnlock()
    {
        using namespace zio::ui;
        const zio::pack::PackageInfo& info = header_.info;
        auto dialog                        = std::make_unique<Dialog>(info.name.empty() ? "Juego protegido" : info.name);
        auto content                       = std::make_unique<VBox>();
        if (!info.author.empty())
            content->add<Label>("por " + info.author)->setBold(true);
        std::string line = "Versión " + info.version;
        if (!info.copyright.empty())
            line += "   " + info.copyright;
        content->add<Label>(line)->setColor(ui_->theme().textDim);
        for (const std::string& text : wrap(info.description, 62))
            content->add<Label>(text);
        if (header_.signedPackage)
            content->add<Label>("Firmado por el autor. Huella: " + zio::pack::fingerprint(header_.publicKey))
                ->setColor(rgb(0x4ade80));
        else
            content->add<Label>(std::string("Sin firma del autor"))->setColor(ui_->theme().textDim);
        content->add<Label>(std::string("Este juego está cifrado. Escribe la contraseña para jugar."));
        password_ = content->add<LineEdit>();
        password_->setPassword(true);
        password_->setPlaceholder("Contraseña");
        error_ = content->add<Label>(std::string());
        error_->setColor(ui_->theme().danger);
        dialog->setContent(std::move(content));
        dialog->setWidth(560.f);

        Dialog* raw  = dialog.get();
        auto confirm = [this, raw]() {
            if (unlock(password_->text()))
                raw->close();
        };
        dialog->addButton("Salir", [this, raw]() {
            raw->close();
            requestExit();
        });
        dialog->addButton("Jugar", confirm, true);
        dialog->setOnConfirm(confirm);
        dialog->setOnCancel([this]() { requestExit(); });
        password_->setOnSubmit([confirm](const std::string&) { confirm(); });
        ui_->openPopup(std::move(dialog));
        ui_->setFocus(password_);
    }

    bool unlock(const std::string& password)
    {
        std::uint8_t suffix[4];
        zio::pack::randomBytes(suffix, sizeof(suffix));
        char tag[16];
        std::snprintf(tag, sizeof(tag), "%02x%02x%02x%02x", suffix[0], suffix[1], suffix[2], suffix[3]);
        std::error_code ec;
        const fs::path dir = fs::temp_directory_path(ec) / ("ZioGame_" + header_.info.id.substr(0, 8) + "_" + tag);
        fs::create_directories(dir, ec);
        fs::permissions(dir, fs::perms::owner_all, fs::perm_options::replace, ec);

        zio::pack::OpenCredentials credentials;
        credentials.password = password;
        credentials.key      = args_.key;
        std::string error;
        if (!zio::pack::extractPackage(args_.package, dir, credentials, nullptr, &error))
        {
            removeTree(dir);
            if (error_)
                error_->setText(error);
            else
                showMessage("No se puede abrir el juego", error);
            std::fill(credentials.password.begin(), credentials.password.end(), '\0');
            return false;
        }
        std::fill(credentials.password.begin(), credentials.password.end(), '\0');
        extractDir_ = dir;
        if (!start(&error))
        {
            showMessage("No se puede iniciar el juego", error);
            return true;
        }
        return true;
    }

    bool start(std::string* error)
    {
        std::error_code ec;
        fs::current_path(extractDir_, ec);
        if (ec)
        {
            *error = "No se pudo preparar la carpeta del juego.";
            return false;
        }

        std::string scriptWarnings;
        for (const auto& language : zio::scripting::Languages::instance().all())
        {
            std::error_code exists;
            if (!fs::exists(language->outputPath(extractDir_), exists))
                continue;
            std::string loadError;
            if (!language->load(extractDir_, &loadError))
                scriptWarnings += (scriptWarnings.empty() ? "" : " ") + language->displayName() + ": " + loadError;
        }

        std::string entry = header_.info.entryScene;
        if (entry.empty())
        {
            const fs::path scenes = extractDir_ / "assets" / "scenes";
            for (const auto& file : fs::directory_iterator(scenes, ec))
                if (file.path().extension() == ".zscene3d")
                {
                    entry = "assets/scenes/" + file.path().filename().string();
                    break;
                }
        }
        if (entry.empty() || !zio::pack::isSafeArchivePath(entry))
        {
            *error = "El juego no tiene una escena inicial.";
            return false;
        }

        auto* scene = zio::scene::SceneStack::instance().run<zio::scene3d::Scene3D>();
        if (!zio::scene3d::loadScene3DFile(*scene, entry, error))
        {
            *error = "No se pudo cargar la escena inicial: " + *error;
            return false;
        }

        zio::platform::Window::instance().setTitle(header_.info.name + (header_.info.author.empty() ? "" : " - " + header_.info.author));

        std::string credits = header_.info.name;
        if (!header_.info.author.empty())
            credits += "\npor " + header_.info.author;
        credits += "\nHecho con Zio";
        auto* label = hud_.root().addChild<zio::scene2d::Label>(credits, std::string("Segoe UI"), 22.f);
        label->setAnchorPoint({0.f, 1.f});
        label->setPosition({24.f, static_cast<float>(zio::platform::Window::instance().height()) - 24.f});
        (void)scriptWarnings;
        phase_ = Phase::Playing;
        return true;
    }

    LaunchArgs args_;
    zio::pack::PackageHeader header_;
    bool headerOk_ = false;
    std::string headerError_;
    bool signatureOk_ = false;

    std::unique_ptr<zio::ui::Context> ui_;
    std::unique_ptr<zio::ui::EngineHost> host_;
    zio::ui::LineEdit* password_ = nullptr;
    zio::ui::Label* error_       = nullptr;
    zio::scene2d::Scene2D hud_;
    bool smoke_       = false;
    bool pendingExit_ = false;
    std::string smokePassword_;
    int smokeFrames_ = 0;
    int exitCode_    = 0;
    fs::path extractDir_;
    Phase phase_   = Phase::Dialog;
    float elapsed_ = 0.f;
};

std::optional<LaunchArgs> detectLaunch(int argc, char** argv)
{
    LaunchArgs args;
    bool explicitPackage = false;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--jugar" && i + 1 < argc)
        {
            args.package    = argv[++i];
            explicitPackage = true;
        }
        else if (arg == "--clave" && i + 1 < argc)
        {
            std::ifstream in(argv[++i], std::ios::binary);
            std::stringstream text;
            text << in.rdbuf();
            args.key = zio::pack::keyFromText(text.str());
        }
    }
    if (explicitPackage)
        return args;

    const fs::path self = selfPath();
    if (self.empty() || self.stem() == "zio_editor")
        return std::nullopt;
    const fs::path sibling = self.parent_path() / (self.stem().string() + ".zgame");
    std::error_code ec;
    if (!fs::exists(sibling, ec))
        return std::nullopt;
    args.package = sibling;
    return args;
}

int run(const LaunchArgs& args)
{
    zio::pack::PackageHeader header;
    std::string error;
    const bool headerOk = zio::pack::readHeader(args.package, header, &error);

    zio::core::AppConfig config;
    config.title        = headerOk && !header.info.name.empty() ? header.info.name : "Zio";
    config.windowWidth  = 1280;
    config.windowHeight = 720;
    config.targetFps    = 60;
    config.enableAudio  = true;

    PlayerApp app(args, header, headerOk, error);
    if (!app.run(config))
    {
        std::fprintf(stderr, "No se pudo iniciar la ventana o Vulkan.\n");
        return 4;
    }
    return app.exitCode();
}

}
