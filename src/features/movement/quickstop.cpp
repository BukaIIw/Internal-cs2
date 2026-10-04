#include "movement.h"
#include "movement_detail.h"
#include "../combat/combat.h"
#include "../../core/convar.h"
#include "../../core/cstypes.h"
#include "../../core/settings.h"
#include "../../systems/local.h"
#include "../../systems/prediction.h"
#include <algorithm>
#include <cmath>

namespace
{
    constexpr float min_stop_speed = 15.f;
    constexpr float min_forced_speed = 1.f;
    constexpr float default_friction = 5.2f;
    constexpr float default_stop_speed = 80.f;
    constexpr float default_accelerate = 5.5f;

    float convar_or(const convars::convar* cvar, float fallback)
    {
        if (!cvar || !cvar->value)
            return fallback;
        const float value = cvar->get<float>();
        return std::isfinite(value) && value > 0.f ? value : fallback;
    }

    float stop_fraction(float speed, float max_speed, float surface_friction)
    {
        const float friction = convar_or(CONVAR("sv_friction"), default_friction) * surface_friction;
        const float control = std::max(speed, convar_or(CONVAR("sv_stopspeed"), default_stop_speed));
        const float after = std::max(0.f, speed - control * friction * cstypes::tick_interval);
        const float accel = convar_or(CONVAR("sv_accelerate"), default_accelerate) * max_speed * cstypes::tick_interval * surface_friction;
        if (!(after > 0.f) || !(accel > 0.f))
            return 0.f;
        return std::clamp(after / accel, 0.f, 1.f);
    }
}

namespace features::movement
{
    void quickstop::on_create_move(systems::input::frame& frame) const
    {
        using namespace cstypes::command_buttons;

        if (!frame.valid())
            return;

        const bool forced = combat::g_rage.should_stop();
        if (!settings::g_movement.quickstop && !forced)
            return;

        const auto& prestate = systems::g_prediction.pre();
        if (!prestate.valid || (!forced && !detail::on_ground(prestate)))
            return;

        const auto local = systems::g_local.get();
        if (!local.pawn || !local.is_alive)
            return;

        if (detail::ladder_or_noclip(detail::move_type(local.pawn, prestate)))
            return;

        if (!forced && (frame.real_buttons & (in_move | in_jump)))
            return;

        const math::vector3& velocity = prestate.networked_velocity;
        const float speed = velocity.length_2d();
        if (!std::isfinite(speed))
            return;

        if (!forced && speed <= min_stop_speed)
            return;

        if (detail::free_steps_after_clear(frame) < 1)
            return;

        math::vector2 move{};
        if (speed > min_forced_speed)
        {
            const float max_speed = std::min(prestate.max_speed, detail::weapon_max_speed());
            const float amount = stop_fraction(speed, max_speed, prestate.surface_friction);
            move = detail::move_toward(detail::velocity_yaw(velocity) + 180.f, frame.view().y, amount);
        }

        detail::set_constant_move(frame, move.x, move.y);
    }
}
