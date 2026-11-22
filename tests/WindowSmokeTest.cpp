#include <cstdio>
#include <cstdlib>
#include <string>

#include "zio/assets/Image.h"
#include "zio/core/Application.h"
#include "zio/input/InputManager.h"
#include "zio/platform/Window.h"
#include "zio/render/Device.h"
#include "zio/scene3d/Mesh.h"
#include "zio/scene3d/Renderer3D.h"
#include "zio/scene2d/Label.h"
#include "zio/ui/Font.h"

using namespace zio;

namespace
{

int failures = 0;

void check(bool condition, const char* name)
{
    std::printf("%s %s\n", condition ? "[ok]  " : "[FALLO]", name);
    if (!condition)
        ++failures;
}

bool required(const char* variable)
{
    const char* value = std::getenv(variable);
    return value && *value && *value != '0';
}

class Smoke final : public core::Application
{
public:
    int frames = 0;
    bool windowOpen = false;
    bool fontLoaded = false;
    bool labelDrawn = false;

    unsigned samples = 0;
    std::size_t culled = 0;
    std::size_t drawn  = 0;
    bool pixelRead     = false;
    int center[4]      = {};
    int centerInstanced[4] = {};
    int centerTextured[4]  = {};
    bool fullscreenSet     = false;
    bool fullscreenMode    = false;
    bool restoredMode      = false;
    std::uint32_t fullscreenWidth = 0;
    std::uint32_t restoredWidth   = 0;
    std::size_t instanced  = 0;

protected:
    bool onInit() override
    {
        samples    = render::Device::instance().sampleCount();
        windowOpen = platform::Window::instance().isOpen() && platform::Window::instance().width() > 0;
        auto font  = ui::createSystemFont("Arial", 16.f);
        fontLoaded = font != nullptr && font->lineHeight() > 0.f && font->textWidth("Hola") > 0.f;
        scene2d::Label label("Hola Zio", "Arial", 24.f);
        labelDrawn = label.contentSize().x > 0.f && label.contentSize().y > 0.f;
        return true;
    }

    void onRender(float) override
    {
        if (frames != 5 && frames != 6 && frames != 7)
            return;
        const bool second = frames == 6;
        const bool textured = frames == 7;
        auto& renderer = scene3d::Renderer3D::instance();
        if (!renderer.ensureInitialized())
            return;
        scene3d::Camera camera;
        camera.position = {0.f, 0.f, 5.f};
        camera.target   = {0.f, 0.f, 0.f};
        scene3d::Light light;
        const auto cube = scene3d::Mesh::cube(1.f);
        auto& device = render::Device::instance();
        device.beginFrame(render::ClearColor{0.f, 0.f, 0.f, 1.f});
        renderer.begin(camera, light, 640, 360);
        if (textured)
        {
            const std::uint8_t flat[4]  = {128, 128, 255, 255};
            const std::uint8_t white[4] = {255, 255, 255, 255};
            render::TextureDesc desc{};
            desc.width       = 1;
            desc.height      = 1;
            desc.pixelsRgba8 = flat;
            const auto normalMap = device.createTexture(desc);
            desc.pixelsRgba8     = white;
            const auto roughMap  = device.createTexture(desc);
            scene3d::MeshStyle style;
            style.normalTexture            = normalMap;
            style.metallicRoughnessTexture = roughMap;
            style.roughness                = 0.8f;
            renderer.drawMesh(*cube, math::Mat4::translation({0.f, 0.f, 0.f}), style);
            renderer.end();
            std::uint8_t px[4] = {};
            device.readBackPixel(320, 180, px);
            device.endFrame();
            for (int i = 0; i < 4; ++i)
                centerTextured[i] = px[i];
            device.destroyTexture(normalMap);
            device.destroyTexture(roughMap);
            return;
        }
        renderer.drawMesh(*cube, math::Mat4::translation({0.f, 0.f, 0.f}), renderer::Color::white());
        renderer.drawMesh(*cube, math::Mat4::translation({0.f, 0.f, 50.f}), renderer::Color::white());
        renderer.drawMesh(*cube, math::Mat4::translation({500.f, 0.f, 0.f}), renderer::Color::white());
        if (second)
            renderer.drawMesh(*cube, math::Mat4::translation({2.f, 0.f, 0.f}), renderer::Color::white());
        renderer.end();
        std::uint8_t px[4] = {};
        const bool read    = device.readBackPixel(320, 180, px);
        device.endFrame();
        if (!second)
        {
            culled    = renderer.culledLastFrame();
            drawn     = 1;
            pixelRead = read;
            for (int i = 0; i < 4; ++i)
                center[i] = px[i];
        }
        else
        {
            instanced = renderer.instancedLastFrame();
            for (int i = 0; i < 4; ++i)
                centerInstanced[i] = px[i];
        }
    }

    void onBeforeTick(float) override
    {
        input::InputManager::instance().isKeyDown(events::KeyCode::A);
        auto& window = platform::Window::instance();
        if (frames == 8)
            fullscreenSet = window.setMode(platform::WindowMode::Fullscreen);
        if (frames == 11)
        {
            fullscreenMode  = window.mode() == platform::WindowMode::Fullscreen && window.isFullscreen();
            fullscreenWidth = window.width();
        }
        if (frames == 12)
            window.toggleFullscreen();
        if (frames == 17)
        {
            restoredMode  = window.mode() == platform::WindowMode::Windowed && !window.isFullscreen();
            restoredWidth = window.width();
        }
        if (++frames >= 20)
            requestExit();
    }
};

}

int main()
{
    Smoke app;
    core::AppConfig config;
    config.title        = "zio smoke";
    config.windowWidth  = 640;
    config.windowHeight = 360;
    const bool ran      = app.run(config);

    if (!ran || !app.isRenderReady())
    {
        std::printf("[omitida] no hay ventana o Vulkan disponible en este equipo\n");
        return required("ZIO_REQUIRE_RENDER") ? 1 : 77;
    }

    check(app.windowOpen, "la ventana se abre con tamano");
    check(app.frames >= 20, "el bucle corre varios cuadros");
    check(app.samples == 4 || app.samples == 2 || app.samples == 1, "el antialiasing se activa o cae a 1 muestra");
    std::printf("muestras de antialiasing: %u\n", app.samples);
    std::printf("pixel central: %d %d %d %d\n", app.center[0], app.center[1], app.center[2], app.center[3]);
    check(app.drawn == 0 || (app.pixelRead && app.center[0] + app.center[1] + app.center[2] > 120), "el cubo se dibuja con color en el centro");
    std::printf("pixel central instanciado: %d %d %d %d (instancias: %zu)\n", app.centerInstanced[0],
                app.centerInstanced[1], app.centerInstanced[2], app.centerInstanced[3], app.instanced);
    check(app.drawn == 0 || app.instanced == 2, "dos cubos iguales se dibujan instanciados");
    check(app.drawn == 0 || (std::abs(app.center[0] - app.centerInstanced[0]) <= 2 &&
                             std::abs(app.center[1] - app.centerInstanced[1]) <= 2 &&
                             std::abs(app.center[2] - app.centerInstanced[2]) <= 2),
          "el dibujo instanciado coincide con el normal");
    check(app.drawn == 0 || app.centerTextured[0] + app.centerTextured[1] + app.centerTextured[2] > 120,
          "un material con mapa de normales y de rugosidad se dibuja");
    check(app.fullscreenSet && app.fullscreenMode && app.fullscreenWidth > 640, "la ventana pasa a pantalla completa");
    check(app.restoredMode && app.restoredWidth == 640, "la ventana vuelve a modo ventana con su tamano");
    check(app.drawn == 0 || app.culled == 2, "el descarte quita los objetos fuera de la camara");

    if (app.fontLoaded || required("ZIO_REQUIRE_FONT"))
    {
        check(app.fontLoaded, "la fuente del sistema carga y mide texto");
        check(app.labelDrawn, "un Label genera su textura");
    }
    else
    {
        std::printf("[omitida] no hay fuentes TrueType instaladas\n");
    }

#if defined(ZIO_SOURCE_DIR)
    const auto image = assets::decodeImageFile(std::string(ZIO_SOURCE_DIR) + "/assets/icon/zio.png");
    check(image.has_value() && image->width > 0 && image->pixelsRgba8.size() == static_cast<std::size_t>(image->width) * image->height * 4,
          "se decodifica un PNG");
#endif

    std::printf("%s\n", failures == 0 ? "TODO OK" : "HAY FALLOS");
    return failures == 0 ? 0 : 1;
}
