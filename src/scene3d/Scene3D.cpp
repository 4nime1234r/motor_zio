#include "zio/scene3d/Scene3D.h"

namespace zio::scene3d
{

void Scene3D::updateChildren(Node3D& parent, float dt)
{
    for (std::size_t i = 0; i < parent.children().size();)
    {
        Node3D* child = parent.children()[i].get();
        child->update(dt);
        child->runScripts(dt);

        const auto& siblings = parent.children();
        if (i >= siblings.size() || siblings[i].get() != child)
            continue;

        updateChildren(*child, dt);
        ++i;
    }
}

void Scene3D::update(float dt)
{
    root_.updateWorldMatrix(math::Mat4::identity());
    root_.update(dt);
    root_.runScripts(dt);
    updateChildren(root_, dt);
    root_.removeDestroyed();
}

void Scene3D::draw(std::uint32_t viewportWidth, std::uint32_t viewportHeight)
{
    auto& renderer = Renderer3D::instance();
    renderer.begin(camera_, light_, viewportWidth, viewportHeight);
    root_.visit(renderer);
    renderer.end();
}

}
