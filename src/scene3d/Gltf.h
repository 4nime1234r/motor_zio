#pragma once

#include <memory>
#include <string>

#include "zio/scene3d/Mesh.h"

namespace zio::scene3d
{

std::shared_ptr<Mesh> loadGltf(const std::string& path, std::string* error);

}
