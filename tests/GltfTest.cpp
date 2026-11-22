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

std::string twoMaterialJson(const std::string& bufferEntry)
{
    return R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],)"
           R"("nodes":[{"mesh":0}],)"
           R"("meshes":[{"primitives":[{"attributes":{"POSITION":0,"TEXCOORD_0":1},"indices":2,"material":0},)"
           R"({"attributes":{"POSITION":0,"TEXCOORD_0":1},"indices":2,"material":1},)"
           R"({"attributes":{"POSITION":0,"TEXCOORD_0":1},"indices":2,"material":1}]}],)"
           R"("materials":[{"pbrMetallicRoughness":{"baseColorFactor":[1,0,0,1]}},)"
           R"({"pbrMetallicRoughness":{"baseColorFactor":[0,1,0,1]}}],)"
           R"("buffers":[)" +
           bufferEntry +
           R"(],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":24},)"
           R"({"buffer":0,"byteOffset":60,"byteLength":6}],)"
           R"("accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},)"
           R"({"bufferView":1,"componentType":5126,"count":3,"type":"VEC2"},)"
           R"({"bufferView":2,"componentType":5123,"count":3,"type":"SCALAR"}]})";
}

std::string pbrJson(const std::string& bufferEntry)
{
    return R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],)"
           R"("nodes":[{"mesh":0}],)"
           R"("meshes":[{"primitives":[{"attributes":{"POSITION":0,"TEXCOORD_0":1},"indices":2,"material":0}]}],)"
           R"("materials":[{"pbrMetallicRoughness":{"metallicFactor":0.75,"roughnessFactor":0.3,)"
           R"("metallicRoughnessTexture":{"index":0}},"emissiveFactor":[0.1,0.2,0.3],)"
           R"("normalTexture":{"index":1,"scale":0.5},"occlusionTexture":{"index":2,"strength":0.8},)"
           R"("emissiveTexture":{"index":3}}],)"
           R"("textures":[{"source":0},{"source":1},{"source":2},{"source":3}],)"
           R"("images":[{"uri":"mr.png"},{"uri":"n.png"},{"uri":"ao.png"},{"uri":"e.png"}],)"
           R"("buffers":[)" +
           bufferEntry +
           R"(],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":24},)"
           R"({"buffer":0,"byteOffset":60,"byteLength":6}],)"
           R"("accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},)"
           R"({"bufferView":1,"componentType":5126,"count":3,"type":"VEC2"},)"
           R"({"bufferView":2,"componentType":5123,"count":3,"type":"SCALAR"}]})";
}

std::string extrasJson(const std::string& bufferEntry)
{
    return R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0,1,2]}],)"
           R"("extensionsUsed":["KHR_lights_punctual"],)"
           R"("extensions":{"KHR_lights_punctual":{"lights":[{"type":"spot","color":[1,0.5,0.25],"intensity":3,)"
           R"("range":12,"spot":{"innerConeAngle":0.2,"outerConeAngle":0.6}}]}},)"
           R"("cameras":[{"type":"perspective","perspective":{"yfov":1.0,"znear":0.5,"zfar":50}}],)"
           R"("nodes":[{"mesh":0,"weights":[0.5]},{"camera":0,"translation":[0,2,5]},)"
           R"({"extensions":{"KHR_lights_punctual":{"light":0}},"translation":[1,2,3]}],)"
           R"("animations":[{"name":"subir","samplers":[{"input":3,"output":4,"interpolation":"LINEAR"}],)"
           R"("channels":[{"sampler":0,"target":{"node":0,"path":"weights"}}]}],)"
           R"("meshes":[{"extras":{"targetNames":["subir"]},"primitives":[{"attributes":{"POSITION":0,"COLOR_0":0},)"
           R"("indices":2,"material":0,"targets":[{"POSITION":0}]}]}],)"
           R"("materials":[{"alphaMode":"MASK","alphaCutoff":0.3,"doubleSided":true,)"
           R"("pbrMetallicRoughness":{"baseColorTexture":{"index":0}}}],)"
           R"("textures":[{"source":0}],"images":[{"uri":"data:image/png;base64,AAAA"}],)"
           R"("buffers":[)" +
           bufferEntry +
           R"(],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":24},)"
           R"({"buffer":0,"byteOffset":60,"byteLength":6},{"buffer":0,"byteOffset":68,"byteLength":8},)"
           R"({"buffer":0,"byteOffset":76,"byteLength":8}],)"
           R"("accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},)"
           R"({"bufferView":1,"componentType":5126,"count":3,"type":"VEC2"},)"
           R"({"bufferView":2,"componentType":5123,"count":3,"type":"SCALAR"},)"
           R"({"bufferView":3,"componentType":5126,"count":2,"type":"SCALAR"},)"
           R"({"bufferView":4,"componentType":5126,"count":2,"type":"SCALAR"}]})";
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

    const auto pbrPath = dir / "pbr.gltf";
    write(pbrPath, pbrJson(R"({"byteLength":66,"uri":"data:application/octet-stream;base64,)" + base64(buffer) +
                           R"("})"));
    const auto pbr = zio::scene3d::Mesh::fromSource(pbrPath.string(), &error);
    check(pbr != nullptr, "pbr: carga");
    if (pbr)
    {
        const auto& m = pbr->material;
        check(near(m.metallic, 0.75f) && near(m.roughness, 0.3f), "pbr: metalico y rugosidad");
        check(near(m.emissive[0], 0.1f) && near(m.emissive[1], 0.2f) && near(m.emissive[2], 0.3f), "pbr: emisivo");
        check(near(m.normalScale, 0.5f) && near(m.occlusionStrength, 0.8f), "pbr: escala de normales y oclusion");
        check(m.normalTexture.find("n.png") != std::string::npos &&
                  m.metallicRoughnessTexture.find("mr.png") != std::string::npos &&
                  m.occlusionTexture.find("ao.png") != std::string::npos &&
                  m.emissiveTexture.find("e.png") != std::string::npos,
              "pbr: rutas de las texturas");
    }
    const auto plain = zio::scene3d::Mesh::fromSource(gltfPath.string(), &error);
    check(plain && near(plain->material.metallic, 1.f) && near(plain->material.roughness, 1.f),
          "pbr: valores por defecto de glTF (metalico 1, rugosidad 1)");

    const auto extrasPath = dir / "extras.gltf";
    std::vector<std::uint8_t> extrasBuffer = buffer;
    extrasBuffer.resize(68, 0);
    const float morphKeys[4] = {0.f, 1.f, 0.f, 1.f};
    const auto* morphBytes   = reinterpret_cast<const std::uint8_t*>(morphKeys);
    extrasBuffer.insert(extrasBuffer.end(), morphBytes, morphBytes + sizeof(morphKeys));
    write(extrasPath, extrasJson(R"({"byteLength":84,"uri":"data:application/octet-stream;base64,)" + base64(extrasBuffer) +
                                 R"("})"));
    const auto extras = zio::scene3d::Mesh::fromSource(extrasPath.string(), &error);
    check(extras != nullptr, "extras: carga");
    if (extras)
    {
        check(near(extras->vertices[1].r, 1.f) && near(extras->vertices[1].g, 0.f) && near(extras->vertices[2].g, 1.f),
              "extras: colores de vertice");
        check(extras->material.alphaMode == zio::scene3d::AlphaMode::Mask && near(extras->material.alphaCutoff, 0.3f) &&
                  extras->material.doubleSided,
              "extras: modo alfa, umbral y doble cara");
        check(extras->embeddedImages.size() == 1 &&
                  extras->texturePath.rfind(zio::scene3d::kEmbeddedPrefix, 0) == 0,
              "extras: textura incrustada");
        check(extras->morphTargets.size() == 1 && extras->morphTargets[0].name == "subir" &&
                  extras->morphTargets[0].positions.size() == 3 && near(extras->morphTargets[0].positions[1].x, 1.f) &&
                  extras->morphWeights.size() == 1 && near(extras->morphWeights[0], 0.5f),
              "extras: morph targets, nombre y peso");
        check(extras->cameras.size() == 1 && near(extras->cameras[0].nearZ, 0.5f) &&
                  near(extras->cameras[0].world.m[1][3], 2.f),
              "extras: camara del archivo");
        check(extras->lights.size() == 1 && extras->lights[0].type == zio::scene3d::FileLightType::Spot &&
                  near(extras->lights[0].intensity, 3.f) && near(extras->lights[0].world.m[2][3], 3.f),
              "extras: luz del archivo");

        zio::scene3d::MeshNode extrasNode;
        check(extrasNode.setMeshSource(extrasPath.string(), &error), "extras: MeshNode");
        check(extrasNode.morphTargetCount() == 1 && near(extrasNode.morphWeight(0), 0.5f), "extras: pesos morph en el nodo");
        check(extrasNode.addFileLights() == 1 && extrasNode.children().size() == 1, "extras: luces del archivo como nodos");
        check(extras->morphClips.size() == 1 && extras->morphClips[0].name == "subir" &&
                  near(extras->morphClips[0].duration, 1.f),
              "extras: animacion de pesos morph");
        check(extrasNode.morphAnimationNames().size() == 1 && extrasNode.playMorphAnimation("subir", false),
              "extras: se reproduce la animacion de morph");
        extrasNode.update(0.5f);
        check(near(extrasNode.morphWeight(0), 0.5f), "extras: el peso morph avanza con el tiempo");
        extrasNode.update(1.f);
        check(near(extrasNode.morphWeight(0), 1.f) && !extrasNode.isMorphAnimationPlaying(),
              "extras: la animacion de morph termina en el ultimo valor");
        zio::scene3d::Camera cam;
        check(extrasNode.fileCamera(0, cam) && near(cam.position.y, 2.f) && near(cam.nearZ, 0.5f),
              "extras: camara del archivo en el nodo");
    }

    const auto multiPath = dir / "dos_materiales.gltf";
    write(multiPath, twoMaterialJson(R"({"byteLength":66,"uri":"data:application/octet-stream;base64,)" + base64(buffer) +
                                     R"("})"));
    const auto multi = zio::scene3d::Mesh::fromSource(multiPath.string(), &error);
    check(multi != nullptr && multi->vertices.size() == 9, "varios materiales: nueve vertices");
    if (multi && multi->parts.size() == 2)
    {
        check(true, "varios materiales: dos partes (las primitivas con el mismo material se unen)");
        check(multi->parts[0].first == 0 && multi->parts[0].count == 3 && multi->parts[1].first == 3 &&
                  multi->parts[1].count == 6,
              "varios materiales: rangos de vertices");
        check(near(multi->parts[0].baseColor[0], 1.f) && near(multi->parts[0].baseColor[1], 0.f) &&
                  near(multi->parts[1].baseColor[1], 1.f) && near(multi->parts[1].baseColor[0], 0.f),
              "varios materiales: color de cada parte");
        check(near(multi->baseColor[0], 1.f), "varios materiales: el color de la malla es el de la primera parte");
        zio::scene3d::MeshNode multiNode;
        check(multiNode.setMeshSource(multiPath.string(), &error) && near(multiNode.color().r, 1.f) &&
                  near(multiNode.color().g, 1.f) && near(multiNode.color().b, 1.f),
              "varios materiales: el nodo no tiñe todo con el primer color");
    }
    else
    {
        check(false, "varios materiales: dos partes");
    }

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
