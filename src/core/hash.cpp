#include "hash.h"
#include <cstring>

namespace
{
    constexpr std::uint32_t murmur_m = 0x5BD1E995u;
    constexpr int murmur_r = 24;

    std::uint32_t lower(char c)
    {
        const auto b = static_cast<std::uint8_t>(c);
        return b >= 'A' && b <= 'Z' ? b + 32u : b;
    }
}

namespace hash
{
    std::uint32_t murmur2_lower(const char* str, std::uint32_t seed)
    {
        if (!str)
            str = "";
        auto len = static_cast<std::uint32_t>(std::strlen(str));
        std::uint32_t h = seed ^ len;
        while (len >= 4)
        {
            std::uint32_t k = lower(str[0]) | lower(str[1]) << 8 | lower(str[2]) << 16 | lower(str[3]) << 24;
            k *= murmur_m;
            k ^= k >> murmur_r;
            k *= murmur_m;
            h *= murmur_m;
            h ^= k;
            str += 4;
            len -= 4;
        }
        switch (len)
        {
        case 3:
            h ^= lower(str[2]) << 16;
            [[fallthrough]];
        case 2:
            h ^= lower(str[1]) << 8;
            [[fallthrough]];
        case 1:
            h ^= lower(str[0]);
            h *= murmur_m;
            break;
        default:
            break;
        }
        h ^= h >> 13;
        h *= murmur_m;
        h ^= h >> 15;
        return h;
    }
}
