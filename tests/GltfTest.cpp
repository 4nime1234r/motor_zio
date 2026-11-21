#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "zio/scene3d/Mesh.h"
#include "zio/scene3d/Node3D.h"

namespace
{

int failures = 0;

void check(bool condition, const char* name)
{
    std::printf("%s %s\n", condition ? "[ok]  " : "[FALLO]", name);
    if (!condition)
        ++failures;
}

bool near(float a, float b)
{
    return std::fabs(a - b) < 1e-4f;
}

std::string base64(const std::vector<std::uint8_t>& data)
{
    static const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (std::size_t i = 0; i < data.size(); i += 3)
    {
        const std::uint32_t b0 = data[i];
        const std::uint32_t b1 = i + 1 < data.size() ? data[i + 1] : 0;
        const std::uint32_t b2 = i + 2 < data.size() ? data[i + 2] : 0;
        const std::uint32_t v  = (b0 << 16) | (b1 << 8) | b2;
        out += table[(v >> 18) & 63];
        out += table[(v >> 12) & 63];
        out += i + 1 < data.size() ? table[(v >> 6) & 63] : '=';
        out += i + 2 < data.size() ? table[v & 63] : '=';
    }
    return out;
}

std::vector<std::uint8_t> triangleBuffer()
{
    const float positions[9] = {0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f};
    const float uvs[6]       = {0.f, 0.f, 1.f, 0.f, 0.f, 1.f};
    const std::uint16_t idx[4] = {0, 1, 2, 0};
    std::vector<std::uint8_t> buf(sizeof(positions) + sizeof(uvs) + sizeof(idx));
    std::memcpy(buf.data(), positions, sizeof(positions));
    std::memcpy(buf.data() + sizeof(positions), uvs, sizeof(uvs));
    std::memcpy(buf.data() + sizeof(positions) + sizeof(uvs), idx, sizeof(idx));
    return buf;
}

std::string triangleJson(const std::string& bufferEntry)
{
    return R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],)"
           R"("nodes":[{"translation":[2,0,0],"scale":[2,2,2],"mesh":0}],)"
           R"("meshes":[{"primitives":[{"attributes":{"POSITION":0,"TEXCOORD_0":1},"indices":2,"material":0}]}],)"
           R"("materials":[{"pbrMetallicRoughness":{"baseColorFactor":[0.5,0.25,1,1]}}],)"
           R"("buffers":[)" +
           bufferEntry +
           R"(],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":24},)"
           R"({"buffer":0,"byteOffset":60,"byteLength":6}],)"
           R"("accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},)"
           R"({"bufferView":1,"componentType":5126,"count":3,"type":"VEC2"},)"
           R"({"bufferView":2,"componentType":5123,"count":3,"type":"SCALAR"}]})";
}

void write(const std::filesystem::path& path, const std::string& bytes)
{
    std::ofstream f(path, std::ios::binary);
    f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void checkTriangle(const std::shared_ptr<zio::scene3d::Mesh>& mesh, const char* label)
{
    const std::string name(label);
    check(mesh != nullptr, (name + ": carga").c_str());
    if (!mesh)
        return;
    check(mesh->vertices.size() == 3, (name + ": tres vertices").c_str());
    if (mesh->vertices.size() != 3)
        return;
    check(near(mesh->vertices[0].position.x, 2.f) && near(mesh->vertices[1].position.x, 4.f) &&
              near(mesh->vertices[2].position.y, 2.f),
          (name + ": transformacion del nodo").c_str());
    check(near(mesh->vertices[0].normal.z, 1.f), (name + ": normal calculada").c_str());
    check(near(mesh->vertices[1].u, 1.f) && near(mesh->vertices[2].v, 1.f), (name + ": uvs").c_str());
    check(near(mesh->baseColor[0], 0.5f) && near(mesh->baseColor[1], 0.25f) && near(mesh->baseColor[2], 1.f),
          (name + ": color base").c_str());
}

}

int main()
{
    const auto dir = std::filesystem::temp_directory_path() / "zio_gltf_test";
    std::filesystem::create_directories(dir);
    const auto buffer = triangleBuffer();

    const auto gltfPath = dir / "triangulo.gltf";
    write(gltfPath, triangleJson(R"({"byteLength":66,"uri":"data:application/octet-stream;base64,)" + base64(buffer) +
                                 R"("})"));
    std::string error;
    checkTriangle(zio::scene3d::Mesh::fromSource(gltfPath.string(), &error), "gltf");

    std::string json = triangleJson(R"({"byteLength":66})");
    while (json.size() % 4)
        json += ' ';
    std::vector<std::uint8_t> bin = buffer;
    while (bin.size() % 4)
        bin.push_back(0);
    const std::uint32_t total = 12 + 8 + static_cast<std::uint32_t>(json.size()) + 8 + static_cast<std::uint32_t>(bin.size());
    std::string glb;
    const auto put32 = [&glb](std::uint32_t v) { glb.append(reinterpret_cast<const char*>(&v), 4); };
    glb += "glTF";
    put32(2);
    put32(total);
    put32(static_cast<std::uint32_t>(json.size()));
    put32(0x4E4F534A);
    glb += json;
    put32(static_cast<std::uint32_t>(bin.size()));
    put32(0x004E4942);
    glb.append(reinterpret_cast<const char*>(bin.data()), bin.size());
    const auto glbPath = dir / "triangulo.glb";
    write(glbPath, glb);
    checkTriangle(zio::scene3d::Mesh::fromSource(glbPath.string(), &error), "glb");

    zio::scene3d::MeshNode node;
    check(node.setMeshSource(glbPath.string(), &error), "MeshNode acepta .glb");
    check(near(node.color().r, 0.5f) && near(node.color().b, 1.f), "MeshNode toma el color del material");

    write(dir / "roto.gltf", "{ esto no es json");
    std::string badError;
    check(zio::scene3d::Mesh::fromSource((dir / "roto.gltf").string(), &badError) == nullptr && !badError.empty(),
          "archivo roto da error");
    check(zio::scene3d::Mesh::fromSource((dir / "no_existe.glb").string(), &badError) == nullptr,
          "archivo inexistente da error");

    std::filesystem::remove_all(dir);
    std::printf("%s\n", failures == 0 ? "TODO OK" : "HAY FALLOS");
    return failures == 0 ? 0 : 1;
}
