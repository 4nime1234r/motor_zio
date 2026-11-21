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
#include "zio/scene3d/Scene3D.h"
#include "zio/scene3d/SceneIO3D.h"

using namespace zio::scene3d;

namespace
{

int failures = 0;

void check(bool condition, const char* name)
{
    std::printf("%s %s\n", condition ? "[ok]  " : "[FALLO]", name);
    if (!condition)
        ++failures;
}

bool near(float a, float b, float eps = 1e-3f)
{
    return std::fabs(a - b) < eps;
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

template <typename T>
void append(std::vector<std::uint8_t>& buf, const T* data, std::size_t count)
{
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(data);
    buf.insert(buf.end(), bytes, bytes + sizeof(T) * count);
}

std::string armJson(const std::vector<std::uint8_t>& buf, bool twoSkins = false)
{
    const std::string extraNode = twoSkins ? R"(,{"name":"malla2","mesh":0,"skin":1})" : "";
    const std::string extraSkin = twoSkins ? R"(,{"joints":[0,1],"inverseBindMatrices":4})" : "";
    return std::string(R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0,2)") + (twoSkins ? ",3" : "") + "]}],"
           R"("nodes":[{"name":"raiz","children":[1],"translation":[0,0,0]},)"
           R"({"name":"codo","translation":[1,0,0]},)"
           R"({"name":"malla","mesh":0,"skin":0})" +
           extraNode + R"(],)"
           R"("skins":[{"joints":[0,1],"inverseBindMatrices":4})" + extraSkin + R"(],)"
           R"("meshes":[{"primitives":[{"attributes":{"POSITION":0,"JOINTS_0":1,"WEIGHTS_0":2},"indices":3}]}],)"
           R"("animations":[{"name":"doblar","channels":[{"sampler":0,"target":{"node":1,"path":"rotation"}}],)"
           R"("samplers":[{"input":5,"output":6,"interpolation":"LINEAR"}]}],)"
           R"("buffers":[{"byteLength":)" +
           std::to_string(buf.size()) + R"(,"uri":"data:application/octet-stream;base64,)" + base64(buf) +
           R"("}],)"
           R"("bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":24},)"
           R"({"buffer":0,"byteOffset":60,"byteLength":48},{"buffer":0,"byteOffset":108,"byteLength":6},)"
           R"({"buffer":0,"byteOffset":116,"byteLength":128},{"buffer":0,"byteOffset":244,"byteLength":8},)"
           R"({"buffer":0,"byteOffset":252,"byteLength":32}],)"
           R"("accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},)"
           R"({"bufferView":1,"componentType":5123,"count":3,"type":"VEC4"},)"
           R"({"bufferView":2,"componentType":5126,"count":3,"type":"VEC4"},)"
           R"({"bufferView":3,"componentType":5123,"count":3,"type":"SCALAR"},)"
           R"({"bufferView":4,"componentType":5126,"count":2,"type":"MAT4"},)"
           R"({"bufferView":5,"componentType":5126,"count":2,"type":"SCALAR"},)"
           R"({"bufferView":6,"componentType":5126,"count":2,"type":"VEC4"}]})";
}

std::vector<std::uint8_t> armBuffer()
{
    std::vector<std::uint8_t> buf;
    const float positions[9] = {0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 2.f, 0.f, 0.f};
    append(buf, positions, 9);
    const std::uint16_t joints[12] = {0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
    append(buf, joints, 12);
    const float weights[12] = {1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f};
    append(buf, weights, 12);
    const std::uint16_t idx[3] = {0, 1, 2};
    append(buf, idx, 3);
    const std::uint16_t pad = 0;
    append(buf, &pad, 1);
    const float ibm[32] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1,
                           1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -1, 0, 0, 1};
    append(buf, ibm, 32);
    const float times[2] = {0.f, 1.f};
    append(buf, times, 2);
    const float s = std::sin(3.14159265f / 4.f), c = std::cos(3.14159265f / 4.f);
    const float rot[8] = {0.f, 0.f, 0.f, 1.f, 0.f, 0.f, s, c};
    append(buf, rot, 8);
    return buf;
}

}

int main()
{
    const auto dir = std::filesystem::temp_directory_path() / "zio_skeleton_test";
    std::filesystem::create_directories(dir);
    const auto path = dir / "brazo.gltf";
    {
        std::ofstream f(path, std::ios::binary);
        const std::string json = armJson(armBuffer());
        f.write(json.data(), static_cast<std::streamsize>(json.size()));
    }

    std::string error;
    auto mesh = Mesh::fromSource(path.string(), &error);
    if (!mesh)
        std::printf("error: %s\n", error.c_str());
    check(mesh != nullptr, "carga el glTF con esqueleto");
    if (!mesh)
        return 1;
    check(mesh->skin != nullptr, "tiene esqueleto");
    check(mesh->skin && mesh->skin->joints.size() == 2, "dos huesos");
    check(mesh->skin && mesh->skin->joints[1].parent == 0, "el codo cuelga de la raiz");
    check(mesh->skin && mesh->skin->clips.size() == 1 && mesh->skin->clips[0].name == "doblar", "una animacion llamada doblar");
    check(mesh->skin && near(mesh->skin->clips[0].duration, 1.f), "la animacion dura un segundo");

    MeshNode node;
    check(node.setMeshSource(path.string(), &error), "MeshNode acepta la malla con huesos");
    check(node.hasSkeleton(), "MeshNode detecta el esqueleto");
    check(node.animationNames().size() == 1, "MeshNode lista las animaciones");
    check(!node.playAnimation("no_existe"), "una animacion inexistente falla");
    check(node.playAnimation("doblar", false), "reproduce doblar");

    Mesh bind = *mesh;
    std::vector<zio::math::Mat4> matrices;
    mesh->skin->skinMatrices(mesh->skin->findClip("doblar"), 0.f, matrices);
    Mesh posed;
    mesh->skin->apply(bind, matrices, posed);
    check(near(posed.vertices[2].position.x, 2.f) && near(posed.vertices[2].position.y, 0.f),
          "en el instante 0 la malla esta en reposo");

    mesh->skin->skinMatrices(mesh->skin->findClip("doblar"), 1.f, matrices);
    mesh->skin->apply(bind, matrices, posed);
    check(near(posed.vertices[0].position.x, 0.f) && near(posed.vertices[0].position.y, 0.f),
          "el vertice de la raiz no se mueve");
    check(near(posed.vertices[1].position.x, 1.f) && near(posed.vertices[1].position.y, 0.f),
          "el vertice del codo queda en el codo");
    check(near(posed.vertices[2].position.x, 1.f) && near(posed.vertices[2].position.y, 1.f),
          "al doblar 90 grados el extremo sube");

    mesh->skin->skinMatrices(mesh->skin->findClip("doblar"), 0.5f, matrices);
    mesh->skin->apply(bind, matrices, posed);
    const float expect = std::cos(3.14159265f / 4.f) + 1.f;
    check(near(posed.vertices[2].position.x, expect, 2e-3f), "a mitad de camino interpola la rotacion");

    node.update(0.4f);
    check(node.isAnimationPlaying() && near(node.animationTime(), 0.4f), "el tiempo avanza con update");
    node.update(2.f);
    check(!node.isAnimationPlaying(), "sin bucle se detiene al terminar");

    Scene3D scene;
    auto* first = scene.root().addChild<MeshNode>();
    first->setName("brazo");
    first->setMeshSource(path.string(), &error);
    first->setAnimationSpeed(2.f);
    first->playAnimation("doblar", true);
    const std::string text = serializeScene3D(scene);
    Scene3D loaded;
    check(deserializeScene3D(loaded, text, &error), "la escena con animacion se vuelve a leer");
    if (loaded.root().children().size() == 1)
    {
        const auto* back = dynamic_cast<const MeshNode*>(loaded.root().children()[0].get());
        check(back && back->isAnimationPlaying() && back->currentAnimation() == "doblar" && back->animationLoops() &&
                  near(back->animationSpeed(), 2.f),
              "se conserva la animacion en curso");
    }
    else
    {
        check(false, "un hijo tras leer");
    }

    {
        const auto twoPath = dir / "dos.gltf";
        {
            std::ofstream f(twoPath, std::ios::binary);
            const std::string json = armJson(armBuffer(), true);
            f.write(json.data(), static_cast<std::streamsize>(json.size()));
        }
        auto two = Mesh::fromSource(twoPath.string(), &error);
        if (!two)
            std::printf("error: %s\n", error.c_str());
        check(two && two->skin && two->skin->joints.size() == 4, "dos esqueletos se unen en uno de cuatro huesos");
        check(two && two->vertices.size() == 6 && two->skin->vertices.size() == 6, "los vertices de ambos modelos se conservan");
        if (two && two->skin && two->vertices.size() == 6)
        {
            check(two->skin->clips.size() == 1 && two->skin->clips[0].channels.size() == 2,
                  "la animacion mueve los huesos de ambos esqueletos");
            std::vector<zio::math::Mat4> m;
            two->skin->skinMatrices(two->skin->findClip("doblar"), 1.f, m);
            Mesh out;
            two->skin->apply(*two, m, out);
            check(near(out.vertices[2].position.y, 1.f) && near(out.vertices[5].position.y, 1.f),
                  "ambos modelos se deforman");
        }
    }

    {
        Skin cubic;
        SkinJoint joint;
        cubic.joints.push_back(joint);
        AnimationClip clip;
        AnimationChannel ch;
        ch.joint         = 0;
        ch.path          = AnimationPath::Translation;
        ch.interpolation = Interpolation::CubicSpline;
        ch.times         = {0.f, 1.f};
        ch.values        = {0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0};
        clip.channels.push_back(ch);
        clip.duration = 1.f;
        cubic.clips.push_back(clip);
        std::vector<JointPose> pose;
        cubic.samplePose(&cubic.clips[0], 0.5f, pose);
        check(near(pose[0].translation.x, 0.75f), "la curva cubica usa las tangentes");
        cubic.samplePose(&cubic.clips[0], 1.f, pose);
        check(near(pose[0].translation.x, 1.f), "la curva cubica termina en el ultimo valor");
        cubic.clips[0].channels[0].values = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0};
        cubic.samplePose(&cubic.clips[0], 0.5f, pose);
        check(near(pose[0].translation.x, 0.5f), "con tangentes nulas la curva pasa por la mitad");
    }

    {
        Skin wide;
        for (int i = 0; i < 8; ++i)
            wide.joints.push_back(SkinJoint{});
        VertexSkin vs;
        vs.joints[7]  = 7;
        vs.weights[7] = 1.f;
        wide.vertices.push_back(vs);
        Mesh bind;
        bind.vertices.push_back({{0.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, 0.f, 0.f});
        std::vector<zio::math::Mat4> m(8);
        m[7] = zio::math::Mat4::translation({0.f, 5.f, 0.f});
        Mesh out;
        wide.apply(bind, m, out);
        check(near(out.vertices[0].position.y, 5.f), "la octava influencia por vertice funciona");
    }

    {
        Skin skin;
        SkinJoint joint;
        skin.joints.push_back(joint);
        std::vector<JointPose> a, b, mixed;
        skin.restPose(a);
        b = a;
        b[0].translation = {2.f, 0.f, 0.f};
        b[0].rotation    = zio::math::Quaternion::fromAxisAngle({0.f, 0.f, 1.f}, 90.f);
        Skin::blendPoses(a, b, 0.5f, mixed);
        check(near(mixed[0].translation.x, 1.f), "la mezcla de poses interpola la traslacion");
        const zio::math::Quaternion expected = zio::math::Quaternion::fromAxisAngle({0.f, 0.f, 1.f}, 45.f);
        check(near(mixed[0].rotation.z, expected.z) && near(mixed[0].rotation.w, expected.w),
              "la mezcla de poses interpola la rotacion");
    }

    {
        MeshNode fade;
        fade.setMeshSource(path.string(), &error);
        fade.playAnimation("doblar", true);
        fade.update(0.5f);
        check(fade.playAnimation("doblar", true, 0.4f) && fade.isBlending(), "la transicion empieza");
        fade.update(0.2f);
        check(fade.isBlending(), "la transicion sigue a la mitad");
        fade.update(0.3f);
        check(!fade.isBlending(), "la transicion termina");
    }

    std::filesystem::remove_all(dir);
    std::printf("%s\n", failures == 0 ? "TODO OK" : "HAY FALLOS");
    return failures == 0 ? 0 : 1;
}
