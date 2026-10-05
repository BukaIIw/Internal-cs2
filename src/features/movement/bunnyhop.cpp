#include "movement.h"
#include "movement_detail.h"
#include "../../core/convar.h"
#include "../../core/cstypes.h"
#include "../../core/hash.h"
#include "../../core/keys.h"
#include "../../core/schema.h"
#include "../../core/settings.h"
#include "../../systems/game_reads.h"
#include "../../systems/local.h"
#include "../../systems/prediction.h"
#include <algorithm>
#include <cmath>
#include <optional>

namespace
{
    namespace reads = systems::reads;

    constexpr int surf_grace_ticks = 4;
    constexpr float bias_step = 1.f / 128.f;
    constexpr float bias_decay = 1.f / 512.f;
    constexpr float max_bias = 3.f / 64.f;
    constexpr int decay_streak = 8;

    struct hop_context
    {
        std::uintptr_t pawn = 0;
        std::uintptr_t services = 0;
        int last_jump_tick = 0;
        bool duck_locked = false;
    };

    bool surfing(std::uintptr_t services)
    {
        if (reads::field<std::uint8_t>(services, SCHEMA("CCSPlayer_MovementServices", "m_bWasSurfing"_hash)) != 0)
            return true;
        const float since = reads::field<float>(services, SCHEMA("CCSPlayer_MovementServices", "m_flTicksSinceLastSurfingDetected"_hash), 1e6f);
        return std::isfinite(since) && since >= 0.f && since < static_cast<float>(surf_grace_ticks);
    }

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
        if (!services || surfing(services))
            return std::nullopt;

        hop_context out{ local.pawn, services };
        out.last_jump_tick = reads::field<int>(services, SCHEMA("CCSPlayer_MovementServices", "m_nLastJumpTick"_hash));
        out.duck_locked = reads::field<std::uint8_t>(services, SCHEMA("CCSPlayer_MovementServices", "m_duckUntilOnGround"_hash)) != 0;
        return out;
    }

    float landing_when(float fraction, float bias)
    {
        return std::clamp(std::round((fraction + bias) * 64.f) / 64.f, 1.f / 64.f, 63.f / 64.f);
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
            m_air_pressed = false;
            return;
        }

        const bool jumped = context->last_jump_tick != m_last_jump_tick;
        m_last_jump_tick = context->last_jump_tick;

        const bool was_down = m_jump_sent;

        frame.remove_steps(in_jump);
        frame.pressed() &= ~in_jump;
        frame.released() &= ~in_jump;

        const auto& prestate = systems::g_prediction.pre();
        const bool grounded = detail::on_ground(prestate);

        if (m_air_pressed)
        {
            if (jumped)
            {
                if (++m_streak >= decay_streak)
                {
                    m_streak = 0;
                    m_bias = std::max(m_bias - bias_decay, 0.f);
                }
            }
            else if (grounded)
            {
                m_streak = 0;
                m_bias = std::min(m_bias + bias_step, max_bias);
            }
            m_air_pressed = false;
        }

        if (grounded)
        {
            if (was_down)
                frame.release(in_jump, 0.f);
            frame.press(in_jump, detail::subtick);
        }
        else if (const auto landing = detail::trace_landing(context->pawn, context->services, prestate, frame.really_held(in_duck) || context->duck_locked))
        {
            const float when = landing_when(landing->fraction, m_bias);
            const float release_when = std::clamp(when - detail::subtick, 1.f / 64.f, 63.f / 64.f);
            if (release_when < when)
                frame.release(in_jump, release_when);
            else if (was_down)
                frame.release(in_jump, 0.f);
            frame.press(in_jump, when);
            m_air_pressed = true;
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
