#include "scene3d/Gltf.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>

#include "internal/Json.h"
#include "zio/math/Mat4.h"
#include "zio/scene3d/Skeleton.h"

namespace fs = std::filesystem;

namespace zio::scene3d
{

namespace
{

using json::Array;
using json::Value;

struct Loader
{
    std::string path;
    std::string err;
    Value root;
    std::vector<std::vector<std::uint8_t>> buffers;
    std::shared_ptr<Mesh> mesh = std::make_shared<Mesh>();
    std::vector<long> partMaterial;
    std::vector<long> parentOf;
    std::shared_ptr<Skin> skin;
    long currentSkin = -1;
    std::map<long, int> skinOffset;
    std::map<long, std::vector<int>> nodeJoints;
    std::vector<VertexSkin> skinVerts;

    bool fail(const std::string& msg)
    {
        if (err.empty())
            err = path + ": " + msg;
        return false;
    }

    static double number(const Value* v, double fallback)
    {
        if (!v)
            return fallback;
        const auto* d = std::get_if<double>(&v->v);
        return d ? *d : fallback;
    }

    static const Array* array(const Value& o, const char* key)
    {
        const Value* v = o.find(key);
        return v ? std::get_if<Array>(&v->v) : nullptr;
    }

    const Value* at(const char* key, long index) const
    {
        const Array* a = array(root, key);
        if (!a || index < 0 || static_cast<std::size_t>(index) >= a->size())
            return nullptr;
        return &(*a)[static_cast<std::size_t>(index)];
    }

    static int base64Value(char c)
    {
        if (c >= 'A' && c <= 'Z')
            return c - 'A';
        if (c >= 'a' && c <= 'z')
            return c - 'a' + 26;
        if (c >= '0' && c <= '9')
            return c - '0' + 52;
        if (c == '+')
            return 62;
        if (c == '/')
            return 63;
        return -1;
    }

    static std::vector<std::uint8_t> decodeBase64(std::string_view s)
    {
        std::vector<std::uint8_t> out;
        std::uint32_t acc = 0;
        int bits          = 0;
        for (const char c : s)
        {
            const int v = base64Value(c);
            if (v < 0)
                continue;
            acc = (acc << 6) | static_cast<std::uint32_t>(v);
            bits += 6;
            if (bits >= 8)
            {
                bits -= 8;
                out.push_back(static_cast<std::uint8_t>((acc >> bits) & 0xFF));
            }
        }
        return out;
    }

    static int hexValue(char c)
    {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    }

    static std::string decodeUri(const std::string& uri)
    {
        std::string out;
        for (std::size_t i = 0; i < uri.size(); ++i)
        {
            if (uri[i] == '%' && i + 2 < uri.size())
            {
                const int a = hexValue(uri[i + 1]), b = hexValue(uri[i + 2]);
                if (a >= 0 && b >= 0)
                {
                    out += static_cast<char>(a * 16 + b);
                    i += 2;
                    continue;
                }
            }
            out += uri[i];
        }
        return out;
    }

    static bool readFile(const fs::path& p, std::vector<std::uint8_t>& out)
    {
        std::ifstream f(p, std::ios::binary);
        if (!f)
            return false;
        out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        return true;
    }

    bool loadBuffers(const std::vector<std::uint8_t>& glbBin)
    {
        const Array* list = array(root, "buffers");
        if (!list)
            return true;
        const fs::path dir = fs::u8path(path).parent_path();
        for (const Value& b : *list)
        {
            std::vector<std::uint8_t> data;
            const Value* uri = b.find("uri");
            if (!uri)
            {
                data = glbBin;
            }
            else
            {
                const auto* text = std::get_if<std::string>(&uri->v);
                if (!text)
                    return fail("buffer uri must be a string");
                if (text->rfind("data:", 0) == 0)
                {
                    const std::size_t comma = text->find(',');
                    if (comma == std::string::npos)
                        return fail("bad data uri");
                    data = decodeBase64(std::string_view(*text).substr(comma + 1));
                }
                else if (!readFile(dir / fs::u8path(decodeUri(*text)), data))
                    return fail("cannot open buffer \"" + *text + "\"");
            }
            buffers.push_back(std::move(data));
        }
        return true;
    }

    struct Accessor
    {
        const std::uint8_t* data = nullptr;
        std::size_t count        = 0;
        std::size_t components   = 0;
        std::size_t stride       = 0;
        std::size_t compSize     = 0;
        int type                 = 0;
        bool normalized          = false;
    };

    bool accessor(long index, Accessor& out)
    {
        const Value* a = at("accessors", index);
        if (!a)
            return fail("missing accessor");
        const Value* view = a->find("bufferView");
        if (!view)
            return fail("accessor without bufferView is not supported");
        const Value* bv = at("bufferViews", static_cast<long>(number(view, -1)));
        if (!bv)
            return fail("missing bufferView");
        const long bufferIndex = static_cast<long>(number(bv->find("buffer"), -1));
        if (bufferIndex < 0 || static_cast<std::size_t>(bufferIndex) >= buffers.size())
            return fail("missing buffer");
        const auto& buf = buffers[static_cast<std::size_t>(bufferIndex)];

        out.type       = static_cast<int>(number(a->find("componentType"), 0));
        out.count      = static_cast<std::size_t>(number(a->find("count"), 0));
        out.normalized = false;
        if (const Value* n = a->find("normalized"))
            if (const auto* b = std::get_if<bool>(&n->v))
                out.normalized = *b;

        const Value* t = a->find("type");
        const auto* ts = t ? std::get_if<std::string>(&t->v) : nullptr;
        if (!ts)
            return fail("accessor without type");
        out.components = *ts == "SCALAR" ? 1 : *ts == "VEC2" ? 2 : *ts == "VEC3" ? 3 : *ts == "VEC4" ? 4 : *ts == "MAT4" ? 16 : 0;
        if (out.components == 0)
            return fail("unsupported accessor type \"" + *ts + "\"");

        switch (out.type)
        {
        case 5120:
        case 5121:
            out.compSize = 1;
            break;
        case 5122:
        case 5123:
            out.compSize = 2;
            break;
        case 5125:
        case 5126:
            out.compSize = 4;
            break;
        default:
            return fail("unsupported component type");
        }

        const std::size_t viewOffset = static_cast<std::size_t>(number(bv->find("byteOffset"), 0));
        const std::size_t accOffset  = static_cast<std::size_t>(number(a->find("byteOffset"), 0));
        out.stride                   = static_cast<std::size_t>(number(bv->find("byteStride"), 0));
        if (out.stride == 0)
            out.stride = out.components * out.compSize;
        const std::size_t start = viewOffset + accOffset;
        if (out.count > 0 && start + out.stride * (out.count - 1) + out.components * out.compSize > buf.size())
            return fail("accessor is out of range of its buffer");
        out.data = buf.data() + start;
        return true;
    }

    static float component(const Accessor& a, std::size_t element, std::size_t c)
    {
        const std::uint8_t* p = a.data + element * a.stride + c * a.compSize;
        switch (a.type)
        {
        case 5126:
        {
            float f;
            std::memcpy(&f, p, 4);
            return f;
        }
        case 5120:
        {
            const float v = static_cast<float>(static_cast<std::int8_t>(*p));
            return a.normalized ? (std::max)(v / 127.f, -1.f) : v;
        }
        case 5121:
        {
            const float v = static_cast<float>(*p);
            return a.normalized ? v / 255.f : v;
        }
        case 5122:
        {
            std::int16_t s;
            std::memcpy(&s, p, 2);
            return a.normalized ? (std::max)(static_cast<float>(s) / 32767.f, -1.f) : static_cast<float>(s);
        }
        case 5123:
        {
            std::uint16_t s;
            std::memcpy(&s, p, 2);
            return a.normalized ? static_cast<float>(s) / 65535.f : static_cast<float>(s);
        }
        default:
        {
            std::uint32_t u;
            std::memcpy(&u, p, 4);
            return static_cast<float>(u);
        }
        }
    }

    static std::uint32_t index(const Accessor& a, std::size_t element)
    {
        const std::uint8_t* p = a.data + element * a.stride;
        if (a.type == 5121)
            return *p;
        if (a.type == 5123)
        {
            std::uint16_t s;
            std::memcpy(&s, p, 2);
            return s;
        }
        std::uint32_t u;
        std::memcpy(&u, p, 4);
        return u;
    }

    std::string imagePath(const Value* textureInfo)
    {
        if (!textureInfo)
            return {};
        const Value* t = at("textures", static_cast<long>(number(textureInfo->find("index"), -1)));
        if (!t)
            return {};
        const long imageIndex = static_cast<long>(number(t->find("source"), -1));
        const Value* img      = at("images", imageIndex);
        if (!img)
            return {};

        const Value* uri = img->find("uri");
        const auto* text = uri ? std::get_if<std::string>(&uri->v) : nullptr;
        if (text && text->rfind("data:", 0) != 0)
            return (fs::u8path(path).parent_path() / fs::u8path(decodeUri(*text))).generic_string();

        const std::string key = std::string(kEmbeddedPrefix) + path + "#" + std::to_string(imageIndex);
        if (mesh->embeddedImages.count(key))
            return key;

        std::vector<std::uint8_t> bytes;
        if (text)
        {
            const std::size_t comma = text->find(',');
            if (comma == std::string::npos)
                return {};
            bytes = decodeBase64(std::string_view(*text).substr(comma + 1));
        }
        else if (const Value* view = img->find("bufferView"))
        {
            const Value* bv = at("bufferViews", static_cast<long>(number(view, -1)));
            if (!bv)
                return {};
            const long bufferIndex = static_cast<long>(number(bv->find("buffer"), -1));
            if (bufferIndex < 0 || static_cast<std::size_t>(bufferIndex) >= buffers.size())
                return {};
            const auto& buffer      = buffers[static_cast<std::size_t>(bufferIndex)];
            const std::size_t first = static_cast<std::size_t>(number(bv->find("byteOffset"), 0));
            const std::size_t size  = static_cast<std::size_t>(number(bv->find("byteLength"), 0));
            if (first + size > buffer.size())
                return {};
            bytes.assign(buffer.begin() + static_cast<std::ptrdiff_t>(first),
                         buffer.begin() + static_cast<std::ptrdiff_t>(first + size));
        }
        if (bytes.empty())
            return {};
        mesh->embeddedImages[key] = std::move(bytes);
        return key;
    }

    void readMaterial(long materialIndex, SubMesh& part)
    {
        const Value* m = at("materials", materialIndex);
        if (!m)
            return;
        Material& mat = part.material;
        if (const Value* pbr = m->find("pbrMetallicRoughness"))
        {
            if (const Value* f = pbr->find("baseColorFactor"))
                if (const auto* a = std::get_if<Array>(&f->v); a && a->size() == 4)
                    for (std::size_t i = 0; i < 4; ++i)
                        part.baseColor[i] = static_cast<float>(number(&(*a)[i], 1.0));
            mat.metallic  = static_cast<float>(number(pbr->find("metallicFactor"), 1.0));
            mat.roughness = static_cast<float>(number(pbr->find("roughnessFactor"), 1.0));
            part.texturePath               = imagePath(pbr->find("baseColorTexture"));
            mat.metallicRoughnessTexture   = imagePath(pbr->find("metallicRoughnessTexture"));
        }
        if (const Value* f = m->find("emissiveFactor"))
            if (const auto* a = std::get_if<Array>(&f->v); a && a->size() == 3)
                for (std::size_t i = 0; i < 3; ++i)
                    mat.emissive[i] = static_cast<float>(number(&(*a)[i], 0.0));
        if (const Value* n = m->find("normalTexture"))
        {
            mat.normalTexture = imagePath(n);
            mat.normalScale   = static_cast<float>(number(n->find("scale"), 1.0));
        }
        if (const Value* o = m->find("occlusionTexture"))
        {
            mat.occlusionTexture   = imagePath(o);
            mat.occlusionStrength  = static_cast<float>(number(o->find("strength"), 1.0));
        }
        mat.emissiveTexture = imagePath(m->find("emissiveTexture"));
        if (const Value* am = m->find("alphaMode"))
            if (const auto* text = std::get_if<std::string>(&am->v))
                mat.alphaMode = *text == "BLEND"  ? AlphaMode::Blend
                                : *text == "MASK" ? AlphaMode::Mask
                                                  : AlphaMode::Opaque;
        mat.alphaCutoff = static_cast<float>(number(m->find("alphaCutoff"), 0.5));
        if (const Value* ds = m->find("doubleSided"))
            if (const auto* b = std::get_if<bool>(&ds->v))
                mat.doubleSided = *b;
    }

    void addPart(long materialIndex, std::size_t firstVertex)
    {
        const std::size_t total = mesh->vertices.size();
        if (total <= firstVertex)
            return;
        if (!mesh->parts.empty() && partMaterial.back() == materialIndex && mesh->parts.back().first + mesh->parts.back().count == firstVertex)
        {
            mesh->parts.back().count += total - firstVertex;
            return;
        }
        SubMesh part;
        part.first = firstVertex;
        part.count = total - firstVertex;
        readMaterial(materialIndex, part);
        mesh->parts.push_back(std::move(part));
        partMaterial.push_back(materialIndex);
    }

    math::Mat4 nodeLocal(long nodeIndex) const
    {
        const Value* n = at("nodes", nodeIndex);
        return n ? localMatrix(*n) : math::Mat4{};
    }

    math::Mat4 ancestorWorld(long nodeIndex) const
    {
        math::Mat4 result;
        long p = nodeIndex < 0 || static_cast<std::size_t>(nodeIndex) >= parentOf.size()
                     ? -1
                     : parentOf[static_cast<std::size_t>(nodeIndex)];
        for (int guard = 0; p >= 0 && guard < 256; ++guard)
        {
            result = nodeLocal(p) * result;
            p      = static_cast<std::size_t>(p) < parentOf.size() ? parentOf[static_cast<std::size_t>(p)] : -1;
        }
        return result;
    }

    bool buildSkin(long skinIndex)
    {
        if (skinOffset.count(skinIndex))
            return true;
        const Value* s = at("skins", skinIndex);
        if (!s)
            return fail("missing skin");
        const Array* joints = array(*s, "joints");
        if (!joints || joints->empty())
            return fail("skin without joints");
        if (!skin)
        {
            skin = std::make_shared<Skin>();
            skinVerts.resize(mesh->vertices.size());
        }
        const int offset = static_cast<int>(skin->joints.size());
        if (offset + joints->size() > 65535)
            return fail("too many joints");
        skinOffset[skinIndex] = offset;

        std::map<long, int> local;
        for (std::size_t i = 0; i < joints->size(); ++i)
            local[static_cast<long>(number(&(*joints)[i], -1))] = static_cast<int>(i);

        std::vector<math::Mat4> inverse(joints->size());
        if (const Value* ibm = s->find("inverseBindMatrices"))
        {
            Accessor a;
            if (!accessor(static_cast<long>(number(ibm, -1)), a))
                return false;
            if (a.components != 16 || a.type != 5126 || a.count < joints->size())
                return fail("bad inverseBindMatrices");
            for (std::size_t j = 0; j < joints->size(); ++j)
                for (std::size_t k = 0; k < 16; ++k)
                    inverse[j].m[k % 4][k / 4] = component(a, j, k);
        }

        for (std::size_t i = 0; i < joints->size(); ++i)
        {
            const long nodeIndex = static_cast<long>(number(&(*joints)[i], -1));
            const Value* node    = at("nodes", nodeIndex);
            if (!node)
                return fail("skin refers to a missing node");
            SkinJoint joint;
            if (const Value* nm = node->find("name"))
                if (const auto* str = std::get_if<std::string>(&nm->v))
                    joint.name = *str;
            float tv[3] = {0, 0, 0}, sv[3] = {1, 1, 1}, qv[4] = {0, 0, 0, 1};
            const auto vec = [&](const char* key, std::size_t n, float* out) {
                if (const Value* v = node->find(key))
                    if (const auto* arr = std::get_if<Array>(&v->v); arr && arr->size() == n)
                        for (std::size_t k = 0; k < n; ++k)
                            out[k] = static_cast<float>(number(&(*arr)[k], out[k]));
            };
            vec("translation", 3, tv);
            vec("scale", 3, sv);
            vec("rotation", 4, qv);
            joint.translation = {tv[0], tv[1], tv[2]};
            joint.scale       = {sv[0], sv[1], sv[2]};
            joint.rotation    = math::Quaternion{qv[0], qv[1], qv[2], qv[3]}.normalized();
            joint.inverseBind = inverse[i];

            const long parent = static_cast<std::size_t>(nodeIndex) < parentOf.size()
                                    ? parentOf[static_cast<std::size_t>(nodeIndex)]
                                    : -1;
            const auto it = local.find(parent);
            if (it != local.end())
                joint.parent = offset + it->second;
            else
                joint.parentTransform = ancestorWorld(nodeIndex);
            nodeJoints[nodeIndex].push_back(offset + static_cast<int>(i));
            skin->joints.push_back(std::move(joint));
        }
        return true;
    }

    bool loadAnimations()
    {
        const Array* list = array(root, "animations");
        if (!list || !skin)
            return true;
        for (std::size_t ai = 0; ai < list->size(); ++ai)
        {
            const Value& anim = (*list)[ai];
            AnimationClip clip;
            clip.name = "animacion_" + std::to_string(ai);
            if (const Value* nm = anim.find("name"))
                if (const auto* str = std::get_if<std::string>(&nm->v); str && !str->empty())
                    clip.name = *str;
            const Array* samplers = array(anim, "samplers");
            const Array* channels = array(anim, "channels");
            if (!samplers || !channels)
                continue;
            for (const Value& ch : *channels)
            {
                const Value* target = ch.find("target");
                if (!target)
                    continue;
                const auto it = nodeJoints.find(static_cast<long>(number(target->find("node"), -1)));
                if (it == nodeJoints.end())
                    continue;
                const Value* pathValue = target->find("path");
                const auto* pathText   = pathValue ? std::get_if<std::string>(&pathValue->v) : nullptr;
                if (!pathText)
                    continue;
                AnimationChannel c;
                if (*pathText == "translation")
                    c.path = AnimationPath::Translation;
                else if (*pathText == "rotation")
                    c.path = AnimationPath::Rotation;
                else if (*pathText == "scale")
                    c.path = AnimationPath::Scale;
                else
                    continue;

                const long samplerIndex = static_cast<long>(number(ch.find("sampler"), -1));
                if (samplerIndex < 0 || static_cast<std::size_t>(samplerIndex) >= samplers->size())
                    continue;
                const Value& sampler = (*samplers)[static_cast<std::size_t>(samplerIndex)];
                std::string interpolation = "LINEAR";
                if (const Value* iv = sampler.find("interpolation"))
                    if (const auto* str = std::get_if<std::string>(&iv->v))
                        interpolation = *str;
                c.interpolation = interpolation == "STEP"          ? Interpolation::Step
                                  : interpolation == "CUBICSPLINE" ? Interpolation::CubicSpline
                                                                   : Interpolation::Linear;

                Accessor input, output;
                if (!accessor(static_cast<long>(number(sampler.find("input"), -1)), input) ||
                    !accessor(static_cast<long>(number(sampler.find("output"), -1)), output))
                    return false;
                const std::size_t width = c.path == AnimationPath::Rotation ? 4 : 3;
                if (input.components != 1 || output.components != width)
                    continue;
                const std::size_t perKey = c.interpolation == Interpolation::CubicSpline ? 3 : 1;
                if (output.count < input.count * perKey)
                    continue;
                for (std::size_t k = 0; k < input.count; ++k)
                {
                    c.times.push_back(component(input, k, 0));
                    for (std::size_t r = 0; r < perKey; ++r)
                        for (std::size_t w = 0; w < width; ++w)
                            c.values.push_back(component(output, k * perKey + r, w));
                }
                if (c.times.empty())
                    continue;
                clip.duration = (std::max)(clip.duration, c.times.back());
                for (const int joint : it->second)
                {
                    AnimationChannel copy = c;
                    copy.joint            = joint;
                    clip.channels.push_back(std::move(copy));
                }
            }
            if (!clip.channels.empty())
                skin->clips.push_back(std::move(clip));
        }
        return true;
    }

    bool primitive(const Value& prim, const math::Mat4& worldMatrix)
    {
        const std::size_t firstVertex = mesh->vertices.size();
        math::Mat4 world              = worldMatrix;
        const long mode = static_cast<long>(number(prim.find("mode"), 4));
        if (mode != 4 && mode != 5 && mode != 6)
            return true;
        const Value* attrs = prim.find("attributes");
        if (!attrs)
            return true;
        const Value* posIdx = attrs->find("POSITION");
        if (!posIdx)
            return true;

        Accessor pos;
        if (!accessor(static_cast<long>(number(posIdx, -1)), pos))
            return false;
        Accessor nrm, uv;
        bool hasN = false, hasUv = false;
        if (const Value* n = attrs->find("NORMAL"))
        {
            if (!accessor(static_cast<long>(number(n, -1)), nrm))
                return false;
            hasN = nrm.components == 3 && nrm.count == pos.count;
        }
        if (const Value* t = attrs->find("TEXCOORD_0"))
        {
            if (!accessor(static_cast<long>(number(t, -1)), uv))
                return false;
            hasUv = uv.components == 2 && uv.count == pos.count;
        }

        Accessor col;
        bool hasColor = false;
        if (const Value* c = attrs->find("COLOR_0"))
        {
            if (!accessor(static_cast<long>(number(c, -1)), col))
                return false;
            hasColor = (col.components == 3 || col.components == 4) && col.count == pos.count;
        }

        struct TargetData
        {
            Accessor pos, nrm;
            bool hasPos = false, hasNrm = false;
        };
        std::vector<TargetData> targets;
        if (const Array* list = array(prim, "targets"))
            for (const Value& t : *list)
            {
                TargetData d;
                if (const Value* tp = t.find("POSITION"))
                {
                    if (!accessor(static_cast<long>(number(tp, -1)), d.pos))
                        return false;
                    d.hasPos = d.pos.components == 3 && d.pos.count == pos.count;
                }
                if (const Value* tn = t.find("NORMAL"))
                {
                    if (!accessor(static_cast<long>(number(tn, -1)), d.nrm))
                        return false;
                    d.hasNrm = d.nrm.components == 3 && d.nrm.count == pos.count;
                }
                targets.push_back(d);
            }
        while (mesh->morphTargets.size() < targets.size())
            mesh->morphTargets.emplace_back();

        Accessor jointsAccessor[2], weightsAccessor[2];
        bool hasSet[2] = {false, false};
        bool skinned   = false;
        int offset     = 0;
        if (currentSkin >= 0)
        {
            const char* jointKeys[2]  = {"JOINTS_0", "JOINTS_1"};
            const char* weightKeys[2] = {"WEIGHTS_0", "WEIGHTS_1"};
            for (int set = 0; set < 2; ++set)
            {
                const Value* jv = attrs->find(jointKeys[set]);
                const Value* wv = attrs->find(weightKeys[set]);
                if (!jv || !wv)
                    continue;
                if (!accessor(static_cast<long>(number(jv, -1)), jointsAccessor[set]) ||
                    !accessor(static_cast<long>(number(wv, -1)), weightsAccessor[set]))
                    return false;
                hasSet[set] = jointsAccessor[set].components == 4 && weightsAccessor[set].components == 4 &&
                              jointsAccessor[set].count == pos.count && weightsAccessor[set].count == pos.count;
            }
            skinned = hasSet[0];
        }
        if (skinned)
        {
            if (!buildSkin(currentSkin))
                return false;
            offset = skinOffset[currentSkin];
            world  = math::Mat4{};
        }

        std::vector<std::uint32_t> indices;
        if (const Value* ii = prim.find("indices"))
        {
            Accessor ia;
            if (!accessor(static_cast<long>(number(ii, -1)), ia))
                return false;
            if (ia.type == 5126 || ia.components != 1)
                return fail("bad index accessor");
            for (std::size_t i = 0; i < ia.count; ++i)
                indices.push_back(index(ia, i));
        }
        else
        {
            for (std::size_t i = 0; i < pos.count; ++i)
                indices.push_back(static_cast<std::uint32_t>(i));
        }

        std::vector<std::uint32_t> tris;
        if (mode == 4)
        {
            tris = indices;
        }
        else
        {
            for (std::size_t i = 2; i < indices.size(); ++i)
            {
                if (mode == 5)
                {
                    const bool odd = (i % 2) == 1;
                    tris.push_back(indices[i - 2]);
                    tris.push_back(odd ? indices[i] : indices[i - 1]);
                    tris.push_back(odd ? indices[i - 1] : indices[i]);
                }
                else
                {
                    tris.push_back(indices[0]);
                    tris.push_back(indices[i - 1]);
                    tris.push_back(indices[i]);
                }
            }
        }

        for (std::size_t t = 0; t + 2 < tris.size(); t += 3)
        {
            math::Vec3 p[3];
            for (std::size_t k = 0; k < 3; ++k)
            {
                const std::size_t i = tris[t + k];
                if (i >= pos.count)
                    return fail("index out of range");
                p[k] = world.transformPoint({component(pos, i, 0), component(pos, i, 1), component(pos, i, 2)});
            }
            const math::Vec3 face = (p[1] - p[0]).cross(p[2] - p[0]).normalized();
            for (std::size_t k = 0; k < 3; ++k)
            {
                const std::size_t i = tris[t + k];
                math::Vec3 n        = face;
                if (hasN)
                    n = world.transformVector({component(nrm, i, 0), component(nrm, i, 1), component(nrm, i, 2)})
                            .normalized();
                const float u = hasUv ? component(uv, i, 0) : 0.f;
                const float v = hasUv ? component(uv, i, 1) : 0.f;
                Vertex3D vertex{p[k], n, u, v};
                if (hasColor)
                {
                    vertex.r = component(col, i, 0);
                    vertex.g = component(col, i, 1);
                    vertex.b = component(col, i, 2);
                    vertex.a = col.components == 4 ? component(col, i, 3) : 1.f;
                }
                mesh->vertices.push_back(vertex);
                for (std::size_t ti = 0; ti < mesh->morphTargets.size(); ++ti)
                {
                    MorphTarget& mt = mesh->morphTargets[ti];
                    mt.positions.resize(mesh->vertices.size(), math::Vec3::zero());
                    mt.normals.resize(mesh->vertices.size(), math::Vec3::zero());
                    if (ti >= targets.size())
                        continue;
                    const TargetData& td = targets[ti];
                    if (td.hasPos)
                        mt.positions.back() = world.transformVector(
                            {component(td.pos, i, 0), component(td.pos, i, 1), component(td.pos, i, 2)});
                    if (td.hasNrm)
                        mt.normals.back() = world.transformVector(
                            {component(td.nrm, i, 0), component(td.nrm, i, 1), component(td.nrm, i, 2)});
                }
                if (skin)
                {
                    VertexSkin vs;
                    if (skinned)
                        for (std::size_t set = 0; set < 2; ++set)
                            if (hasSet[set])
                                for (std::size_t w = 0; w < 4; ++w)
                                {
                                    vs.joints[set * 4 + w] =
                                        static_cast<std::uint16_t>(component(jointsAccessor[set], i, w) + offset);
                                    vs.weights[set * 4 + w] = component(weightsAccessor[set], i, w);
                                }
                    skinVerts.push_back(vs);
                }
            }
        }
        addPart(static_cast<long>(number(prim.find("material"), -1)), firstVertex);
        return true;
    }

    static math::Mat4 localMatrix(const Value& node)
    {
        math::Mat4 r;
        if (const Value* m = node.find("matrix"))
            if (const auto* a = std::get_if<Array>(&m->v); a && a->size() == 16)
            {
                for (std::size_t i = 0; i < 16; ++i)
                    r.m[i % 4][i / 4] = static_cast<float>(number(&(*a)[i], 0.0));
                return r;
            }
        const auto vec = [&](const char* key, std::size_t n, float* out) {
            if (const Value* v = node.find(key))
                if (const auto* a = std::get_if<Array>(&v->v); a && a->size() == n)
                    for (std::size_t i = 0; i < n; ++i)
                        out[i] = static_cast<float>(number(&(*a)[i], out[i]));
        };
        float tv[3] = {0, 0, 0}, sv[3] = {1, 1, 1}, qv[4] = {0, 0, 0, 1};
        vec("translation", 3, tv);
        vec("scale", 3, sv);
        vec("rotation", 4, qv);
        const math::Quaternion q{qv[0], qv[1], qv[2], qv[3]};
        return math::Mat4::translation({tv[0], tv[1], tv[2]}) * math::Mat4::rotation(q) *
               math::Mat4::scale({sv[0], sv[1], sv[2]});
    }

    void readMorphInfo(const Value& node, const Value& meshValue)
    {
        if (mesh->morphTargets.empty())
            return;
        if (mesh->morphWeights.empty())
        {
            const Array* w = array(node, "weights");
            if (!w)
                w = array(meshValue, "weights");
            if (w)
                for (const Value& v : *w)
                    mesh->morphWeights.push_back(static_cast<float>(number(&v, 0.0)));
        }
        if (const Value* extras = meshValue.find("extras"))
            if (const Array* names = array(*extras, "targetNames"))
                for (std::size_t i = 0; i < names->size() && i < mesh->morphTargets.size(); ++i)
                    if (const auto* text = std::get_if<std::string>(&(*names)[i].v))
                        if (mesh->morphTargets[i].name.empty())
                            mesh->morphTargets[i].name = *text;
    }

    void readFileCamera(const Value& node, const math::Mat4& world)
    {
        const Value* ci = node.find("camera");
        if (!ci)
            return;
        const Value* c = at("cameras", static_cast<long>(number(ci, -1)));
        if (!c)
            return;
        FileCamera cam;
        if (const Value* n = c->find("name"))
            if (const auto* text = std::get_if<std::string>(&n->v))
                cam.name = *text;
        cam.world = world;
        if (const Value* type = c->find("type"))
            if (const auto* text = std::get_if<std::string>(&type->v))
                cam.orthographic = *text == "orthographic";
        const Value* body = c->find(cam.orthographic ? "orthographic" : "perspective");
        if (body)
        {
            cam.nearZ = static_cast<float>(number(body->find("znear"), 0.1));
            cam.farZ  = static_cast<float>(number(body->find("zfar"), 1000.0));
            if (!cam.orthographic)
                cam.fovYDegrees = static_cast<float>(number(body->find("yfov"), 1.0471976)) * 57.29578f;
        }
        mesh->cameras.push_back(std::move(cam));
    }

    void readFileLight(const Value& node, const math::Mat4& world)
    {
        const Value* ext = node.find("extensions");
        const Value* ref = ext ? ext->find("KHR_lights_punctual") : nullptr;
        if (!ref)
            return;
        const Value* rootExt  = root.find("extensions");
        const Value* rootBody = rootExt ? rootExt->find("KHR_lights_punctual") : nullptr;
        const Array* lights   = rootBody ? array(*rootBody, "lights") : nullptr;
        const long index      = static_cast<long>(number(ref->find("light"), -1));
        if (!lights || index < 0 || static_cast<std::size_t>(index) >= lights->size())
            return;
        const Value& l = (*lights)[static_cast<std::size_t>(index)];
        FileLight light;
        light.world = world;
        if (const Value* n = l.find("name"))
            if (const auto* text = std::get_if<std::string>(&n->v))
                light.name = *text;
        if (const Value* type = l.find("type"))
            if (const auto* text = std::get_if<std::string>(&type->v))
                light.type = *text == "spot"          ? FileLightType::Spot
                             : *text == "directional" ? FileLightType::Directional
                                                      : FileLightType::Point;
        if (const Value* c = l.find("color"))
            if (const auto* a = std::get_if<Array>(&c->v); a && a->size() == 3)
                for (std::size_t i = 0; i < 3; ++i)
                    light.color[i] = static_cast<float>(number(&(*a)[i], 1.0));
        light.intensity = static_cast<float>(number(l.find("intensity"), 1.0));
        light.range     = static_cast<float>(number(l.find("range"), 0.0));
        if (const Value* spot = l.find("spot"))
        {
            light.innerAngleDegrees = static_cast<float>(number(spot->find("innerConeAngle"), 0.0)) * 57.29578f;
            light.outerAngleDegrees = static_cast<float>(number(spot->find("outerConeAngle"), 0.7853982)) * 57.29578f;
        }
        mesh->lights.push_back(std::move(light));
    }

    bool visit(long nodeIndex, const math::Mat4& parent, int depth)
    {
        if (depth > 64)
            return fail("node hierarchy is too deep");
        const Value* node = at("nodes", nodeIndex);
        if (!node)
            return fail("missing node");
        const math::Mat4 world = parent * localMatrix(*node);
        currentSkin            = static_cast<long>(number(node->find("skin"), -1));
        if (const Value* mi = node->find("mesh"))
            if (const Value* m = at("meshes", static_cast<long>(number(mi, -1))))
            {
                if (const Array* prims = array(*m, "primitives"))
                    for (const Value& p : *prims)
                        if (!primitive(p, world))
                            return false;
                readMorphInfo(*node, *m);
            }
        readFileCamera(*node, world);
        readFileLight(*node, world);
        if (const Array* kids = array(*node, "children"))
            for (const Value& k : *kids)
                if (!visit(static_cast<long>(number(&k, -1)), world, depth + 1))
                    return false;
        return true;
    }

    bool run()
    {
        std::vector<std::uint8_t> raw;
        if (!readFile(fs::u8path(path), raw))
            return fail("cannot open file");

        std::string_view jsonText;
        std::vector<std::uint8_t> glbBin;
        if (raw.size() >= 12 && std::memcmp(raw.data(), "glTF", 4) == 0)
        {
            std::size_t pos = 12;
            while (pos + 8 <= raw.size())
            {
                std::uint32_t len, type;
                std::memcpy(&len, raw.data() + pos, 4);
                std::memcpy(&type, raw.data() + pos + 4, 4);
                pos += 8;
                if (pos + len > raw.size())
                    return fail("truncated GLB chunk");
                if (type == 0x4E4F534A)
                    jsonText = std::string_view(reinterpret_cast<const char*>(raw.data() + pos), len);
                else if (type == 0x004E4942 && glbBin.empty())
                    glbBin.assign(raw.begin() + static_cast<std::ptrdiff_t>(pos),
                                  raw.begin() + static_cast<std::ptrdiff_t>(pos + len));
                pos += (static_cast<std::size_t>(len) + 3) & ~static_cast<std::size_t>(3);
            }
        }
        else
        {
            jsonText = std::string_view(reinterpret_cast<const char*>(raw.data()), raw.size());
        }
        if (jsonText.empty())
            return fail("no JSON data");

        std::string perr;
        if (!json::Parser(jsonText).parse(root, perr))
            return fail(perr);
        if (!std::get_if<json::Object>(&root.v))
            return fail("not a glTF object");
        if (!loadBuffers(glbBin))
            return false;

        if (const Array* all = array(root, "nodes"))
        {
            parentOf.assign(all->size(), -1);
            for (std::size_t i = 0; i < all->size(); ++i)
                if (const Array* kids = array((*all)[i], "children"))
                    for (const Value& k : *kids)
                    {
                        const long child = static_cast<long>(number(&k, -1));
                        if (child >= 0 && static_cast<std::size_t>(child) < parentOf.size())
                            parentOf[static_cast<std::size_t>(child)] = static_cast<long>(i);
                    }
        }

        const math::Mat4 identity;
        const Array* scenes = array(root, "scenes");
        if (scenes && !scenes->empty())
        {
            long sceneIndex = static_cast<long>(number(root.find("scene"), 0));
            if (sceneIndex < 0 || static_cast<std::size_t>(sceneIndex) >= scenes->size())
                sceneIndex = 0;
            if (const Array* roots = array((*scenes)[static_cast<std::size_t>(sceneIndex)], "nodes"))
                for (const Value& n : *roots)
                    if (!visit(static_cast<long>(number(&n, -1)), identity, 0))
                        return false;
        }
        else if (const Array* all = array(root, "nodes"))
        {
            for (std::size_t i = 0; i < all->size(); ++i)
                if (!visit(static_cast<long>(i), identity, 0))
                    return false;
        }
        if (mesh->vertices.empty())
            return fail("has no triangles");
        if (!mesh->parts.empty())
        {
            std::copy(std::begin(mesh->parts.front().baseColor), std::end(mesh->parts.front().baseColor), mesh->baseColor);
            mesh->texturePath = mesh->parts.front().texturePath;
            mesh->material    = mesh->parts.front().material;
        }
        if (skin)
        {
            skinVerts.resize(mesh->vertices.size());
            skin->vertices = std::move(skinVerts);
            if (!loadAnimations())
                return false;
            mesh->skin = skin;
        }
        return true;
    }
};

}

std::shared_ptr<Mesh> loadGltf(const std::string& path, std::string* error)
{
    Loader loader;
    loader.path = path;
    if (!loader.run())
    {
        if (error)
            *error = loader.err;
        return nullptr;
    }
    return loader.mesh;
}

}
