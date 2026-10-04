#include "movement.h"
#include "movement_detail.h"
#include "../combat/combat.h"
#include "../../core/convar.h"
#include "../../core/cstypes.h"
#include "../../core/keys.h"
#include "../../core/math.h"
#include "../../core/settings.h"
#include "../../systems/local.h"
#include "../../systems/prediction.h"
#include <algorithm>
#include <cmath>

namespace
{
    constexpr int max_subticks = 16;
    constexpr float min_strafe_speed = 5.f;
    constexpr float max_start_when = 0.99f;
    constexpr float min_yaw_delta = 0.01f;
    constexpr float default_air_accelerate = 12.f;
    constexpr float default_air_max_wishspeed = 30.f;

    float ideal_angle(float speed, float dt, float wishspeed, float air_accel, float air_max_wishspeed)
    {
        if (speed < 1.f)
            return 15.f;

        const float accel_speed = wishspeed * air_accel * dt;
        float cos_theta = 0.f;

        if (accel_speed >= air_max_wishspeed)
            cos_theta = air_max_wishspeed / (2.f * speed);
        else
            cos_theta = (air_max_wishspeed - accel_speed) / speed;

        cos_theta = std::clamp(cos_theta, -1.f, 1.f);
        return std::fmax(math::rad2deg(std::acos(cos_theta)), 1.f);
    }

    float air_strafer(float vel_x, float vel_y, float target_yaw, float dt, bool side_switch, float wishspeed, float air_accel, float air_max_wishspeed)
    {
        const float speed = std::sqrt(vel_x * vel_x + vel_y * vel_y);
        const float theta = ideal_angle(speed, dt, wishspeed, air_accel, air_max_wishspeed);

        if (speed < 15.f)
            return target_yaw;

        const float vel_angle = math::rad2deg(std::atan2(vel_y, vel_x));
        const float vel_delta = math::helpers::normalized_angle(target_yaw - vel_angle);

        if (std::fabs(vel_delta) > 2.f)
            return math::helpers::normalized_angle(vel_delta > 0.f ? vel_angle + theta : vel_angle - theta);

        return math::helpers::normalized_angle(side_switch ? vel_angle + theta : vel_angle - theta);
    }

    void air_accel_sim(float& vel_x, float& vel_y, float wishdir_yaw, float frame_time, float friction, float wishspeed, float air_accel, float air_max_wishspeed)
    {
        const float yaw_rad = math::deg2rad(wishdir_yaw);
        const float wish_dir_x = std::cos(yaw_rad);
        const float wish_dir_y = std::sin(yaw_rad);

        const float capped = std::fmin(wishspeed, air_max_wishspeed);
        const float dot = vel_x * wish_dir_x + vel_y * wish_dir_y;
        const float add_speed = capped - dot;

        if (add_speed <= 0.f)
            return;

        const float accel_speed = wishspeed * air_accel * friction * frame_time;
        const float step = std::fmin(accel_speed, add_speed);

        vel_x += wish_dir_x * step;
        vel_y += wish_dir_y * step;
    }

    math::vector2 movement_from_buttons(std::uint64_t pressed)
    {
        using namespace cstypes::command_buttons;
        math::vector2 move{};

        if (pressed & in_forward)
            move.x = 1.f;
        else if (pressed & in_back)
            move.x = -1.f;

        if (pressed & in_moveleft)
            move.y = -1.f;
        else if (pressed & in_moveright)
            move.y = 1.f;

        return move;
    }

    float max_step_when(systems::input::frame& frame)
    {
        float max_when = 0.f;
        const int count = frame.step_count();
        for (int i = 0; i < count; ++i)
        {
            if (const systems::input::subtick_event* event = frame.step(i))
                max_when = std::fmax(max_when, event->when);
        }
        return max_when;
    }
}

namespace features::movement
{
    bool test_strafer::is_active() const
    {
        if (settings::g_movement.airstrafe_mode != 1 || !keys::active(settings::g_movement.airstrafe, settings::g_movement.airstrafe_key))
            return false;

        return detail::convar_bool(CONVAR("sv_quantize_movement_input"), true);
    }

    void test_strafer::on_create_move(systems::input::frame& frame)
    {
        using namespace cstypes::command_buttons;

        m_handled_this_tick = false;

        if (!frame.valid() || !is_active())
            return;

        if (g_jumpbug.active_this_tick() || g_airstrafe.handled_this_tick())
            return;

        const auto local = systems::g_local.get();
        if (!local.pawn || !local.is_alive)
            return;

        const auto& prestate = systems::g_prediction.pre();
        if (!prestate.valid)
            return;

        if (detail::ladder_or_noclip(detail::move_type(local.pawn, prestate)))
            return;

        if (detail::on_ground(prestate))
            return;

        if (combat::g_rage.is_firing_this_tick() || combat::g_rage.should_stop())
            return;

        const std::uint64_t current_buttons = frame.real_buttons;
        if (current_buttons & in_sprint)
            return;

        check_button(current_buttons, in_moveleft);
        check_button(current_buttons, in_moveright);
        check_button(current_buttons, in_forward);
        check_button(current_buttons, in_back);
        m_last_buttons = current_buttons;

        const math::vector3 velocity = prestate.networked_velocity;
        const float speed_2d = velocity.length_2d();
        const math::qangle view = frame.view();
        const float command_yaw = view.y;

        const math::vector2 player_move = movement_from_buttons(m_last_pressed);
        if (player_move.x == 0.f && player_move.y == 0.f)
            return;

        if (!std::isfinite(speed_2d) || speed_2d < min_strafe_speed)
            return;

        const float start_when = max_step_when(frame);
        if (start_when >= max_start_when)
            return;

        const int subticks = std::min(max_subticks, systems::input::max_steps - frame.step_count() - detail::reserved_steps - 1);
        if (subticks < 1)
            return;

        const float air_accelerate = detail::convar_positive(CONVAR("sv_airaccelerate"), default_air_accelerate);
        const float max_speed = detail::convar_positive(CONVAR("sv_maxspeed"), prestate.max_speed);
        const float air_max_wishspeed = detail::convar_positive(CONVAR("sv_air_max_wishspeed"), default_air_max_wishspeed);
        const float surface_friction = prestate.surface_friction;

        const float base_yaw_offset = math::rad2deg(std::atan2(-player_move.y, player_move.x));
        const float target_yaw = math::helpers::normalized_angle(command_yaw + base_yaw_offset);

        const float sub_frame = cstypes::tick_interval / static_cast<float>(subticks);
        const float when_step = (1.f - start_when) / static_cast<float>(subticks + 1);

        float acc_yaw = command_yaw;
        float sim_vx = velocity.x;
        float sim_vy = velocity.y;
        int injected = 0;

        for (int i = 1; i <= subticks; ++i)
        {
            const bool entry_side = ((m_substep_counter + i) % 2) == 0;
            const float wishdir_yaw = air_strafer(sim_vx, sim_vy, target_yaw, sub_frame, entry_side, max_speed, air_accelerate, air_max_wishspeed);
            const float target_view_yaw = wishdir_yaw - base_yaw_offset;
            const float yaw_delta = math::helpers::normalized_angle(target_view_yaw - acc_yaw);

            if (!std::isfinite(yaw_delta) || std::fabs(yaw_delta) <= min_yaw_delta)
                break;

            systems::input::subtick_event* step = frame.add_step(start_when + static_cast<float>(i) * when_step);
            if (!step)
                break;

            acc_yaw += yaw_delta;
            step->yaw = acc_yaw;

            air_accel_sim(sim_vx, sim_vy, wishdir_yaw, sub_frame, surface_friction, max_speed, air_accelerate, air_max_wishspeed);
            ++injected;
        }

        if (injected == 0)
            return;

        if (systems::input::subtick_event* restore = frame.add_step(1.f))
        {
            restore->pitch = view.x;
            restore->yaw = command_yaw;
        }

        m_handled_this_tick = true;
        m_substep_counter ^= 1;
    }

    void test_strafer::check_button(std::uint64_t current_buttons, std::uint64_t button)
    {
        detail::track_button(m_last_pressed, m_last_buttons, current_buttons, button);
    }
}
