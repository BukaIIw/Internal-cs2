#include "movement.h"
#include "movement_detail.h"
#include "../combat/combat.h"
#include "../../core/cstypes.h"
#include "../../core/settings.h"
#include "../../systems/local.h"
#include "../../systems/prediction.h"
#include <cmath>

namespace
{
    constexpr float min_stop_speed = 15.f;
    constexpr float min_forced_speed = 1.f;
    constexpr float hard_stop_scale = 0.25f;
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
            move = detail::stop_move(frame.view().y, velocity, forced ? detail::weapon_max_speed() * hard_stop_scale : detail::weapon_max_speed());

        detail::set_constant_move(frame, move.x, move.y);
    }
}
