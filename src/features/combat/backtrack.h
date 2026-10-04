#pragma once
#include "hitbox.h"
#include "../../core/math.h"
#include <cstdint>

namespace features::combat::backtrack
{
    constexpr int max_records = 16;

    struct record
    {
        bool valid = false;
        int tick = 0;
        float simulation_time = 0.f;
        bool pitch_broken = false;
        math::vector3 origin{};
        hitbox::set boxes{};
    };

    void update(int tick_base);
    void reset();
    bool tick_valid(int tick, int tick_base);
    int collect(int slot, int tick_base, const record** out, int max);
    bool pitch_broken(int slot);
}
