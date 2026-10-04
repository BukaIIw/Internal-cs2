#include "movement.h"
#include "movement_detail.h"
#include "../../core/convar.h"
#include "../../core/cstypes.h"
#include "../../core/keys.h"
#include "../../core/settings.h"
#include "../../systems/local.h"
#include "../../systems/prediction.h"
#include <algorithm>
#include <cmath>
#include <optional>

namespace
{
    struct hop_context
    {
        std::uintptr_t pawn = 0;
        std::uintptr_t services = 0;
    };

    std::optional<hop_context> make_context(systems::input::frame& frame)
    {
        if (!keys::active(settings::g_movement.bhop, settings::g_movement.bhop_key))
            return std::nullopt;

        if (features::movement::detail::convar_bool(CONVAR("sv_autobunnyhopping"), false))
            return std::nullopt;

        if (!frame.really_held(cstypes::command_buttons::in_jump))
            return std::nullopt;

        if (features::movement::g_jumpbug.active_this_tick())
            return std::nullopt;

        const auto local = systems::g_local.get();
        if (!local.pawn || !local.is_alive)
            return std::nullopt;

        const auto& prestate = systems::g_prediction.pre();
        if (!prestate.valid)
            return std::nullopt;

        if (features::movement::detail::ladder_or_noclip(features::movement::detail::move_type(local.pawn, prestate)))
            return std::nullopt;

        const std::uintptr_t services = features::movement::detail::movement_services(local.pawn);
        if (!services)
            return std::nullopt;

        return hop_context{ local.pawn, services };
    }

    float landing_when(float fraction)
    {
        return std::clamp(std::round(fraction * 64.f) / 64.f, 1.f / 64.f, 63.f / 64.f);
    }
}

namespace features::movement
{
    void bhop::on_create_move(systems::input::frame& frame)
    {
        using namespace cstypes::command_buttons;

        if (!frame.valid())
            return;

        const auto context = make_context(frame);
        if (!context)
        {
            m_jump_sent = detail::jump_down(frame);
            return;
        }

        const bool was_down = m_jump_sent;

        frame.remove_steps(in_jump);
        frame.pressed() &= ~in_jump;
        frame.released() &= ~in_jump;

        const auto& prestate = systems::g_prediction.pre();

        if (detail::on_ground(prestate))
        {
            if (was_down)
                frame.release(in_jump, 0.f);
            frame.press(in_jump, detail::subtick);
        }
        else if (const auto landing = detail::trace_landing(context->pawn, context->services, prestate, frame.really_held(in_duck)))
        {
            const float when = landing_when(landing->fraction);
            const float release_when = std::clamp(when - detail::subtick, 1.f / 64.f, 63.f / 64.f);
            if (release_when < when)
                frame.release(in_jump, release_when);
            else if (was_down)
                frame.release(in_jump, 0.f);
            frame.press(in_jump, when);
        }
        else
        {
            if (was_down)
                frame.release(in_jump, 0.f);
            frame.down() &= ~in_jump;
        }

        m_jump_sent = detail::jump_down(frame);
    }
}
