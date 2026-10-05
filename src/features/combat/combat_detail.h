#pragma once
#include "combat.h"
#include "../../core/math.h"
#include "../../systems/entities.h"
#include <cstdint>

namespace features::combat::detail
{
    bool valid_target(const systems::entities::player& player, std::uintptr_t local_pawn, bool teammates);
    bool in_fov_range(const math::qangle& reference, const math::vector3& eye, const math::vector3& origin, float fov);
    bool revolver_primed(const weapon_context& ctx);
    float recoil_scale();
    math::qangle recoil(const weapon_context& ctx);
    float damage(const weapon_context& ctx, const systems::entities::player& target, int hitgroup, float distance);
}
