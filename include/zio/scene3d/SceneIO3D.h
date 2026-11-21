#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "zio/core/Platform.h"
#include "zio/scene3d/Scene3D.h"

namespace zio::scene3d
{

ZIO_API std::string serializeScene3D(const Scene3D& scene);
ZIO_API bool saveScene3DFile(const Scene3D& scene, const std::string& path, std::string* error = nullptr);

ZIO_API bool deserializeScene3D(Scene3D& scene, std::string_view json, std::string* error = nullptr);
ZIO_API bool loadScene3DFile(Scene3D& scene, const std::string& path, std::string* error = nullptr);

ZIO_API std::string serializeNode3D(const Node3D& node);
ZIO_API std::unique_ptr<Node3D> deserializeNode3D(std::string_view json, std::string* error = nullptr);

}
