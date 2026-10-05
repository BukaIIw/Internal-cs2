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

    void update();
    void shutdown();
    bool active();
    int impacts_since(std::uint64_t since, impact* out, int max);
}
