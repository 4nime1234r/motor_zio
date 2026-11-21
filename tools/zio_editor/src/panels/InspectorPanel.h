#pragma once

#include "core/EditorContext.h"
#include "core/EulerCache.h"

namespace zio_editor
{

class InspectorPanel
{
public:
    void draw(EditorContext& context);

private:
    EulerCache eulerCache_;
};

}
