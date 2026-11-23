#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace zio::pack::lz
{

std::vector<std::uint8_t> compress(const std::uint8_t* source, std::size_t size);

bool decompress(const std::uint8_t* source, std::size_t sourceSize, std::uint8_t* destination,
                std::size_t destinationSize);

}
