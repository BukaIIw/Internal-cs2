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
    constexpr int max_subticks = 32;
    constexpr float min_strafe_speed = 10.f;
    constexpr float fast_speed = 80.f;
    constexpr float reverse_delta = 170.f;
    constexpr float max_ideal_angle = 45.f;
    constexpr float default_air_accelerate = 12.f;
    constexpr float default_air_max_wishspeed = 30.f;
}

namespace features::movement
{
    void airstrafe::on_create_move(systems::input::frame& frame)
    {
        using namespace cstypes::command_buttons;

        m_handled_this_tick = false;

        if (!frame.valid() || g_jumpbug.active_this_tick())
            return;

        const auto& prestate = systems::g_prediction.pre();
        if (!prestate.valid || detail::on_ground(prestate))
            return;

        const auto local = systems::g_local.get();
        if (!local.pawn || !local.is_alive)
            return;

        if (detail::ladder_or_noclip(detail::move_type(local.pawn, prestate)))
            return;

        const std::uint64_t current_buttons = frame.real_buttons;
        const bool bound = keys::active(settings::g_movement.airstrafe, settings::g_movement.airstrafe_key);

        if (current_buttons & in_sprint)
        {
            if (!bound || detail::free_steps_after_clear(frame) < 1)
                return;

            const math::vector3& velocity = prestate.networked_velocity;
            math::vector2 move{};
            if (velocity.length_2d() > min_strafe_speed)
                move = detail::move_toward(detail::velocity_yaw(velocity) + 180.f, frame.view().y, 1.f);

            detail::set_constant_move(frame, move.x, move.y);
            m_handled_this_tick = true;
            return;
        }

        const bool wants_stop = combat::g_rage.should_stop();
        const bool enabled = bound && settings::g_movement.airstrafe_mode == 0;

        if ((!enabled && !wants_stop) || combat::g_rage.is_firing_this_tick())
            return;

        if (!wants_stop)
        {
            check_button(current_buttons, in_moveleft);
            check_button(current_buttons, in_moveright);
            check_button(current_buttons, in_forward);
            check_button(current_buttons, in_back);
            m_last_buttons = current_buttons;
        }

        const int subtick_count = std::min(max_subticks, detail::free_steps_after_clear(frame));
        if (subtick_count < 1)
            return;

        const float air_accelerate = detail::convar_positive(CONVAR("sv_airaccelerate"), default_air_accelerate);
        const float air_max_wishspeed = detail::convar_positive(CONVAR("sv_air_max_wishspeed"), default_air_max_wishspeed);
        const float max_speed = prestate.max_speed;
        const float stop_speed = detail::weapon_max_speed();
        const float frame_time = cstypes::tick_interval / static_cast<float>(subtick_count);
        const float accel_speed = air_accelerate * max_speed * frame_time * prestate.surface_friction;
        const float half_accel = accel_speed * 0.5f;

        const float yaw_offset = wants_stop ? 0.f : detail::direction_offset(m_last_pressed);
        const bool has_direction_input = (m_last_pressed & in_move) != 0;
        const bool effective_wants_stop = wants_stop || (settings::g_movement.airstrafe_fully_directional && !has_direction_input);

        const float view_yaw = frame.view().y;
        math::vector3 velocity = prestate.networked_velocity;

        if (effective_wants_stop && velocity.length_2d() <= min_strafe_speed)
        {
            const math::vector2 move = detail::stop_move(view_yaw, velocity, stop_speed);
            detail::set_constant_move(frame, move.x, move.y);
            m_handled_this_tick = true;
            return;
        }

        const math::vector2 user_move{ frame.forward(), frame.left() };
        math::vector2 impulses = frame.last_impulses;

        if (impulses.y < 0.f)
            m_side_switch = false;
        else if (impulses.y > 0.f)
            m_side_switch = true;

        detail::clear_move_input(frame);

        math::vector3 view_forward{};
        math::vector3 view_right{};
        math::helpers::angle_vectors_2d(view_yaw, view_forward, view_right);

        for (int i = 0; i < subtick_count; ++i)
        {
            if (velocity.length_2d() > 0.0001f)
            {
                math::vector3 wish_dir{
                    (view_forward.x * impulses.x - view_right.x * impulses.y) * max_speed,
                    (view_forward.y * impulses.x - view_right.y * impulses.y) * max_speed,
                    0.f
                };

                float wish_speed = wish_dir.length_2d();
                if (wish_speed > 0.0001f)
                {
                    wish_dir.x /= wish_speed;
                    wish_dir.y /= wish_speed;
                }

                wish_speed = std::fmin(wish_speed, max_speed);

                const float capped_wish = std::fmin(wish_speed, air_max_wishspeed);
                const float current_speed = velocity.x * wish_dir.x + velocity.y * wish_dir.y;
                const float add_speed = capped_wish - current_speed;

                if (add_speed > 0.f)
                {
                    const float gain = std::fmin(half_accel, add_speed);
                    velocity.x += wish_dir.x * gain;
                    velocity.y += wish_dir.y * gain;
                }
            }

            const float speed_2d = velocity.length_2d();
            math::vector2 move = user_move;

            if (speed_2d >= min_strafe_speed)
            {
                if (effective_wants_stop)
                {
                    move = detail::stop_move(view_yaw, velocity, stop_speed);
                }
                else
                {
                    const float velocity_angle = detail::velocity_yaw(velocity);
                    const float optimal_floor = std::fmax(half_accel, air_max_wishspeed - half_accel);
                    const float ideal_angle = std::clamp(math::rad2deg(std::atan(optimal_floor / speed_2d)), 0.f, max_ideal_angle);
                    const float strafe_angle = 90.f - ideal_angle;

                    const float target_yaw = math::helpers::normalized_angle(m_angles.y + yaw_offset);
                    const float velocity_delta = math::helpers::normalized_angle(target_yaw - velocity_angle);

                    float wish_yaw = 0.f;
                    if (speed_2d > fast_speed && (std::fabs(velocity_delta) > reverse_delta || velocity_delta > ideal_angle))
                        wish_yaw = velocity_angle + strafe_angle;
                    else if (-ideal_angle <= velocity_delta || speed_2d <= fast_speed)
                        wish_yaw = m_side_switch ? target_yaw - strafe_angle : target_yaw + strafe_angle;
                    else
                        wish_yaw = velocity_angle - strafe_angle;

                    move = detail::move_toward(wish_yaw, view_yaw, 1.f);
                }
            }

            systems::input::subtick_event* step = frame.add_step(static_cast<float>(i) / static_cast<float>(subtick_count));
            if (!step)
                break;

            step->analog.forward = move.x - impulses.x;
            step->analog.left = move.y - impulses.y;
            impulses = move;

            if (!effective_wants_stop)
                m_side_switch = !m_side_switch;
        }

        frame.forward() = impulses.x;
        frame.left() = impulses.y;
        m_handled_this_tick = true;
    }

    void airstrafe::check_button(std::uint64_t current_buttons, std::uint64_t button)
    {
        detail::track_button(m_last_pressed, m_last_buttons, current_buttons, button);
    }
}
