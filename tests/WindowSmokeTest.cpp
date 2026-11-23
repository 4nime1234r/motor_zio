#include <cmath>
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
    bool scaledFontOk      = false;
    float dpi              = 0.f;
    int centerInstanced[4] = {};
    int centerTextured[4]  = {};
    int centerShadowed[4]  = {};
    int skyPixel[4]        = {};
    int bloomPixel[4]      = {};
    int exposurePixel[4]   = {};
    int bloomEdge          = 0;
    std::size_t skinned    = 0;
    int skinMoved          = 0;
    int skinOrigin         = 0;
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
        auto scaledFont = ui::createScaledSystemFont("Arial", 16.f, false, 2.f);
        auto plainFont  = ui::createSystemFont("Arial", 16.f);
        scaledFontOk    = scaledFont && plainFont && std::fabs(scaledFont->size() - 16.f) < 0.01f &&
                       std::fabs(scaledFont->textWidth("Hola") - plainFont->textWidth("Hola")) < 3.f &&
                       std::fabs(scaledFont->lineHeight() - plainFont->lineHeight()) < 2.f;
        dpi = platform::Window::instance().dpiScale();
        return true;
    }

    void onRender(float) override
    {
        if (frames != 18 && frames != 1 && frames != 2 && frames != 3 && frames != 4 && frames != 5 && frames != 6 && frames != 7)
            return;
        const bool second = frames == 6;
        const bool textured = frames == 7;
        const bool shadowed = frames == 4;
        const bool skyFrame = frames == 3;
        const bool postFrame = frames == 1 || frames == 2;
        const bool skinFrame = frames == 18;
        auto& renderer = scene3d::Renderer3D::instance();
        if (!renderer.ensureInitialized())
            return;
        scene3d::Camera camera;
        camera.position = {0.f, 0.f, 5.f};
        camera.target   = {0.f, 0.f, 0.f};
        scene3d::Light light;
        light.shadows = shadowed || skinFrame;
        const auto cube = scene3d::Mesh::cube(1.f);
        auto& device = render::Device::instance();
        device.beginFrame(render::ClearColor{0.f, 0.f, 0.f, 1.f});
        renderer.begin(camera, light, 640, 360);
        if (skinFrame)
        {
            auto quad = std::make_shared<scene3d::Mesh>();
            const math::Vec3 corners[6] = {{-1.f, -1.f, 0.f}, {1.f, -1.f, 0.f}, {1.f, 1.f, 0.f},
                                           {-1.f, -1.f, 0.f}, {1.f, 1.f, 0.f},  {-1.f, 1.f, 0.f}};
            for (const math::Vec3& corner : corners)
                quad->vertices.push_back({corner, {0.f, 0.f, 1.f}, 0.f, 0.f});
            quad->skin = std::make_shared<scene3d::Skin>();
            scene3d::SkinJoint joint;
            quad->skin->joints.push_back(joint);
            scene3d::VertexSkin weights;
            weights.weights[0] = 1.f;
            quad->skin->vertices.assign(6, weights);
            std::vector<math::Mat4> bones = {math::Mat4::translation({3.f, 0.f, 0.f})};
            scene3d::MeshStyle style;
            style.bones = &bones;
            renderer.begin(camera, light, 640, 360);
            renderer.drawMesh(*quad, math::Mat4::identity(), style);
            renderer.end();
            skinned = renderer.skinnedLastFrame();
            std::uint8_t moved[4] = {};
            std::uint8_t origin[4] = {};
            device.readBackPixel(508, 180, moved);
            device.readBackPixel(320, 180, origin);
            device.endFrame();
            skinMoved  = moved[0] + moved[1] + moved[2];
            skinOrigin = origin[0] + origin[1] + origin[2];
            return;
        }
        if (postFrame)
        {
            scene3d::Environment env;
            env.bloom          = frames == 1;
            env.bloomThreshold = 0.2f;
            env.bloomIntensity = 3.f;
            env.exposure       = frames == 2 ? 0.5f : 1.f;
            renderer.setEnvironment(env);
            renderer.begin(camera, light, 640, 360);
            renderer.drawMesh(*cube, math::Mat4::translation({0.f, 0.f, 0.f}), renderer::Color::white());
            renderer.end();
            std::uint8_t px[4] = {};
            device.readBackPixel(320, 180, px);
            std::uint8_t edge[4] = {};
            device.readBackPixel(356, 180, edge);
            device.endFrame();
            for (int i = 0; i < 4; ++i)
                (frames == 1 ? bloomPixel : exposurePixel)[i] = px[i];
            if (frames == 1)
                bloomEdge = edge[0] + edge[1] + edge[2];
            renderer.setEnvironment(scene3d::Environment{});
            return;
        }
        if (skyFrame)
        {
            scene3d::Environment sky;
            sky.sky     = scene3d::SkyMode::Gradient;
            sky.zenith  = {1.f, 0.f, 0.f, 1.f};
            sky.horizon = {1.f, 0.f, 0.f, 1.f};
            sky.ground  = {1.f, 0.f, 0.f, 1.f};
            renderer.setEnvironment(sky);
            renderer.begin(camera, light, 640, 360);
            renderer.end();
            std::uint8_t px[4] = {};
            device.readBackPixel(20, 20, px);
            device.endFrame();
            for (int i = 0; i < 4; ++i)
                skyPixel[i] = px[i];
            renderer.setEnvironment(scene3d::Environment{});
            return;
        }
        if (shadowed)
        {
            renderer.drawMesh(*cube, math::Mat4::translation({0.f, 0.f, 0.f}), renderer::Color::white());
            renderer.drawMesh(*cube, math::Mat4::translation({2.f, 0.f, 0.f}), renderer::Color::white());
            renderer.drawMesh(*cube, math::Mat4::translation({0.f, -1.f, 0.f}) * math::Mat4::scale({6.f, 0.1f, 6.f}),
                              renderer::Color::white());
            renderer.end();
            std::uint8_t px[4] = {};
            device.readBackPixel(320, 180, px);
            device.endFrame();
            for (int i = 0; i < 4; ++i)
                centerShadowed[i] = px[i];
            return;
        }
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
    std::setvbuf(stdout, nullptr, _IONBF, 0);
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
    check(app.drawn == 0 || app.centerShadowed[0] + app.centerShadowed[1] + app.centerShadowed[2] > 120,
          "las sombras de mallas repetidas se dibujan sin romper la escena");
    check(app.drawn == 0 || (app.skyPixel[0] > 200 && app.skyPixel[1] < 40 && app.skyPixel[2] < 40),
          "el cielo degradado pinta el fondo");
    check(app.drawn == 0 || (app.exposurePixel[0] > 40 && app.exposurePixel[0] < 120),
          "el postproceso baja la exposicion de la imagen");
    check(app.drawn == 0 || app.bloomPixel[0] > 120, "el postproceso con bloom dibuja la escena");
    check(app.drawn == 0 || app.bloomEdge > 0, "el bloom hace brillar el borde de los objetos claros");
    check(app.drawn == 0 || app.skinned == 1, "una malla con huesos se dibuja con el esqueleto en la GPU");
    check(app.drawn == 0 || (app.skinMoved > 120 && app.skinOrigin < 30), "los huesos de la GPU mueven la malla");
    check(app.dpi >= 1.f, "la ventana informa su escala de pantalla");
    check(app.scaledFontOk || !app.fontLoaded, "una fuente rasterizada al doble mantiene las medidas logicas");
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
