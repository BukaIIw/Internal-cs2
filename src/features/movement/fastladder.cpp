#include "movement.h"
#include "movement_detail.h"
#include "../../core/cstypes.h"
#include "../../core/math.h"
#include "../../core/settings.h"
#include "../../systems/local.h"
#include "../../systems/prediction.h"

namespace
{
    constexpr float ladder_pitch = 89.f;
    constexpr float ladder_yaw_offset = 90.f;
    constexpr int ladder_steps = 2;
}

namespace features::movement
{
    void fastladder::on_create_move(systems::input::frame& frame) const
    {
        using namespace cstypes::command_buttons;

        if (!frame.valid() || !settings::g_movement.fastladder)
            return;

        const auto local = systems::g_local.get();
        if (!local.pawn || !local.is_alive)
            return;

        const auto& prestate = systems::g_prediction.pre();
        if (!prestate.valid || detail::move_type(local.pawn, prestate) != cstypes::move_type::ladder)
            return;

        const std::uint64_t keys = frame.real_buttons & in_move;
        if (!keys)
            return;

        if (detail::free_steps_after_clear(frame) < ladder_steps)
            return;

        const math::qangle view = frame.view();

        const bool forward = (keys & in_forward) != 0;
        const bool back = (keys & in_back) != 0;
        const bool going_up = forward != back ? forward : view.x < 0.f;
        const float yaw = view.y + (going_up ? -ladder_yaw_offset : ladder_yaw_offset);

        detail::set_constant_move(frame, -1.f, going_up ? 1.f : -1.f);
        frame.down() |= in_back | (going_up ? in_moveleft : in_moveright);

        const int count = frame.step_count();
        for (int i = 0; i < count; ++i)
        {
            if (systems::input::subtick_event* event = frame.step(i))
            {
                event->pitch = ladder_pitch;
                event->yaw = yaw;
            }
        }

        if (systems::input::subtick_event* restore = frame.add_step(1.f))
        {
            restore->pitch = view.x;
            restore->yaw = view.y;
        }
    }
}
