#pragma once

#include "zio/core/Platform.h"
#include "zio/scene/IScene.h"
#include "zio/scene2d/Node.h"

namespace zio::scene2d
{

class ZIO_API Scene2D : public scene::IScene
{
public:
    Scene2D()           = default;
    ~Scene2D() override = default;

    Node& root() { return root_; }
    const Node& root() const { return root_; }

    void update(float dt) override;

    void draw();

private:
    void updateChildren(Node& parent, float dt);

    Node root_;
};

}
