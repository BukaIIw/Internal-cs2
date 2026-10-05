#pragma once
#include "../core/math.h"
#include <cstdint>

namespace systems::game_events
{
    struct impact
    {
        std::uint64_t time = 0;
        math::vector3 point{};
    };

    struct hurt
    {
        std::uint64_t time = 0;
        int victim = -1;
        int attacker = -1;
        int hitgroup = -1;
        int damage = 0;
    };

    void update();
    void shutdown();
    bool active();
    int impacts_since(std::uint64_t since, impact* out, int max);
    int hurts_since(std::uint64_t since, hurt* out, int max);
}
