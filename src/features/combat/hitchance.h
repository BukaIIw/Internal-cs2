#pragma once
#include "hitbox.h"
#include "../../core/math.h"
#include "../../systems/entities.h"
#include <cstdint>

namespace features::combat::hitchance
{
    struct request
    {
        std::uintptr_t local = 0;
        const systems::entities::player* target = nullptr;
        const hitbox::set* boxes = nullptr;
        math::vector3 shoot{};
        math::vector3 point{};
        float minimum_damage = 1.f;
        float target_health = 100.f;
        float range = 8192.f;
        bool penetration = false;
        bool force_shot = false;
        int force_shot_iterations = 1;
        float force_shot_min_spread = 1.f;
    };

    struct result
    {
        bool pass = false;
        float chance = 0.f;
        math::vector3 point{};
    };

    result evaluate(const request& in, float threshold);
    int seed_hit(const request& in, const math::qangle& view, const math::qangle& recoil, int tick);
    void reset();
}
