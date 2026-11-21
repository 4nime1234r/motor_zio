#include "zio/scene3d/Mesh.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "scene3d/Gltf.h"

namespace zio::scene3d
{

namespace
{

void setError(std::string* error, std::string msg)
{
    if (error)
        *error = std::move(msg);
}

bool parseFloat(std::string_view s, float& out)
{
    const auto r = std::from_chars(s.data(), s.data() + s.size(), out);
    return r.ec == std::errc{} && r.ptr == s.data() + s.size();
}

long resolveIndex(long idx, std::size_t count)
{
    if (idx > 0 && static_cast<std::size_t>(idx) <= count)
        return idx - 1;
    if (idx < 0 && static_cast<std::size_t>(-idx) <= count)
        return static_cast<long>(count) + idx;
    return -1;
}

}

std::shared_ptr<Mesh> Mesh::cube(float size)
{
    using math::Vec3;
    const float h = size * 0.5f;

    struct Face
    {
        Vec3 n, u, v;
    };
    const Face faces[6] = {
        {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}},  {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
        {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}},  {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
        {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},   {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}},
    };

    auto mesh = std::make_shared<Mesh>();
    mesh->vertices.reserve(36);
    for (const Face& f : faces)
    {
        const Vec3 c  = f.n * h;
        const Vec3 p0 = c - f.u * h - f.v * h;
        const Vec3 p1 = c + f.u * h - f.v * h;
        const Vec3 p2 = c + f.u * h + f.v * h;
        const Vec3 p3 = c - f.u * h + f.v * h;
        struct Corner
        {
            Vec3 p;
            float u, v;
        };
        const Corner c0{p0, 0.f, 1.f}, c1{p1, 1.f, 1.f}, c2{p2, 1.f, 0.f}, c3{p3, 0.f, 0.f};
        for (const Corner& k : {c0, c1, c2, c0, c2, c3})
            mesh->vertices.push_back({k.p, f.n, k.u, k.v});
    }
    return mesh;
}

std::shared_ptr<Mesh> Mesh::loadGltf(const std::string& path, std::string* error)
{
    return zio::scene3d::loadGltf(path, error);
}

std::shared_ptr<Mesh> Mesh::loadObj(const std::string& path, std::string* error)
{
    std::ifstream f(path);
    if (!f)
    {
        setError(error, "cannot open \"" + path + "\"");
        return nullptr;
    }

    std::vector<math::Vec3> positions, normals;
    std::vector<std::pair<float, float>> uvs;
    auto mesh = std::make_shared<Mesh>();

    struct Corner
    {
        long v = -1, n = -1, t = -1;
    };

    std::string line;
    std::size_t lineNo = 0;
    while (std::getline(f, line))
    {
        ++lineNo;
        std::istringstream in(line);
        std::string tag;
        in >> tag;
        if (tag == "v" || tag == "vn")
        {
            std::string a, b, c;
            float x = 0, y = 0, z = 0;
            if (!(in >> a >> b >> c) || !parseFloat(a, x) || !parseFloat(b, y) || !parseFloat(c, z))
            {
                setError(error, path + ":" + std::to_string(lineNo) + ": bad \"" + tag + "\" line");
                return nullptr;
            }
            (tag == "v" ? positions : normals).push_back({x, y, z});
        }
        else if (tag == "vt")
        {
            std::string a, b;
            float u = 0, v = 0;
            if (!(in >> a >> b) || !parseFloat(a, u) || !parseFloat(b, v))
            {
                setError(error, path + ":" + std::to_string(lineNo) + ": bad \"vt\" line");
                return nullptr;
            }
            uvs.emplace_back(u, 1.f - v);
        }
        else if (tag == "f")
        {
            std::vector<Corner> corners;
            std::string tok;
            while (in >> tok)
            {
                long idx[3] = {0, 0, 0};
                int part    = 0;
                std::size_t start = 0;
                for (std::size_t i = 0; i <= tok.size() && part < 3; ++i)
                {
                    if (i == tok.size() || tok[i] == '/')
                    {
                        if (i > start)
                        {
                            const auto r = std::from_chars(tok.data() + start, tok.data() + i, idx[part]);
                            if (r.ec != std::errc{})
                            {
                                setError(error, path + ":" + std::to_string(lineNo) + ": bad face index");
                                return nullptr;
                            }
                        }
                        ++part;
                        start = i + 1;
                    }
                }
                Corner c;
                c.v = resolveIndex(idx[0], positions.size());
                c.n = idx[2] != 0 ? resolveIndex(idx[2], normals.size()) : -1;
                c.t = idx[1] != 0 ? resolveIndex(idx[1], uvs.size()) : -1;
                if (c.v < 0)
                {
                    setError(error, path + ":" + std::to_string(lineNo) + ": face refers to a missing vertex");
                    return nullptr;
                }
                corners.push_back(c);
            }
            if (corners.size() < 3)
                continue;
            for (std::size_t i = 1; i + 1 < corners.size(); ++i)
            {
                const Corner tri[3] = {corners[0], corners[i], corners[i + 1]};
                const math::Vec3 p0 = positions[static_cast<std::size_t>(tri[0].v)];
                const math::Vec3 p1 = positions[static_cast<std::size_t>(tri[1].v)];
                const math::Vec3 p2 = positions[static_cast<std::size_t>(tri[2].v)];
                const math::Vec3 faceNormal = (p1 - p0).cross(p2 - p0).normalized();
                for (const Corner& c : tri)
                {
                    const math::Vec3 pos = positions[static_cast<std::size_t>(c.v)];
                    const math::Vec3 nrm = c.n >= 0 ? normals[static_cast<std::size_t>(c.n)].normalized() : faceNormal;
                    const auto uv = c.t >= 0 ? uvs[static_cast<std::size_t>(c.t)] : std::pair<float, float>{0.f, 0.f};
                    mesh->vertices.push_back({pos, nrm, uv.first, uv.second});
                }
            }
        }
    }

    if (mesh->vertices.empty())
    {
        setError(error, "\"" + path + "\" has no triangles");
        return nullptr;
    }
    return mesh;
}

std::shared_ptr<Mesh> Mesh::fromSource(std::string_view source, std::string* error)
{
    if (source == "cube")
        return cube();
    if (source.starts_with("cube:"))
    {
        float size = 0.f;
        if (!parseFloat(source.substr(5), size) || !(size > 0.f))
        {
            setError(error, "bad cube size in \"" + std::string(source) + "\"");
            return nullptr;
        }
        return cube(size);
    }
    if (source.empty())
    {
        setError(error, "empty mesh source");
        return nullptr;
    }
    const std::string path(source);
    std::string ext = std::filesystem::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext == ".gltf" || ext == ".glb")
        return loadGltf(path, error);
    return loadObj(path, error);
}

}
