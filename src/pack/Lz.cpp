#include "Lz.h"

#include <algorithm>
#include <cstring>

namespace zio::pack::lz
{

namespace
{

constexpr std::size_t kMinMatch     = 4;
constexpr std::size_t kLastLiterals = 5;
constexpr std::size_t kMatchMargin  = 12;
constexpr int kHashBits             = 14;

std::uint32_t read32(const std::uint8_t* p)
{
    std::uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

void writeLength(std::vector<std::uint8_t>& out, std::size_t length)
{
    while (length >= 255)
    {
        out.push_back(255);
        length -= 255;
    }
    out.push_back(static_cast<std::uint8_t>(length));
}

}

std::vector<std::uint8_t> compress(const std::uint8_t* source, std::size_t size)
{
    std::vector<std::uint8_t> out;
    out.reserve(size / 2 + 16);

    std::size_t anchor = 0;
    if (size > kMatchMargin)
    {
        std::vector<std::int32_t> table(static_cast<std::size_t>(1) << kHashBits, -1);
        const std::size_t limit = size - kMatchMargin;
        std::size_t ip          = 0;
        while (ip < limit)
        {
            const std::uint32_t sequence = read32(source + ip);
            const std::size_t hash       = (sequence * 2654435761u) >> (32 - kHashBits);
            const std::int32_t ref       = table[hash];
            table[hash]                  = static_cast<std::int32_t>(ip);
            if (ref >= 0 && ip - static_cast<std::size_t>(ref) <= 65535 && read32(source + ref) == sequence)
            {
                std::size_t length = kMinMatch;
                while (ip + length < size - kLastLiterals && source[static_cast<std::size_t>(ref) + length] == source[ip + length])
                    ++length;

                const std::size_t literals = ip - anchor;
                const std::size_t matchExtra = length - kMinMatch;
                out.push_back(static_cast<std::uint8_t>((std::min<std::size_t>(literals, 15) << 4) |
                                                        std::min<std::size_t>(matchExtra, 15)));
                if (literals >= 15)
                    writeLength(out, literals - 15);
                out.insert(out.end(), source + anchor, source + ip);
                const std::size_t offset = ip - static_cast<std::size_t>(ref);
                out.push_back(static_cast<std::uint8_t>(offset & 0xFF));
                out.push_back(static_cast<std::uint8_t>(offset >> 8));
                if (matchExtra >= 15)
                    writeLength(out, matchExtra - 15);

                ip += length;
                anchor = ip;
                continue;
            }
            ++ip;
        }
    }

    const std::size_t literals = size - anchor;
    out.push_back(static_cast<std::uint8_t>(std::min<std::size_t>(literals, 15) << 4));
    if (literals >= 15)
        writeLength(out, literals - 15);
    out.insert(out.end(), source + anchor, source + size);
    return out;
}

bool decompress(const std::uint8_t* source, std::size_t sourceSize, std::uint8_t* destination, std::size_t destinationSize)
{
    std::size_t ip = 0;
    std::size_t op = 0;
    while (ip < sourceSize)
    {
        const std::uint8_t token = source[ip++];
        std::size_t literals     = token >> 4;
        if (literals == 15)
        {
            std::uint8_t more = 0;
            do
            {
                if (ip >= sourceSize)
                    return false;
                more = source[ip++];
                literals += more;
            } while (more == 255);
        }
        if (literals > sourceSize - ip || literals > destinationSize - op)
            return false;
        std::memcpy(destination + op, source + ip, literals);
        ip += literals;
        op += literals;
        if (ip >= sourceSize)
            break;

        if (sourceSize - ip < 2)
            return false;
        const std::size_t offset = static_cast<std::size_t>(source[ip]) | (static_cast<std::size_t>(source[ip + 1]) << 8);
        ip += 2;
        if (offset == 0 || offset > op)
            return false;
        std::size_t length = static_cast<std::size_t>(token & 15);
        if (length == 15)
        {
            std::uint8_t more = 0;
            do
            {
                if (ip >= sourceSize)
                    return false;
                more = source[ip++];
                length += more;
            } while (more == 255);
        }
        length += kMinMatch;
        if (length > destinationSize - op)
            return false;
        for (std::size_t i = 0; i < length; ++i)
            destination[op + i] = destination[op + i - offset];
        op += length;
    }
    return op == destinationSize;
}

}
