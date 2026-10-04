#pragma once
#include "movement.h"
#include "../combat/combat.h"
#include "../../core/convar.h"
#include "../../core/cstypes.h"
#include "../../core/math.h"
#include "../../core/schema.h"
#include "../../systems/game_reads.h"
#include "../../systems/input.h"
#include "../../systems/prediction.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

namespace features::movement::detail
{
    constexpr float subtick = 1.f / 64.f;
    constexpr int reserved_steps = 2;
    constexpr float default_max_speed = 250.f;
    constexpr float max_max_speed = 1000.f;

    struct landing
    {
        float fraction = 1.f;
        math::vector3 normal{};
        math::vector3 velocity{};
    };

    std::optional<landing> trace_landing(std::uintptr_t pawn, std::uintptr_t movement_services, const systems::prediction::state& prestate, bool holding_duck);

    inline float convar_float(const convars::convar* var, float fallback)
    {
        if (!var || !var->value)
            return fallback;
        const float value = var->get<float>();
        return std::isfinite(value) ? value : fallback;
    }

    inline float convar_positive(const convars::convar* var, float fallback)
    {
        const float value = convar_float(var, fallback);
        return value > 0.f ? value : fallback;
    }

    inline bool convar_bool(const convars::convar* var, bool fallback)
    {
        if (!var || !var->value)
            return fallback;
        return var->get<std::uint8_t>() != 0;
    }

    inline std::uintptr_t movement_services(std::uintptr_t pawn)
    {
        return systems::reads::field_pointer(pawn, SCHEMA("C_BasePlayerPawn", "m_pMovementServices"_hash));
    }

    inline std::uint8_t move_type(std::uintptr_t pawn, const systems::prediction::state& prestate)
    {
        const std::uint32_t offset = SCHEMA("C_BaseEntity", "m_nActualMoveType"_hash);
        if (!offset)
            return prestate.move_type;
        return systems::reads::field<std::uint8_t>(pawn, offset, prestate.move_type);
    }

    inline bool ladder_or_noclip(std::uint8_t type)
    {
        return type == cstypes::move_type::ladder || type == cstypes::move_type::noclip;
    }

    inline bool on_ground(const systems::prediction::state& prestate)
    {
        return (prestate.flags & cstypes::entity_flags::on_ground) != 0;
    }

    inline float velocity_yaw(const math::vector3& velocity)
    {
        return math::rad2deg(std::atan2(velocity.y, velocity.x));
    }

    inline math::vector2 move_toward(float wish_yaw, float view_yaw, float amount)
    {
        const float relative = math::deg2rad(math::helpers::normalized_angle(wish_yaw - view_yaw));
        return { std::clamp(std::cos(relative) * amount, -1.f, 1.f), std::clamp(std::sin(relative) * amount, -1.f, 1.f) };
    }

    inline float weapon_max_speed()
    {
        const auto& ctx = combat::g_shared.ctx();
        if (ctx.valid && std::isfinite(ctx.max_speed) && ctx.max_speed > 1.f && ctx.max_speed < max_max_speed)
            return ctx.max_speed;
        return default_max_speed;
    }

    inline math::vector2 stop_move(float view_yaw, const math::vector3& velocity, float max_speed)
    {
        const float speed = velocity.length_2d();
        if (!std::isfinite(speed) || speed < 0.1f || max_speed <= 0.f)
            return {};
        return move_toward(velocity_yaw(velocity) + 180.f, view_yaw, std::clamp(speed / max_speed, 0.f, 1.f));
    }

    inline float direction_offset(std::uint64_t keys)
    {
        using namespace cstypes::command_buttons;
        float offset = 0.f;
        if (keys & in_moveleft)
            offset += 90.f;
        if (keys & in_moveright)
            offset -= 90.f;
        if (keys & in_forward)
            offset *= 0.5f;
        else if (keys & in_back)
            offset = -offset * 0.5f + 180.f;
        return offset;
    }

    inline void track_button(std::uint64_t& last_pressed, std::uint64_t last_buttons, std::uint64_t current, std::uint64_t button)
    {
        using namespace cstypes::command_buttons;
        const bool fresh = !(last_buttons & button)
            || ((button & in_moveleft) && !(last_pressed & in_moveright))
            || ((button & in_moveright) && !(last_pressed & in_moveleft))
            || ((button & in_forward) && !(last_pressed & in_back))
            || ((button & in_back) && !(last_pressed & in_forward));

        if ((current & button) && fresh)
        {
            if (button & in_moveleft)
                last_pressed &= ~in_moveright;
            else if (button & in_moveright)
                last_pressed &= ~in_moveleft;
            else if (button & in_forward)
                last_pressed &= ~in_back;
            else if (button & in_back)
                last_pressed &= ~in_forward;
            last_pressed |= button;
        }
        else if (!(current & button))
        {
            last_pressed &= ~button;
        }
    }

    inline int free_steps_after_clear(systems::input::frame& frame)
    {
        const int count = frame.step_count();
        int kept = 0;
        for (int i = 0; i < count; ++i)
        {
            const systems::input::subtick_event* event = frame.step(i);
            if (event && !(event->button & cstypes::command_buttons::in_move))
                ++kept;
        }
        return systems::input::max_steps - kept - reserved_steps;
    }

    inline void clear_move_input(systems::input::frame& frame)
    {
        using namespace cstypes::command_buttons;
        frame.remove_steps(in_move);
        const int count = frame.step_count();
        for (int i = 0; i < count; ++i)
        {
            systems::input::subtick_event* event = frame.step(i);
            if (event && event->button == 0)
            {
                event->analog.forward = 0.f;
                event->analog.left = 0.f;
            }
        }
        frame.down() &= ~in_move;
        frame.pressed() &= ~in_move;
        frame.released() &= ~in_move;
    }

    inline void set_constant_move(systems::input::frame& frame, float forward, float left)
    {
        clear_move_input(frame);
        if (systems::input::subtick_event* event = frame.add_step(0.f))
        {
            event->analog.forward = forward - frame.last_impulses.x;
            event->analog.left = left - frame.last_impulses.y;
        }
        frame.forward() = forward;
        frame.left() = left;
    }

    inline bool jump_down(systems::input::frame& frame)
    {
        return (frame.down() & cstypes::command_buttons::in_jump) != 0;
    }
}
