#include "zio/scripting/DotNet.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>

#include "scripting/EmbeddedBridge.h"
#include "zio/audio/AudioManager.h"
#include "zio/audio/ClipCache.h"
#include "zio/input/InputManager.h"
#include "zio/math/Vec2.h"
#include "zio/physics3d/World3D.h"
#include "zio/scene3d/Node3D.h"
#include "zio/scripting/Script.h"

#if defined(ZIO_PLATFORM_WINDOWS)

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

namespace fs = std::filesystem;

namespace zio::scripting
{

namespace
{

using HostfxrHandle = void*;

struct HostfxrInitParams
{
    std::size_t size;
    const wchar_t* hostPath;
    const wchar_t* dotnetRoot;
};

using InitializeFn         = int(__cdecl*)(const wchar_t*, const HostfxrInitParams*, HostfxrHandle*);
using GetDelegateFn        = int(__cdecl*)(HostfxrHandle, int, void**);
using CloseFn              = int(__cdecl*)(HostfxrHandle);
using LoadAssemblyFn       = int(__stdcall*)(const wchar_t*, const wchar_t*, const wchar_t*, const wchar_t*, void*, void**);

constexpr int kLoadAssemblyAndGetFunctionPointer = 5;

struct NativeApi
{
    void(__cdecl* getPosition)(void*, float*);
    void(__cdecl* setPosition)(void*, const float*);
    void(__cdecl* getRotation)(void*, float*);
    void(__cdecl* setRotation)(void*, const float*);
    void(__cdecl* getScale)(void*, float*);
    void(__cdecl* setScale)(void*, const float*);
    int(__cdecl* getName)(void*, char*, int);
    void(__cdecl* log)(const char*, int, int);
    int(__cdecl* isKeyDown)(int);
    int(__cdecl* isMouseDown)(int);
    void(__cdecl* getMousePosition)(float*);
    void*(__cdecl* createNode)(void*, int, const char*, int);
    void(__cdecl* destroyNode)(void*);
    void*(__cdecl* findChild)(void*, const char*, int);
    void*(__cdecl* getParent)(void*);
    int(__cdecl* getChildCount)(void*);
    void*(__cdecl* getChild)(void*, int);
    void(__cdecl* setVisible)(void*, int);
    int(__cdecl* isVisible)(void*);
    void(__cdecl* setName)(void*, const char*, int);
    int(__cdecl* setMesh)(void*, const char*, int);
    void(__cdecl* setColor)(void*, const float*);
    void(__cdecl* setTexture)(void*, const char*, int);
    void(__cdecl* setLight)(void*, float, float);
    unsigned(__cdecl* createBody)(int, int, const float*, const float*, float, float, float);
    void(__cdecl* destroyBody)(unsigned);
    int(__cdecl* hasBody)(unsigned);
    void(__cdecl* getBodyVector)(unsigned, int, float*);
    void(__cdecl* setBodyVector)(unsigned, int, const float*);
    void(__cdecl* applyBody)(unsigned, int, const float*);
    void(__cdecl* getGravity)(float*);
    void(__cdecl* setGravity)(const float*);
    int(__cdecl* raycast)(const float*, const float*, float, float*, unsigned*);
    int(__cdecl* contactCount)();
    int(__cdecl* getContact)(int, unsigned*, unsigned*, float*);
    unsigned(__cdecl* playSound)(const char*, int, float, float, int, int);
    void(__cdecl* stopSound)(unsigned);
    void(__cdecl* setSound)(unsigned, int, float);
    int(__cdecl* isSoundPlaying)(unsigned);
    void(__cdecl* setCategoryVolume)(int, float);
    void(__cdecl* loadScene)(const char*, int);
    void*(__cdecl* instantiate)(void*, const char*, int);
};

scene3d::Node3D* asNode(void* node)
{
    return static_cast<scene3d::Node3D*>(node);
}

void __cdecl apiGetPosition(void* node, float* out)
{
    const math::Vec3 p = asNode(node)->position();
    out[0] = p.x;
    out[1] = p.y;
    out[2] = p.z;
}

void __cdecl apiSetPosition(void* node, const float* in)
{
    asNode(node)->setPosition({in[0], in[1], in[2]});
}

void __cdecl apiGetRotation(void* node, float* out)
{
    const math::Quaternion q = asNode(node)->rotation();
    out[0] = q.x;
    out[1] = q.y;
    out[2] = q.z;
    out[3] = q.w;
}

void __cdecl apiSetRotation(void* node, const float* in)
{
    asNode(node)->setRotation({in[0], in[1], in[2], in[3]});
}

void __cdecl apiGetScale(void* node, float* out)
{
    const math::Vec3 s = asNode(node)->scale();
    out[0] = s.x;
    out[1] = s.y;
    out[2] = s.z;
}

void __cdecl apiSetScale(void* node, const float* in)
{
    asNode(node)->setScale({in[0], in[1], in[2]});
}

int __cdecl apiGetName(void* node, char* buffer, int capacity)
{
    const std::string& name = asNode(node)->name();
    const int length        = static_cast<int>(std::min<std::size_t>(name.size(), static_cast<std::size_t>(std::max(capacity - 1, 0))));
    if (capacity > 0)
    {
        std::memcpy(buffer, name.data(), static_cast<std::size_t>(length));
        buffer[length] = '\0';
    }
    return length;
}

std::mutex gLogMutex;
std::vector<std::string> gPendingLog;

void __cdecl apiLog(const char* text, int length, int level)
{
    std::lock_guard<std::mutex> lock(gLogMutex);
    gPendingLog.push_back((level == 1 ? "[error] " : "") + std::string(text, static_cast<std::size_t>(length)));
    std::fprintf(stderr, "[C#] %.*s\n", length, text);
}

int __cdecl apiIsKeyDown(int key)
{
    return input::InputManager::instance().isKeyDown(static_cast<events::KeyCode>(key)) ? 1 : 0;
}

int __cdecl apiIsMouseDown(int button)
{
    return input::InputManager::instance().isMouseButtonDown(static_cast<events::MouseButton>(button)) ? 1 : 0;
}

void __cdecl apiGetMousePosition(float* out)
{
    const math::Vec2 p = input::InputManager::instance().mousePosition();
    out[0] = p.x;
    out[1] = p.y;
}

std::mutex gSceneMutex;
std::string gSceneRequest;

std::string text(const char* data, int length)
{
    return data && length > 0 ? std::string(data, static_cast<std::size_t>(length)) : std::string();
}

void* __cdecl apiCreateNode(void* parent, int type, const char* name, int length)
{
    if (!parent)
        return nullptr;
    std::unique_ptr<scene3d::Node3D> node;
    switch (type)
    {
    case 1:
        node = std::make_unique<scene3d::MeshNode>();
        break;
    case 2:
        node = std::make_unique<scene3d::LightNode>();
        break;
    default:
        node = std::make_unique<scene3d::Node3D>();
        break;
    }
    node->setName(text(name, length));
    return asNode(parent)->addChild(std::move(node));
}

void __cdecl apiDestroyNode(void* node)
{
    if (node)
        asNode(node)->markForDestroy();
}

scene3d::Node3D* searchChild(scene3d::Node3D* node, const std::string& name)
{
    for (const auto& child : node->children())
    {
        if (child->destroyPending())
            continue;
        if (child->name() == name)
            return child.get();
        if (auto* found = searchChild(child.get(), name))
            return found;
    }
    return nullptr;
}

void* __cdecl apiFindChild(void* node, const char* name, int length)
{
    return node ? searchChild(asNode(node), text(name, length)) : nullptr;
}

void* __cdecl apiGetParent(void* node)
{
    return node ? asNode(node)->parent() : nullptr;
}

int __cdecl apiGetChildCount(void* node)
{
    return node ? static_cast<int>(asNode(node)->children().size()) : 0;
}

void* __cdecl apiGetChild(void* node, int index)
{
    if (!node || index < 0 || static_cast<std::size_t>(index) >= asNode(node)->children().size())
        return nullptr;
    return asNode(node)->children()[static_cast<std::size_t>(index)].get();
}

void __cdecl apiSetVisible(void* node, int visible)
{
    if (node)
        asNode(node)->setVisible(visible != 0);
}

int __cdecl apiIsVisible(void* node)
{
    return node && asNode(node)->isVisible() ? 1 : 0;
}

void __cdecl apiSetName(void* node, const char* name, int length)
{
    if (node)
        asNode(node)->setName(text(name, length));
}

int __cdecl apiSetMesh(void* node, const char* source, int length)
{
    auto* mesh = node ? dynamic_cast<scene3d::MeshNode*>(asNode(node)) : nullptr;
    return mesh && mesh->setMeshSource(text(source, length)) ? 1 : 0;
}

void __cdecl apiSetColor(void* node, const float* rgba)
{
    if (!node)
        return;
    const renderer::Color color{rgba[0], rgba[1], rgba[2], rgba[3]};
    if (auto* mesh = dynamic_cast<scene3d::MeshNode*>(asNode(node)))
        mesh->setColor(color);
    else if (auto* light = dynamic_cast<scene3d::LightNode*>(asNode(node)))
        light->setColor(color);
}

void __cdecl apiSetTexture(void* node, const char* path, int length)
{
    if (auto* mesh = node ? dynamic_cast<scene3d::MeshNode*>(asNode(node)) : nullptr)
        mesh->setTexturePath(text(path, length));
}

void __cdecl apiSetLight(void* node, float intensity, float range)
{
    if (auto* light = node ? dynamic_cast<scene3d::LightNode*>(asNode(node)) : nullptr)
    {
        light->setIntensity(intensity);
        light->setRange(range);
    }
}

math::Vec3 vec3(const float* v)
{
    return {v[0], v[1], v[2]};
}

unsigned __cdecl apiCreateBody(int type, int shape, const float* shapeParams, const float* position, float mass,
                               float restitution, float friction)
{
    physics3d::BodyDesc desc;
    desc.type = type == 0 ? physics3d::BodyType::Static
                : type == 2 ? physics3d::BodyType::Kinematic
                            : physics3d::BodyType::Dynamic;
    desc.position    = vec3(position);
    desc.mass        = mass;
    desc.restitution = restitution;
    desc.friction    = friction;
    if (shape == 1)
        desc.shape = physics3d::BoxShape{vec3(shapeParams)};
    else
        desc.shape = physics3d::SphereShape{shapeParams[0]};
    return physics3d::World3D::instance().createBody(desc);
}

void __cdecl apiDestroyBody(unsigned body)
{
    physics3d::World3D::instance().destroyBody(body);
}

int __cdecl apiHasBody(unsigned body)
{
    return physics3d::World3D::instance().hasBody(body) ? 1 : 0;
}

void __cdecl apiGetBodyVector(unsigned body, int what, float* out)
{
    auto& world = physics3d::World3D::instance();
    if (what == 2)
    {
        const math::Quaternion q = world.rotation(body);
        out[0] = q.x;
        out[1] = q.y;
        out[2] = q.z;
        out[3] = q.w;
        return;
    }
    const math::Vec3 v = what == 0 ? world.position(body) : what == 1 ? world.linearVelocity(body) : world.angularVelocity(body);
    out[0] = v.x;
    out[1] = v.y;
    out[2] = v.z;
}

void __cdecl apiSetBodyVector(unsigned body, int what, const float* in)
{
    auto& world = physics3d::World3D::instance();
    switch (what)
    {
    case 0:
        world.setPosition(body, vec3(in));
        break;
    case 1:
        world.setLinearVelocity(body, vec3(in));
        break;
    case 2:
        world.setRotation(body, math::Quaternion{in[0], in[1], in[2], in[3]});
        break;
    default:
        world.setAngularVelocity(body, vec3(in));
        break;
    }
}

void __cdecl apiApplyBody(unsigned body, int what, const float* in)
{
    auto& world = physics3d::World3D::instance();
    if (what == 0)
        world.applyImpulse(body, vec3(in));
    else if (what == 1)
        world.applyForce(body, vec3(in));
    else
        world.applyTorque(body, vec3(in));
}

void __cdecl apiGetGravity(float* out)
{
    const math::Vec3 g = physics3d::World3D::instance().gravity();
    out[0] = g.x;
    out[1] = g.y;
    out[2] = g.z;
}

void __cdecl apiSetGravity(const float* in)
{
    physics3d::World3D::instance().setGravity(vec3(in));
}

int __cdecl apiRaycast(const float* origin, const float* direction, float maxDistance, float* out, unsigned* body)
{
    const auto hit = physics3d::World3D::instance().raycast(vec3(origin), vec3(direction), maxDistance);
    if (!hit)
        return 0;
    out[0] = hit->distance;
    out[1] = hit->point.x;
    out[2] = hit->point.y;
    out[3] = hit->point.z;
    out[4] = hit->normal.x;
    out[5] = hit->normal.y;
    out[6] = hit->normal.z;
    *body  = hit->body;
    return 1;
}

int __cdecl apiContactCount()
{
    return static_cast<int>(physics3d::World3D::instance().contacts().size());
}

int __cdecl apiGetContact(int index, unsigned* a, unsigned* b, float* out)
{
    const auto& contacts = physics3d::World3D::instance().contacts();
    if (index < 0 || static_cast<std::size_t>(index) >= contacts.size())
        return 0;
    const auto& c = contacts[static_cast<std::size_t>(index)];
    *a     = c.a;
    *b     = c.b;
    out[0] = c.point.x;
    out[1] = c.point.y;
    out[2] = c.point.z;
    out[3] = c.normal.x;
    out[4] = c.normal.y;
    out[5] = c.normal.z;
    out[6] = c.penetration;
    return 1;
}

unsigned __cdecl apiPlaySound(const char* path, int length, float volume, float pitch, int loop, int category)
{
    auto& manager = audio::AudioManager::instance();
    if (!manager.isInitialized())
        return 0;
    auto clip = audio::ClipCache::instance().loadSync(text(path, length));
    if (!clip)
        return 0;
    audio::PlayParams params;
    params.volume   = volume;
    params.pitch    = pitch;
    params.loop     = loop != 0;
    params.category = category == 1 ? audio::Category::Music : audio::Category::Sfx;
    return manager.play(std::move(clip), params);
}

void __cdecl apiStopSound(unsigned voice)
{
    audio::AudioManager::instance().stop(voice);
}

void __cdecl apiSetSound(unsigned voice, int what, float value)
{
    if (what == 0)
        audio::AudioManager::instance().setVolume(voice, value);
    else
        audio::AudioManager::instance().setPitch(voice, value);
}

int __cdecl apiIsSoundPlaying(unsigned voice)
{
    return audio::AudioManager::instance().isPlaying(voice) ? 1 : 0;
}

void __cdecl apiSetCategoryVolume(int category, float volume)
{
    audio::AudioManager::instance().setCategoryVolume(category == 1 ? audio::Category::Music : audio::Category::Sfx,
                                                       volume);
}

void* __cdecl apiInstantiate(void* parent, const char* path, int length)
{
    if (!parent)
        return nullptr;
    auto prefab = std::make_unique<scene3d::PrefabNode>();
    std::string error;
    if (!prefab->setSource(text(path, length), &error))
    {
        std::lock_guard<std::mutex> lock(gLogMutex);
        gPendingLog.push_back("[error] No se pudo instanciar el prefab: " + error);
        return nullptr;
    }
    return asNode(parent)->addChild(std::move(prefab));
}

void __cdecl apiLoadScene(const char* path, int length)
{
    std::lock_guard<std::mutex> lock(gSceneMutex);
    gSceneRequest = text(path, length);
}

fs::path findHostfxr()
{
    std::vector<fs::path> roots;
    if (const char* env = std::getenv("DOTNET_ROOT"))
        roots.push_back(fs::path(env));
    if (const char* env = std::getenv("ProgramFiles"))
        roots.push_back(fs::path(env) / "dotnet");
    roots.push_back("C:\\Program Files\\dotnet");

    for (const fs::path& root : roots)
    {
        std::error_code ec;
        const fs::path fxr = root / "host" / "fxr";
        if (!fs::is_directory(fxr, ec))
            continue;
        fs::path best;
        std::vector<int> bestVersion;
        for (const auto& entry : fs::directory_iterator(fxr, ec))
        {
            if (!entry.is_directory())
                continue;
            std::vector<int> version;
            std::string text = entry.path().filename().string();
            std::size_t pos  = 0;
            while (pos < text.size())
            {
                std::size_t used = 0;
                try
                {
                    version.push_back(std::stoi(text.substr(pos), &used));
                }
                catch (...)
                {
                    break;
                }
                pos += used;
                if (pos < text.size() && text[pos] == '.')
                    ++pos;
                else
                    break;
            }
            if (version.empty() || !fs::exists(entry.path() / "hostfxr.dll", ec))
                continue;
            if (best.empty() || version > bestVersion)
            {
                best        = entry.path() / "hostfxr.dll";
                bestVersion = version;
            }
        }
        if (!best.empty())
            return best;
    }
    return {};
}

std::wstring quoted(const fs::path& path)
{
    return L"\"" + path.wstring() + L"\"";
}

}

struct DotNetRuntime::Impl
{
    HMODULE hostfxr = nullptr;
    LoadAssemblyFn loadAssembly = nullptr;
    fs::path workDir;
    fs::path bridge;
    bool running = false;
    unsigned generation = 1;
    std::vector<std::string> names;

    int(__stdcall* initialize)(void*)                     = nullptr;
    int(__stdcall* loadScripts)(const wchar_t*)           = nullptr;
    int(__stdcall* listScripts)(wchar_t*, int)            = nullptr;
    int(__stdcall* createScript)(const wchar_t*)          = nullptr;
    void(__stdcall* startScript)(int, void*)              = nullptr;
    void(__stdcall* updateScript)(int, float)             = nullptr;
    void(__stdcall* destroyScript)(int)                   = nullptr;

    NativeApi api{};

    template <class Fn>
    bool bind(const wchar_t* method, Fn& target, std::string* error)
    {
        void* fn = nullptr;
        const int rc = loadAssembly(bridge.c_str(), L"Zio.Bridge, ZioScripting", method, reinterpret_cast<const wchar_t*>(-1), nullptr, &fn);
        if (rc != 0 || !fn)
        {
            if (error)
                *error = "No se encontro el metodo administrado " + std::string(method, method + wcslen(method));
            return false;
        }
        target = reinterpret_cast<Fn>(fn);
        return true;
    }

    void refreshNames()
    {
        names.clear();
        if (!listScripts)
            return;
        const int needed = listScripts(nullptr, 0);
        if (needed <= 0)
            return;
        std::wstring buffer(static_cast<std::size_t>(needed) + 1, L'\0');
        listScripts(buffer.data(), needed + 1);
        std::string joined;
        for (const wchar_t c : buffer.substr(0, static_cast<std::size_t>(needed)))
            joined += c < 128 ? static_cast<char>(c) : '_';
        std::size_t start = 0;
        while (start <= joined.size())
        {
            std::size_t end = joined.find('\n', start);
            if (end == std::string::npos)
                end = joined.size();
            if (end > start)
                names.push_back(joined.substr(start, end - start));
            start = end + 1;
        }
    }
};

namespace
{

class ManagedScript final : public Script
{
public:
    explicit ManagedScript(std::string name) : name_(std::move(name)) {}

    void onStart() override
    {
        auto& rt   = DotNetRuntime::instance();
        generation_ = rt.generation();
        handle_     = rt.createScript(name_);
        if (handle_ != 0)
            rt.startScript(handle_, &node());
    }

    void onUpdate(float dt) override
    {
        auto& rt = DotNetRuntime::instance();
        if (handle_ != 0 && generation_ == rt.generation())
            rt.updateScript(handle_, dt);
    }

    void onDestroy() override
    {
        auto& rt = DotNetRuntime::instance();
        if (handle_ != 0 && generation_ == rt.generation())
            rt.destroyScript(handle_);
        handle_ = 0;
    }

private:
    std::string name_;
    int handle_         = 0;
    unsigned generation_ = 0;
};

}

DotNetRuntime::DotNetRuntime() : impl_(std::make_unique<Impl>()) {}
DotNetRuntime::~DotNetRuntime() = default;

DotNetRuntime& DotNetRuntime::instance()
{
    static DotNetRuntime runtime;
    return runtime;
}

bool DotNetRuntime::embeddedBridgeAvailable()
{
    int count = 0;
    const EmbeddedFile* files = embeddedBridgeFiles(count);
    return count > 0 && files[0].size > 1;
}

bool DotNetRuntime::running() const
{
    return impl_->running;
}

unsigned DotNetRuntime::generation() const
{
    return impl_->generation;
}

std::filesystem::path DotNetRuntime::bridgePath() const
{
    return impl_->bridge;
}

std::vector<std::string> DotNetRuntime::scriptNames() const
{
    return impl_->names;
}

bool DotNetRuntime::start(const fs::path& workDir, std::string* error)
{
    const auto fail = [&](const std::string& message) {
        if (error)
            *error = message;
        return false;
    };
    if (impl_->running)
        return true;
    if (!embeddedBridgeAvailable())
        return fail("Esta compilacion de Zio no incluye el puente de C# (faltaba el SDK de .NET al compilar).");

    std::error_code ec;
    fs::create_directories(workDir, ec);
    int count                 = 0;
    const EmbeddedFile* files = embeddedBridgeFiles(count);
    for (int i = 0; i < count; ++i)
    {
        const fs::path target = workDir / files[i].name;
        bool same             = false;
        if (fs::exists(target, ec) && fs::file_size(target, ec) == files[i].size)
        {
            std::ifstream in(target, std::ios::binary);
            std::string existing((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            same = existing.size() == files[i].size && std::memcmp(existing.data(), files[i].data, files[i].size) == 0;
        }
        if (same)
            continue;
        std::ofstream out(target, std::ios::binary | std::ios::trunc);
        if (!out)
            return fail("No se pudo escribir " + target.string());
        out.write(reinterpret_cast<const char*>(files[i].data), static_cast<std::streamsize>(files[i].size));
    }
    impl_->workDir = workDir;
    impl_->bridge  = workDir / "ZioScripting.dll";

    const fs::path fxrPath = findHostfxr();
    if (fxrPath.empty())
        return fail("No se encontro el runtime de .NET. Instala el runtime de .NET 8 o superior.");
    impl_->hostfxr = LoadLibraryW(fxrPath.c_str());
    if (!impl_->hostfxr)
        return fail("No se pudo cargar hostfxr.dll");

    auto init   = reinterpret_cast<InitializeFn>(GetProcAddress(impl_->hostfxr, "hostfxr_initialize_for_runtime_config"));
    auto getDel = reinterpret_cast<GetDelegateFn>(GetProcAddress(impl_->hostfxr, "hostfxr_get_runtime_delegate"));
    auto close  = reinterpret_cast<CloseFn>(GetProcAddress(impl_->hostfxr, "hostfxr_close"));
    if (!init || !getDel || !close)
        return fail("hostfxr.dll no es compatible");

    HostfxrHandle context = nullptr;
    const fs::path config = workDir / "ZioScripting.runtimeconfig.json";
    int rc                = init(config.c_str(), nullptr, &context);
    if ((rc != 0 && rc != 0x00000001 && rc != 0x00000002) || !context)
    {
        if (context)
            close(context);
        return fail("No se pudo iniciar el runtime de .NET (codigo " + std::to_string(rc) + "). Instala el runtime de .NET 8 o superior.");
    }
    void* loader = nullptr;
    rc           = getDel(context, kLoadAssemblyAndGetFunctionPointer, &loader);
    close(context);
    if (rc != 0 || !loader)
        return fail("No se pudo obtener el cargador de .NET (codigo " + std::to_string(rc) + ")");
    impl_->loadAssembly = reinterpret_cast<LoadAssemblyFn>(loader);

    std::string bindError;
    if (!impl_->bind(L"Initialize", impl_->initialize, &bindError) ||
        !impl_->bind(L"LoadScripts", impl_->loadScripts, &bindError) ||
        !impl_->bind(L"ListScripts", impl_->listScripts, &bindError) ||
        !impl_->bind(L"CreateScript", impl_->createScript, &bindError) ||
        !impl_->bind(L"StartScript", impl_->startScript, &bindError) ||
        !impl_->bind(L"UpdateScript", impl_->updateScript, &bindError) ||
        !impl_->bind(L"DestroyScript", impl_->destroyScript, &bindError))
        return fail(bindError);

    impl_->api.getPosition      = apiGetPosition;
    impl_->api.setPosition      = apiSetPosition;
    impl_->api.getRotation      = apiGetRotation;
    impl_->api.setRotation      = apiSetRotation;
    impl_->api.getScale         = apiGetScale;
    impl_->api.setScale         = apiSetScale;
    impl_->api.getName          = apiGetName;
    impl_->api.log              = apiLog;
    impl_->api.isKeyDown        = apiIsKeyDown;
    impl_->api.isMouseDown      = apiIsMouseDown;
    impl_->api.getMousePosition = apiGetMousePosition;
    impl_->api.createNode        = apiCreateNode;
    impl_->api.destroyNode       = apiDestroyNode;
    impl_->api.findChild         = apiFindChild;
    impl_->api.getParent         = apiGetParent;
    impl_->api.getChildCount     = apiGetChildCount;
    impl_->api.getChild          = apiGetChild;
    impl_->api.setVisible        = apiSetVisible;
    impl_->api.isVisible         = apiIsVisible;
    impl_->api.setName           = apiSetName;
    impl_->api.setMesh           = apiSetMesh;
    impl_->api.setColor          = apiSetColor;
    impl_->api.setTexture        = apiSetTexture;
    impl_->api.setLight          = apiSetLight;
    impl_->api.createBody        = apiCreateBody;
    impl_->api.destroyBody       = apiDestroyBody;
    impl_->api.hasBody           = apiHasBody;
    impl_->api.getBodyVector     = apiGetBodyVector;
    impl_->api.setBodyVector     = apiSetBodyVector;
    impl_->api.applyBody         = apiApplyBody;
    impl_->api.getGravity        = apiGetGravity;
    impl_->api.setGravity        = apiSetGravity;
    impl_->api.raycast           = apiRaycast;
    impl_->api.contactCount      = apiContactCount;
    impl_->api.getContact        = apiGetContact;
    impl_->api.playSound         = apiPlaySound;
    impl_->api.stopSound         = apiStopSound;
    impl_->api.setSound          = apiSetSound;
    impl_->api.isSoundPlaying    = apiIsSoundPlaying;
    impl_->api.setCategoryVolume = apiSetCategoryVolume;
    impl_->api.loadScene         = apiLoadScene;
    impl_->api.instantiate       = apiInstantiate;
    impl_->initialize(&impl_->api);

    impl_->running = true;

    Registry::instance().setProvider(
        "dotnet",
        []() { return DotNetRuntime::instance().scriptNames(); },
        [](const std::string& name) -> std::unique_ptr<Script> {
            const auto names = DotNetRuntime::instance().scriptNames();
            if (std::find(names.begin(), names.end(), name) == names.end())
                return nullptr;
            return std::make_unique<ManagedScript>(name);
        });
    return true;
}

bool DotNetRuntime::loadScripts(const fs::path& scriptsDll, std::string* error)
{
    if (!impl_->running)
    {
        if (error)
            *error = "El runtime de C# no esta iniciado.";
        return false;
    }
    const int result = impl_->loadScripts(scriptsDll.c_str());
    ++impl_->generation;
    if (result < 0)
    {
        impl_->names.clear();
        Registry::instance().notifyChanged();
        if (error)
            *error = "No se pudieron cargar los scripts de " + scriptsDll.string();
        return false;
    }
    impl_->refreshNames();
    Registry::instance().notifyChanged();
    return true;
}

int DotNetRuntime::createScript(const std::string& name)
{
    if (!impl_->running)
        return 0;
    const std::wstring wide(name.begin(), name.end());
    return impl_->createScript(wide.c_str());
}

void DotNetRuntime::startScript(int handle, void* node)
{
    if (impl_->running)
        impl_->startScript(handle, node);
}

void DotNetRuntime::updateScript(int handle, float dt)
{
    if (impl_->running)
        impl_->updateScript(handle, dt);
}

void DotNetRuntime::destroyScript(int handle)
{
    if (impl_->running)
        impl_->destroyScript(handle);
}

std::vector<std::string> DotNetRuntime::takeLog()
{
    std::lock_guard<std::mutex> lock(gLogMutex);
    std::vector<std::string> out;
    out.swap(gPendingLog);
    return out;
}

std::string DotNetRuntime::takeSceneRequest()
{
    std::lock_guard<std::mutex> lock(gSceneMutex);
    std::string out;
    out.swap(gSceneRequest);
    return out;
}

namespace
{

fs::path findDotnet()
{
    wchar_t buffer[MAX_PATH];
    if (SearchPathW(nullptr, L"dotnet", L".exe", MAX_PATH, buffer, nullptr) != 0)
        return buffer;
    for (const char* variable : {"ProgramFiles", "ProgramFiles(x86)"})
    {
        const char* root = std::getenv(variable);
        if (!root || !*root)
            continue;
        const fs::path candidate = fs::path(root) / "dotnet" / "dotnet.exe";
        std::error_code ec;
        if (fs::exists(candidate, ec))
            return candidate;
    }
    return {};
}

}

bool DotNetRuntime::dotnetSdkAvailable()
{
    return !findDotnet().empty();
}

fs::path DotNetRuntime::scriptsDllPath(const fs::path& projectDir)
{
    return projectDir / ".zio" / "bin" / "Scripts.dll";
}

bool DotNetRuntime::installSdk(std::string* error)
{
    const HINSTANCE result =
        ShellExecuteW(nullptr, L"open", L"cmd.exe", L"/k winget install --id Microsoft.DotNet.SDK.8 -e", nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32)
    {
        if (error)
            *error = "No se pudo iniciar winget.";
        return false;
    }
    return true;
}

namespace
{

bool writeProjectFiles(const fs::path& projectDir, const fs::path& bridge)
{
    std::error_code ec;
    fs::create_directories(projectDir / ".zio", ec);
    fs::copy_file(bridge, projectDir / ".zio" / "ZioScripting.dll", fs::copy_options::overwrite_existing, ec);
    std::ofstream csproj(projectDir / "Scripts.csproj", std::ios::binary | std::ios::trunc);
    if (!csproj)
        return false;
    csproj << "<Project>\n"
              "  <Import Project=\"Sdk.props\" Sdk=\"Microsoft.NET.Sdk\" />\n"
              "  <PropertyGroup>\n"
              "    <TargetFramework>net8.0</TargetFramework>\n"
              "    <AssemblyName>Scripts</AssemblyName>\n"
              "    <Nullable>disable</Nullable>\n"
              "    <ImplicitUsings>disable</ImplicitUsings>\n"
              "    <AllowUnsafeBlocks>true</AllowUnsafeBlocks>\n"
              "    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>\n"
              "    <RollForward>LatestMajor</RollForward>\n"
              "    <DebugType>none</DebugType>\n"
              "    <BaseIntermediateOutputPath>.zio\\obj\\</BaseIntermediateOutputPath>\n"
              "  </PropertyGroup>\n"
              "  <ItemGroup>\n"
              "    <Compile Include=\"assets\\scripts\\**\\*.cs\" />\n"
              "    <Reference Include=\"ZioScripting\">\n"
              "      <HintPath>.zio\\ZioScripting.dll</HintPath>\n"
              "      <Private>false</Private>\n"
              "    </Reference>\n"
              "  </ItemGroup>\n"
              "  <Import Project=\"Sdk.targets\" Sdk=\"Microsoft.NET.Sdk\" />\n"
              "</Project>\n";
    return static_cast<bool>(csproj);
}

}

void DotNetRuntime::prepareProject(const fs::path& projectDir)
{
    auto& rt = instance();
    if (!rt.impl_->bridge.empty() && fs::exists(rt.impl_->bridge))
        writeProjectFiles(projectDir, rt.impl_->bridge);
}

bool DotNetRuntime::buildScripts(const fs::path& projectDir, const std::function<void(const std::string&)>& onLine,
                                 std::string* error)
{
    const auto fail = [&](const std::string& message) {
        if (error)
            *error = message;
        return false;
    };
    auto& rt = instance();
    if (rt.impl_->bridge.empty() || !fs::exists(rt.impl_->bridge))
        return fail("El puente de C# no esta preparado.");
    if (!dotnetSdkAvailable())
        return fail("Para compilar scripts de C# hace falta el SDK de .NET 8 o superior (comando dotnet).");

    const fs::path work = projectDir / ".zio";
    std::error_code ec;
    fs::create_directories(work / "bin", ec);
    if (!writeProjectFiles(projectDir, rt.impl_->bridge))
        return fail("No se pudo escribir Scripts.csproj");

    SetEnvironmentVariableW(L"DOTNET_NOLOGO", L"1");
    SetEnvironmentVariableW(L"DOTNET_CLI_TELEMETRY_OPTOUT", L"1");
    SetEnvironmentVariableW(L"DOTNET_SKIP_FIRST_TIME_EXPERIENCE", L"1");

    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE readPipe  = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &security, 0))
        return fail("No se pudo crear la tuberia para dotnet");
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    std::wstring command = quoted(findDotnet()) + L" build " + quoted(projectDir / "Scripts.csproj") + L" -c Release -o " +
                           quoted(work / "bin") + L" --nologo -v q -nodeReuse:false";
    STARTUPINFOW startup{};
    startup.cb         = sizeof(startup);
    startup.dwFlags    = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdOutput = writePipe;
    startup.hStdError  = writePipe;
    PROCESS_INFORMATION process{};
    const BOOL created = CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                                        work.c_str(), &startup, &process);
    CloseHandle(writePipe);
    if (!created)
    {
        CloseHandle(readPipe);
        return fail("No se pudo ejecutar dotnet");
    }

    std::string pending;
    char buffer[4096];
    DWORD read = 0;
    while (ReadFile(readPipe, buffer, sizeof(buffer), &read, nullptr) && read > 0)
    {
        pending.append(buffer, read);
        std::size_t newline;
        while ((newline = pending.find('\n')) != std::string::npos)
        {
            std::string line = pending.substr(0, newline);
            pending.erase(0, newline + 1);
            while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
                line.pop_back();
            if (!line.empty() && onLine)
                onLine(line);
        }
    }
    if (!pending.empty() && onLine)
        onLine(pending);
    CloseHandle(readPipe);

    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);

    if (exitCode != 0)
        return fail("La compilacion de los scripts de C# fallo.");
    if (!fs::exists(scriptsDllPath(projectDir), ec))
        return fail("La compilacion termino pero no se genero Scripts.dll.");
    return true;
}

}

#else

namespace zio::scripting
{

struct DotNetRuntime::Impl
{
};

DotNetRuntime::DotNetRuntime() : impl_(std::make_unique<Impl>()) {}
DotNetRuntime::~DotNetRuntime() = default;
DotNetRuntime& DotNetRuntime::instance()
{
    static DotNetRuntime runtime;
    return runtime;
}
bool DotNetRuntime::embeddedBridgeAvailable() { return false; }
bool DotNetRuntime::start(const std::filesystem::path&, std::string* error)
{
    if (error)
        *error = "Los scripts de C# solo estan disponibles en Windows.";
    return false;
}
bool DotNetRuntime::running() const { return false; }
bool DotNetRuntime::loadScripts(const std::filesystem::path&, std::string*) { return false; }
std::vector<std::string> DotNetRuntime::scriptNames() const { return {}; }
unsigned DotNetRuntime::generation() const { return 0; }
std::filesystem::path DotNetRuntime::bridgePath() const { return {}; }
bool DotNetRuntime::dotnetSdkAvailable() { return false; }
bool DotNetRuntime::buildScripts(const std::filesystem::path&, const std::function<void(const std::string&)>&, std::string*) { return false; }
std::filesystem::path DotNetRuntime::scriptsDllPath(const std::filesystem::path& projectDir) { return projectDir / ".zio" / "bin" / "Scripts.dll"; }
bool DotNetRuntime::installSdk(std::string* error)
{
    if (error)
        *error = "Los scripts de C# solo estan disponibles en Windows.";
    return false;
}
void DotNetRuntime::prepareProject(const std::filesystem::path&) {}
std::vector<std::string> DotNetRuntime::takeLog() { return {}; }
std::string DotNetRuntime::takeSceneRequest() { return {}; }
int DotNetRuntime::createScript(const std::string&) { return 0; }
void DotNetRuntime::startScript(int, void*) {}
void DotNetRuntime::updateScript(int, float) {}
void DotNetRuntime::destroyScript(int) {}

}

#endif
