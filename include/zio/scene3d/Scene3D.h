#pragma once

#include <cstdint>

#include "zio/core/Platform.h"
#include "zio/scene/IScene.h"
#include "zio/scene3d/Camera.h"
#include "zio/scene3d/Node3D.h"
#include "zio/scene3d/Renderer3D.h"

namespace zio::scene3d
{

class ZIO_API Scene3D : public scene::IScene
{
public:
    Scene3D()           = default;
    ~Scene3D() override = default;

    Node3D& root() { return root_; }
    const Node3D& root() const { return root_; }

    Camera& camera() { return camera_; }
    const Camera& camera() const { return camera_; }

    Environment& environment() { return environment_; }
    const Environment& environment() const { return environment_; }

    Light& light() { return light_; }
    const Light& light() const { return light_; }

    void update(float dt) override;

    void draw(std::uint32_t viewportWidth, std::uint32_t viewportHeight);

private:
    void updateChildren(Node3D& parent, float dt);

    Node3D root_;
    Camera camera_;
    Light light_;
    Environment environment_;
};

}
