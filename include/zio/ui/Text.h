#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "zio/core/Platform.h"

namespace zio::ui::text
{

ZIO_API char32_t decode(std::string_view s, std::size_t& index);
ZIO_API void append(std::string& out, char32_t codepoint);
ZIO_API std::size_t nextBoundary(std::string_view s, std::size_t index);
ZIO_API std::size_t previousBoundary(std::string_view s, std::size_t index);
ZIO_API std::size_t nextWordBoundary(std::string_view s, std::size_t index);
ZIO_API std::size_t previousWordBoundary(std::string_view s, std::size_t index);
ZIO_API bool isWordCharacter(char32_t codepoint);

}
