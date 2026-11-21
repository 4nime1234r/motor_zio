#pragma once

#include "core/EditorContext.h"

namespace zio_editor
{

class SceneHierarchyPanel
{
public:
    void draw(EditorContext& context);

private:
    void drawNode(EditorContext& context, zio::scene3d::Node3D& node);
};

}
