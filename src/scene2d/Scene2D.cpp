#include "zio/scene2d/Scene2D.h"

namespace zio::scene2d
{

void Scene2D::updateChildren(Node& parent, float dt)
{
    for (std::size_t i = 0; i < parent.children().size();)
    {
        Node* child = parent.children()[i].get();
        child->update(dt);

        const auto& siblings = parent.children();
        if (i >= siblings.size() || siblings[i].get() != child)
            continue;

        updateChildren(*child, dt);
        ++i;
    }
}

void Scene2D::update(float dt)
{
    root_.updateWorldMatrix(math::Mat3::identity());
    root_.update(dt);
    updateChildren(root_, dt);
}

void Scene2D::draw()
{
    root_.visit();
}

}
