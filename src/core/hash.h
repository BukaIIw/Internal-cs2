#pragma once
#include <cstddef>
#include <cstdint>

namespace hash
{
    constexpr std::uint32_t fnv1a(const char* str, std::uint32_t value = 0x811C9DC5u)
    {
        return *str ? fnv1a(str + 1, (value ^ static_cast<std::uint8_t>(*str)) * 0x01000193u) : value;
    }

    inline std::uint32_t runtime(const char* str)
    {
        std::uint32_t value = 0x811C9DC5u;
        while (str && *str)
            value = (value ^ static_cast<std::uint8_t>(*str++)) * 0x01000193u;
        return value;
    }

    std::uint32_t murmur2_lower(const char* str, std::uint32_t seed = 0x31415926u);
}

consteval std::uint32_t operator""_hash(const char* str, std::size_t)
{
    return hash::fnv1a(str);
}
