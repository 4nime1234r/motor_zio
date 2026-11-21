#pragma once

#include <string>
#include <string_view>

#include "zio/core/Platform.h"
#include "zio/scene2d/Node.h"

namespace zio::scene2d
{

ZIO_API std::string serializeScene(const Node& root);
ZIO_API bool saveSceneFile(const Node& root, const std::string& path, std::string* error = nullptr);

ZIO_API bool deserializeScene(Node& root, std::string_view json, std::string* error = nullptr);
ZIO_API bool loadSceneFile(Node& root, const std::string& path, std::string* error = nullptr);

}
