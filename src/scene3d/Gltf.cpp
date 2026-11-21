#include "scene3d/Gltf.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "internal/Json.h"
#include "zio/math/Mat4.h"

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
    bool materialTaken         = false;

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
        out.components = *ts == "SCALAR" ? 1 : *ts == "VEC2" ? 2 : *ts == "VEC3" ? 3 : *ts == "VEC4" ? 4 : 0;
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

    void takeMaterial(long materialIndex)
    {
        if (materialTaken)
            return;
        materialTaken  = true;
        const Value* m = at("materials", materialIndex);
        if (!m)
            return;
        const Value* pbr = m->find("pbrMetallicRoughness");
        if (!pbr)
            return;
        if (const Value* f = pbr->find("baseColorFactor"))
            if (const auto* a = std::get_if<Array>(&f->v); a && a->size() == 4)
                for (std::size_t i = 0; i < 4; ++i)
                    mesh->baseColor[i] = static_cast<float>(number(&(*a)[i], 1.0));
        const Value* tex = pbr->find("baseColorTexture");
        if (!tex)
            return;
        const Value* t = at("textures", static_cast<long>(number(tex->find("index"), -1)));
        if (!t)
            return;
        const Value* img = at("images", static_cast<long>(number(t->find("source"), -1)));
        if (!img)
            return;
        const Value* uri = img->find("uri");
        const auto* text = uri ? std::get_if<std::string>(&uri->v) : nullptr;
        if (text && text->rfind("data:", 0) != 0)
            mesh->texturePath =
                (fs::u8path(path).parent_path() / fs::u8path(decodeUri(*text))).generic_string();
    }

    bool primitive(const Value& prim, const math::Mat4& world)
    {
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
                mesh->vertices.push_back({p[k], n, u, v});
            }
        }
        takeMaterial(static_cast<long>(number(prim.find("material"), -1)));
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

    bool visit(long nodeIndex, const math::Mat4& parent, int depth)
    {
        if (depth > 64)
            return fail("node hierarchy is too deep");
        const Value* node = at("nodes", nodeIndex);
        if (!node)
            return fail("missing node");
        const math::Mat4 world = parent * localMatrix(*node);
        if (const Value* mi = node->find("mesh"))
            if (const Value* m = at("meshes", static_cast<long>(number(mi, -1))))
                if (const Array* prims = array(*m, "primitives"))
                    for (const Value& p : *prims)
                        if (!primitive(p, world))
                            return false;
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
