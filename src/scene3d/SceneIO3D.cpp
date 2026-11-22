#include "zio/scene3d/SceneIO3D.h"

#include <cmath>
#include <fstream>
#include <memory>
#include <sstream>

#include "internal/Json.h"

namespace zio::scene3d
{

namespace
{

using namespace json;

void writeNums(std::string& out, const float* v, int n)
{
    out += '[';
    for (int i = 0; i < n; ++i)
    {
        if (i)
            out += ", ";
        writeNumber(out, v[i]);
    }
    out += ']';
}

void writeVec3(std::string& out, math::Vec3 v)
{
    const float a[3] = {v.x, v.y, v.z};
    writeNums(out, a, 3);
}

void indent(std::string& out, int level)
{
    out.append(static_cast<std::size_t>(level) * 2, ' ');
}

float roundDegrees(float v)
{
    const float r = std::round(v * 10000.f) / 10000.f;
    return r == 0.f ? 0.f : r;
}

void field(std::string& out, int level, const char* key)
{
    indent(out, level);
    out += '"';
    out += key;
    out += "\": ";
}

void writeNode(std::string& out, const Node3D& node, int level)
{
    const auto* mesh = dynamic_cast<const MeshNode*>(&node);
    const int in     = level + 1;

    out += "{\n";
    field(out, in, "type");
    const auto* lightNode  = dynamic_cast<const LightNode*>(&node);
    const auto* prefabNode = dynamic_cast<const PrefabNode*>(&node);
    writeString(out, mesh ? "Mesh" : (lightNode ? "Light" : (prefabNode ? "Prefab" : "Node3D")));
    out += ",\n";
    field(out, in, "name");
    writeString(out, node.name());
    out += ",\n";
    field(out, in, "position");
    writeVec3(out, node.position());
    out += ",\n";
    field(out, in, "rotationDegrees");
    const math::Vec3 e = node.rotation().toEulerDegrees();
    const float ea[3]  = {roundDegrees(e.x), roundDegrees(e.y), roundDegrees(e.z)};
    writeNums(out, ea, 3);
    out += ",\n";
    field(out, in, "scale");
    writeVec3(out, node.scale());
    out += ",\n";
    field(out, in, "visible");
    out += node.isVisible() ? "true" : "false";

    if (mesh)
    {
        out += ",\n";
        field(out, in, "mesh");
        writeString(out, mesh->meshSource());
        out += ",\n";
        field(out, in, "color");
        const renderer::Color c = mesh->color();
        const float ca[4]       = {c.r, c.g, c.b, c.a};
        writeNums(out, ca, 4);
        out += ",\n";
        field(out, in, "texture");
        writeString(out, mesh->texturePath());
        out += ",\n";
        field(out, in, "uvScale");
        const float uv[2] = {mesh->uvScale().x, mesh->uvScale().y};
        writeNums(out, uv, 2);
        out += ",\n";
        field(out, in, "castShadow");
        out += mesh->castShadow() ? "true" : "false";
        if (mesh->isAnimationPlaying())
        {
            out += ",\n";
            field(out, in, "animation");
            writeString(out, mesh->currentAnimation());
            out += ",\n";
            field(out, in, "animationLoop");
            out += mesh->animationLoops() ? "true" : "false";
            out += ",\n";
            field(out, in, "animationSpeed");
            writeNumber(out, mesh->animationSpeed());
        }
    }

    if (prefabNode)
    {
        out += ",\n";
        field(out, in, "source");
        writeString(out, prefabNode->source());
    }

    if (lightNode)
    {
        out += ",\n";
        field(out, in, "light");
        writeString(out, lightNode->lightType() == LightType::Spot ? "spot" : "point");
        out += ",\n";
        field(out, in, "color");
        const renderer::Color c = lightNode->color();
        const float ca[3]       = {c.r, c.g, c.b};
        writeNums(out, ca, 3);
        out += ",\n";
        field(out, in, "intensity");
        writeNumber(out, lightNode->intensity());
        out += ",\n";
        field(out, in, "range");
        writeNumber(out, lightNode->range());
        out += ",\n";
        field(out, in, "innerAngle");
        writeNumber(out, lightNode->innerAngle());
        out += ",\n";
        field(out, in, "outerAngle");
        writeNumber(out, lightNode->outerAngle());
        out += ",\n";
        field(out, in, "castShadow");
        out += lightNode->castShadow() ? "true" : "false";
        out += ",\n";
        field(out, in, "shadowOpacity");
        writeNumber(out, lightNode->shadowOpacity());
    }

    if (!node.scripts().empty())
    {
        out += ",\n";
        field(out, in, "scripts");
        out += '[';
        bool firstScript = true;
        for (const auto& slot : node.scripts())
        {
            out += firstScript ? "" : ", ";
            firstScript = false;
            writeString(out, slot.name);
        }
        out += ']';
    }

    if (!node.children().empty() && !prefabNode)
    {
        out += ",\n";
        field(out, in, "children");
        out += "[\n";
        bool first = true;
        for (const auto& child : node.children())
        {
            if (!first)
                out += ",\n";
            first = false;
            indent(out, in + 1);
            writeNode(out, *child, in + 1);
        }
        out += '\n';
        indent(out, in);
        out += ']';
    }

    out += '\n';
    indent(out, level);
    out += '}';
}

struct Reader : json::Fields
{
    static math::Vec3 vec3(const double* n) { return {static_cast<float>(n[0]), static_cast<float>(n[1]), static_cast<float>(n[2])}; }

    bool camera(const Value& doc, Camera& cam)
    {
        const Value* o = doc.find("camera");
        if (!o)
            return true;
        if (!std::holds_alternative<Object>(o->v))
            return fail("camera", "must be an object");
        double n[3] = {};
        double d    = 0;
        bool has    = false;
        if (!getNumbers(*o, "position", "camera", 3, n, has))
            return false;
        if (has)
            cam.position = vec3(n);
        if (!getNumbers(*o, "target", "camera", 3, n, has))
            return false;
        if (has)
            cam.target = vec3(n);
        if (!getNumbers(*o, "up", "camera", 3, n, has))
            return false;
        if (has)
            cam.up = vec3(n);
        if (!getNumber(*o, "fov", "camera", d, has))
            return false;
        if (has)
            cam.fovYDegrees = static_cast<float>(d);
        if (!getNumber(*o, "near", "camera", d, has))
            return false;
        if (has)
            cam.nearZ = static_cast<float>(d);
        if (!getNumber(*o, "far", "camera", d, has))
            return false;
        if (has)
            cam.farZ = static_cast<float>(d);

        if (!(cam.fovYDegrees > 0.f && cam.fovYDegrees < 180.f))
            return fail("camera", "\"fov\" must be between 0 and 180 degrees");
        if (!(cam.nearZ > 0.f && cam.farZ > cam.nearZ))
            return fail("camera", "needs 0 < \"near\" < \"far\"");
        if ((cam.target - cam.position).length() == 0.f)
            return fail("camera", "\"position\" and \"target\" must differ");
        if (cam.up.length() == 0.f || (cam.target - cam.position).normalized().cross(cam.up).length() == 0.f)
            return fail("camera", "\"up\" must not be zero or parallel to the view direction");
        return true;
    }

    bool light(const Value& doc, Light& l)
    {
        const Value* o = doc.find("light");
        if (!o)
            return true;
        if (!std::holds_alternative<Object>(o->v))
            return fail("light", "must be an object");
        double n[3] = {};
        double d    = 0;
        bool has    = false;
        if (!getNumbers(*o, "direction", "light", 3, n, has))
            return false;
        if (has)
            l.direction = vec3(n);
        if (!getNumber(*o, "ambient", "light", d, has))
            return false;
        if (has)
            l.ambient = static_cast<float>(d);
        bool b = false;
        if (!getBool(*o, "shadows", "light", b, has))
            return false;
        if (has)
            l.shadows = b;
        if (!getNumber(*o, "shadowOpacity", "light", d, has))
            return false;
        if (has)
            l.shadowOpacity = static_cast<float>(d);
        return true;
    }

    bool color3(const Value& o, const char* key, const std::string& path, renderer::Color& out)
    {
        double n[3] = {};
        bool has    = false;
        if (!getNumbers(o, key, path, 3, n, has))
            return false;
        if (has)
            out = {static_cast<float>(n[0]), static_cast<float>(n[1]), static_cast<float>(n[2]), 1.f};
        return true;
    }

    bool number(const Value& o, const char* key, const std::string& path, float& out)
    {
        double d = 0;
        bool has = false;
        if (!getNumber(o, key, path, d, has))
            return false;
        if (has)
            out = static_cast<float>(d);
        return true;
    }

    bool environment(const Value& doc, Environment& env)
    {
        const Value* o = doc.find("environment");
        if (!o)
            return true;
        if (!std::holds_alternative<Object>(o->v))
            return fail("environment", "must be an object");
        const std::string path = "environment";
        std::string text;
        bool has = false;
        if (!getString(*o, "sky", path, text, has))
            return false;
        if (has)
            env.sky = text == "gradient" ? SkyMode::Gradient : (text == "panorama" ? SkyMode::Panorama : SkyMode::None);
        if (!getString(*o, "panorama", path, text, has))
            return false;
        if (has)
            env.panorama = text;
        if (!getString(*o, "fog", path, text, has))
            return false;
        if (has)
            env.fog = text == "linear" ? FogMode::Linear : (text == "exponential" ? FogMode::Exponential : FogMode::None);
        bool flag = false;
        if (!getBool(*o, "bloom", path, flag, has))
            return false;
        if (has)
            env.bloom = flag;
        return color3(*o, "zenith", path, env.zenith) && color3(*o, "horizon", path, env.horizon) &&
               color3(*o, "ground", path, env.ground) && color3(*o, "fogColor", path, env.fogColor) &&
               color3(*o, "tint", path, env.tint) && number(*o, "fogDensity", path, env.fogDensity) &&
               number(*o, "fogStart", path, env.fogStart) && number(*o, "fogEnd", path, env.fogEnd) &&
               number(*o, "bloomThreshold", path, env.bloomThreshold) &&
               number(*o, "bloomIntensity", path, env.bloomIntensity) && number(*o, "exposure", path, env.exposure) &&
               number(*o, "contrast", path, env.contrast) && number(*o, "saturation", path, env.saturation);
    }

    bool common(const Value& o, Node3D& node, const std::string& path)
    {
        std::string s;
        double n[4] = {};
        bool b      = false;
        bool has    = false;

        if (!getString(o, "name", path, s, has))
            return false;
        if (has)
            node.setName(s);
        if (!getNumbers(o, "position", path, 3, n, has))
            return false;
        if (has)
            node.setPosition(vec3(n));
        double deg[3] = {};
        bool hasDeg   = false;
        if (!getNumbers(o, "rotationDegrees", path, 3, deg, hasDeg) || !getNumbers(o, "rotation", path, 4, n, has))
            return false;
        if (has && hasDeg)
            return fail(path, "use either \"rotation\" (quaternion) or \"rotationDegrees\", not both");
        if (hasDeg)
            node.setRotation(math::Quaternion::fromEulerDegrees(vec3(deg)));
        else if (has)
            node.setRotation({static_cast<float>(n[0]), static_cast<float>(n[1]), static_cast<float>(n[2]),
                              static_cast<float>(n[3])});
        if (!getNumbers(o, "scale", path, 3, n, has))
            return false;
        if (has)
            node.setScale(vec3(n));
        if (!getBool(o, "visible", path, b, has))
            return false;
        if (has)
            node.setVisible(b);
        return true;
    }

    std::unique_ptr<Node3D> node(const Value& o, const std::string& path, int depth)
    {
        if (depth > kMaxDepth)
        {
            fail(path, "nesting too deep");
            return nullptr;
        }
        if (!std::holds_alternative<Object>(o.v))
        {
            fail(path, "a node must be an object");
            return nullptr;
        }
        std::string type = "Node3D";
        bool has         = false;
        if (!getString(o, "type", path, type, has))
            return nullptr;

        std::unique_ptr<Node3D> out;
        if (type == "Node3D")
        {
            out = std::make_unique<Node3D>();
        }
        else if (type == "Mesh")
        {
            auto mesh = std::make_unique<MeshNode>();
            std::string source;
            if (!getString(o, "mesh", path, source, has))
                return nullptr;
            if (has && !source.empty())
            {
                std::string err;
                if (!mesh->setMeshSource(source, &err))
                {
                    fail(path, "mesh: " + err);
                    return nullptr;
                }
            }
            double c[4] = {};
            if (!getNumbers(o, "color", path, 4, c, has))
                return nullptr;
            if (has)
                mesh->setColor({static_cast<float>(c[0]), static_cast<float>(c[1]), static_cast<float>(c[2]),
                                static_cast<float>(c[3])});
            std::string texture;
            if (!getString(o, "texture", path, texture, has))
                return nullptr;
            if (has)
                mesh->setTexturePath(std::move(texture));
            double uv[2] = {1.0, 1.0};
            if (!getNumbers(o, "uvScale", path, 2, uv, has))
                return nullptr;
            if (has)
                mesh->setUvScale({static_cast<float>(uv[0]), static_cast<float>(uv[1])});
            bool cast = true;
            if (!getBool(o, "castShadow", path, cast, has))
                return nullptr;
            if (has)
                mesh->setCastShadow(cast);
            std::string animation;
            if (!getString(o, "animation", path, animation, has))
                return nullptr;
            if (has && !animation.empty())
            {
                bool loop = true;
                double speed = 1.0;
                if (!getBool(o, "animationLoop", path, loop, has) || !getNumber(o, "animationSpeed", path, speed, has))
                    return nullptr;
                mesh->setAnimationSpeed(static_cast<float>(speed));
                mesh->playAnimation(animation, loop);
            }
            out = std::move(mesh);
        }
        else if (type == "Prefab")
        {
            auto prefab = std::make_unique<PrefabNode>();
            std::string source;
            if (!getString(o, "source", path, source, has))
                return nullptr;
            if (has && !source.empty())
            {
                std::string err;
                if (!prefab->setSource(source, &err))
                {
                    fail(path, "prefab: " + err);
                    return nullptr;
                }
            }
            out = std::move(prefab);
        }
        else if (type == "Light")
        {
            auto light = std::make_unique<LightNode>();
            std::string kind;
            if (!getString(o, "light", path, kind, has))
                return nullptr;
            if (has)
            {
                if (kind != "point" && kind != "spot")
                {
                    fail(path, "\"light\" must be \"point\" or \"spot\"");
                    return nullptr;
                }
                light->setLightType(kind == "spot" ? LightType::Spot : LightType::Point);
            }
            double c[3] = {};
            if (!getNumbers(o, "color", path, 3, c, has))
                return nullptr;
            if (has)
                light->setColor({static_cast<float>(c[0]), static_cast<float>(c[1]), static_cast<float>(c[2]), 1.f});
            double d = 0;
            if (!getNumber(o, "intensity", path, d, has))
                return nullptr;
            if (has)
                light->setIntensity(static_cast<float>(d));
            if (!getNumber(o, "range", path, d, has))
                return nullptr;
            if (has)
                light->setRange(static_cast<float>(d));
            if (!getNumber(o, "innerAngle", path, d, has))
                return nullptr;
            if (has)
                light->setInnerAngle(static_cast<float>(d));
            if (!getNumber(o, "outerAngle", path, d, has))
                return nullptr;
            if (has)
                light->setOuterAngle(static_cast<float>(d));
            bool cast = false;
            if (!getBool(o, "castShadow", path, cast, has))
                return nullptr;
            if (has)
                light->setCastShadow(cast);
            if (!getNumber(o, "shadowOpacity", path, d, has))
                return nullptr;
            if (has)
                light->setShadowOpacity(static_cast<float>(d));
            out = std::move(light);
        }
        else
        {
            fail(path, "unknown node type \"" + type + "\"");
            return nullptr;
        }

        if (!common(o, *out, path))
            return nullptr;

        if (const Value* list = o.find("scripts"))
        {
            const auto* names = std::get_if<Array>(&list->v);
            if (!names)
            {
                fail(path, "\"scripts\" must be an array of names");
                return nullptr;
            }
            for (const Value& entry : *names)
            {
                const auto* name = std::get_if<std::string>(&entry.v);
                if (!name)
                {
                    fail(path, "\"scripts\" must be an array of names");
                    return nullptr;
                }
                out->addScript(*name);
            }
        }

        if (const Value* f = o.find("children"))
        {
            const auto* a = std::get_if<Array>(&f->v);
            if (!a)
            {
                fail(path, "\"children\" must be an array");
                return nullptr;
            }
            for (std::size_t i = 0; i < a->size(); ++i)
            {
                auto child = node((*a)[i], path + "/children[" + std::to_string(i) + "]", depth + 1);
                if (!child)
                    return nullptr;
                out->addChild(std::move(child));
            }
        }
        return out;
    }
};

void setError(std::string* error, const std::string& msg)
{
    if (error)
        *error = msg;
}

std::string colorText(const renderer::Color& c)
{
    std::string out;
    const float v[3] = {c.r, c.g, c.b};
    writeNums(out, v, 3);
    return out;
}

std::string environmentText(const Environment& e)
{
    const Environment defaults;
    std::string out = "  \"environment\": { \"sky\": ";
    writeString(out, e.sky == SkyMode::Gradient ? "gradient" : (e.sky == SkyMode::Panorama ? "panorama" : "none"));
    out += ", \"zenith\": " + colorText(e.zenith) + ", \"horizon\": " + colorText(e.horizon) +
           ", \"ground\": " + colorText(e.ground) + ", \"panorama\": ";
    writeString(out, e.panorama);
    out += ", \"fog\": ";
    writeString(out, e.fog == FogMode::Linear ? "linear" : (e.fog == FogMode::Exponential ? "exponential" : "none"));
    out += ", \"fogColor\": " + colorText(e.fogColor) + ", \"fogDensity\": ";
    writeNumber(out, e.fogDensity);
    out += ", \"fogStart\": ";
    writeNumber(out, e.fogStart);
    out += ", \"fogEnd\": ";
    writeNumber(out, e.fogEnd);
    out += ", \"bloom\": ";
    out += e.bloom ? "true" : "false";
    out += ", \"bloomThreshold\": ";
    writeNumber(out, e.bloomThreshold);
    out += ", \"bloomIntensity\": ";
    writeNumber(out, e.bloomIntensity);
    out += ", \"exposure\": ";
    writeNumber(out, e.exposure);
    out += ", \"contrast\": ";
    writeNumber(out, e.contrast);
    out += ", \"saturation\": ";
    writeNumber(out, e.saturation);
    out += ", \"tint\": " + colorText(e.tint) + " },\n";
    return out;
}

}

std::string serializeScene3D(const Scene3D& scene)
{
    const Camera& c = scene.camera();
    const Light& l  = scene.light();

    std::string out = "{\n  \"format\": \"zio.scene3d\",\n  \"version\": 1,\n";
    out += "  \"camera\": { \"position\": ";
    writeVec3(out, c.position);
    out += ", \"target\": ";
    writeVec3(out, c.target);
    out += ", \"up\": ";
    writeVec3(out, c.up);
    out += ", \"fov\": ";
    writeNumber(out, c.fovYDegrees);
    out += ", \"near\": ";
    writeNumber(out, c.nearZ);
    out += ", \"far\": ";
    writeNumber(out, c.farZ);
    out += " },\n  \"light\": { \"direction\": ";
    writeVec3(out, l.direction);
    out += ", \"ambient\": ";
    writeNumber(out, l.ambient);
    out += ", \"shadows\": ";
    out += l.shadows ? "true" : "false";
    out += ", \"shadowOpacity\": ";
    writeNumber(out, l.shadowOpacity);
    out += " },\n";
    out += environmentText(scene.environment());
    out += "  \"root\": ";
    writeNode(out, scene.root(), 1);
    out += "\n}\n";
    return out;
}

std::string serializeNode3D(const Node3D& node)
{
    std::string out;
    writeNode(out, node, 0);
    out += '\n';
    return out;
}

std::unique_ptr<Node3D> deserializeNode3D(std::string_view jsonText, std::string* error)
{
    Value doc;
    std::string err;
    if (!Parser(jsonText).parse(doc, err))
    {
        setError(error, err);
        return nullptr;
    }
    Reader reader;
    auto built = reader.node(doc, "node", 0);
    if (!built)
        setError(error, reader.err);
    return built;
}

bool saveScene3DFile(const Scene3D& scene, const std::string& path, std::string* error)
{
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f)
    {
        setError(error, "cannot open \"" + path + "\" for writing");
        return false;
    }
    const std::string text = serializeScene3D(scene);
    f.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!f)
    {
        setError(error, "failed writing \"" + path + "\"");
        return false;
    }
    return true;
}

bool deserializeScene3D(Scene3D& scene, std::string_view jsonText, std::string* error)
{
    if (jsonText.size() >= 3 && static_cast<unsigned char>(jsonText[0]) == 0xEF &&
        static_cast<unsigned char>(jsonText[1]) == 0xBB && static_cast<unsigned char>(jsonText[2]) == 0xBF)
        jsonText.remove_prefix(3);

    Value doc;
    std::string err;
    if (!Parser(jsonText).parse(doc, err))
    {
        setError(error, err);
        return false;
    }
    if (!std::holds_alternative<Object>(doc.v))
    {
        setError(error, "scene file must be a JSON object");
        return false;
    }
    const Value* fmt   = doc.find("format");
    const auto* fmtStr = fmt ? std::get_if<std::string>(&fmt->v) : nullptr;
    if (!fmtStr || *fmtStr != "zio.scene3d")
    {
        setError(error, "not a zio.scene3d file (missing or wrong \"format\")");
        return false;
    }
    const Value* ver   = doc.find("version");
    const auto* verNum = ver ? std::get_if<double>(&ver->v) : nullptr;
    if (!verNum || *verNum != 1.0)
    {
        setError(error, "unsupported scene \"version\" (this build reads version 1)");
        return false;
    }
    const Value* rootVal = doc.find("root");
    if (!rootVal)
    {
        setError(error, "missing \"root\"");
        return false;
    }

    Reader reader;
    Camera camera = scene.camera();
    Light light   = scene.light();
    Environment environment;
    if (!reader.camera(doc, camera) || !reader.light(doc, light) || !reader.environment(doc, environment))
    {
        setError(error, reader.err);
        return false;
    }
    auto built = reader.node(*rootVal, "root", 0);
    if (!built)
    {
        setError(error, reader.err);
        return false;
    }
    if (dynamic_cast<MeshNode*>(built.get()))
    {
        setError(error, "root: the root node must be of type \"Node3D\"");
        return false;
    }

    Node3D& root = scene.root();
    root.removeAllChildren();
    root.setName(built->name());
    root.setPosition(built->position());
    root.setRotation(built->rotation());
    root.setScale(built->scale());
    root.setVisible(built->isVisible());
    while (!built->children().empty())
        root.addChild(built->removeChild(built->children().front().get()));
    scene.camera() = camera;
    scene.light()  = light;
    scene.environment() = environment;
    return true;
}

bool loadScene3DFile(Scene3D& scene, const std::string& path, std::string* error)
{
    std::ifstream f(path, std::ios::binary);
    if (!f)
    {
        setError(error, "cannot open \"" + path + "\"");
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return deserializeScene3D(scene, ss.str(), error);
}

}
