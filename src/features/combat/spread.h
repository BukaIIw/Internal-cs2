#pragma once
#include "combat.h"
#include "../../core/math.h"
#include <cstdint>

namespace features::combat::spread
{
    constexpr int seed_count = 64;
    constexpr int max_bullets = 16;

    math::vector2 offset(std::uint16_t def, int mode, float recoil_index, std::int32_t seed, float inaccuracy, float spread);
    void table(const weapon_context& ctx, math::vector2 (&out)[seed_count]);
    bool available();
    bool bullet(const weapon_context& ctx, const math::qangle& view, const math::qangle& recoil, int tick, math::vector3& out);
    bool compensate(const weapon_context& ctx, const math::qangle& desired, const math::qangle& recoil, int tick, math::qangle& out);
}
