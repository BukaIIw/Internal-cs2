#include "view.h"
#include "../core/convar.h"
#include "../core/cstypes.h"
#include "../features/combat/combat.h"
#include <algorithm>
#include <cmath>

namespace
{
    constexpr float default_fire_when = 0.98f;
    constexpr float max_fire_when = 0.999f;
    constexpr float release_delay = cstypes::tick_interval;
    constexpr float when_epsilon = 1e-5f;
    constexpr std::uint64_t attack = cstypes::command_buttons::in_attack;

    bool g_tick_open = false;
    bool g_user_attack = false;
    bool g_user_press = false;
    bool g_frame_aimed = false;

    bool revolver()
    {
        return features::combat::g_shared.ctx().def == cstypes::weapon_id::revolver;
    }

    bool semi_auto_locked(bool last_fired)
    {
        return last_fired && !features::combat::g_shared.ctx().full_auto && !revolver();
    }

    bool event_angles_follow_aim()
    {
        const auto* cvar = CONVAR("sv_subtick_movement_view_angles");
        return cvar->value != 0 && !cvar->get<bool>();
    }

    systems::input::subtick_event* find_press(systems::input::frame& frame, float when)
    {
        const int count = frame.step_count();
        for (int i = 0; i < count; ++i)
        {
            systems::input::subtick_event* event = frame.step(i);
            if (event && (event->button & attack) && event->pressed && std::fabs(event->when - when) < when_epsilon)
                return event;
        }
        return nullptr;
    }

    bool has_press(systems::input::frame& frame)
    {
        const int count = frame.step_count();
        for (int i = 0; i < count; ++i)
        {
            const systems::input::subtick_event* event = frame.step(i);
            if (event && (event->button & attack) && event->pressed)
                return true;
        }
        return false;
    }

    void write_event_angles(systems::input::frame& frame, systems::input::subtick_event& event, const math::qangle& angle)
    {
        const math::qangle target = math::helpers::sanitized(angle);
        const float yaw = frame.data->yaw + math::helpers::normalized_angle(target.y - frame.data->yaw);
        if (!std::isfinite(target.x) || !std::isfinite(yaw))
            return;
        event.pitch = target.x;
        event.yaw = yaw;
    }

    math::qangle lerp_angle(const math::qangle& from, const math::qangle& to, float t)
    {
        const math::qangle a = math::helpers::sanitized(from);
        const math::qangle b = math::helpers::sanitized(to);
        const float yaw_delta = math::helpers::normalized_angle(b.y - a.y);
        return math::helpers::sanitized({ a.x + (b.x - a.x) * t, a.y + yaw_delta * t, 0.f });
    }
}

namespace systems
{
    void view::begin(input::frame& frame)
    {
        if (g_tick_open)
            m_last_fired = m_fire && m_applied_attack;
        m_request = {};
        m_fire = false;
        m_hold = false;
        m_cock = false;
        m_render_tick = 0;
        m_block_fire = false;
        m_applied_attack = false;
        m_fire_when = 0.f;
        g_frame_aimed = false;
        g_user_attack = frame.valid() && frame.really_held(attack);
        g_user_press = g_user_attack && frame.held(attack) && (has_press(frame) || features::combat::g_shared.ctx().full_auto);
        math::qangle original = frame.valid() ? frame.view() : g_input.get_view_angles();
        m_original = original.is_valid() ? math::helpers::sanitized(original) : math::qangle{};
        g_tick_open = true;
    }

    bool view::aim(const math::qangle& angle, bool silent, int priority, const char* owner)
    {
        if (!angle.is_valid())
            return false;
        if (m_request.active && priority <= m_request.priority)
            return false;
        m_request.angle = math::helpers::sanitized(angle);
        m_request.angle.z = math::helpers::normalized_angle(angle.z);
        m_request.active = true;
        m_request.silent = silent;
        m_request.priority = priority;
        m_request.owner = owner;
        return true;
    }

    bool view::fire(float when)
    {
        if (m_block_fire || semi_auto_locked(m_last_fired))
            return false;
        m_fire = true;
        m_hold = revolver();
        m_fire_when = when < 0.f || !std::isfinite(when) ? default_fire_when : std::clamp(when, 0.f, max_fire_when);
        return true;
    }

    void view::apply(input::frame& frame)
    {
        if (!frame.valid())
            return;
        if (m_block_fire || semi_auto_locked(m_last_fired))
            m_fire = false;

        if (m_request.active && !m_request.silent)
        {
            frame.set_view(m_request.angle);
            g_input.set_view_angles(m_request.angle);
            g_frame_aimed = true;
        }

        if (!m_fire)
        {
            if (m_cock && !m_block_fire && !frame.held(attack))
                frame.press(attack, 0.f);
            return;
        }

        if (g_user_press && !m_hold)
        {
            if (m_request.active && (!m_request.silent || event_angles_follow_aim()))
            {
                const int steps = frame.step_count();
                for (int i = 0; i < steps; ++i)
                {
                    input::subtick_event* event = frame.step(i);
                    if (event && (event->button & attack) && event->pressed)
                        write_event_angles(frame, *event, m_request.angle);
                }
            }
            m_applied_attack = true;
            return;
        }

        if (m_hold)
        {
            if (!frame.held(attack))
                frame.press(attack, 0.f);
            m_applied_attack = true;
            return;
        }

        const float release_when = std::min(m_fire_when + release_delay, max_fire_when);
        frame.remove_steps(attack);
        frame.press(attack, m_fire_when);
        frame.release(attack, release_when);

        if (m_request.active)
        {
            if (input::subtick_event* press = find_press(frame, m_fire_when))
            {
                if (!m_request.silent || event_angles_follow_aim())
                    write_event_angles(frame, *press, m_request.angle);
            }
        }
        m_applied_attack = true;
    }

    void view::apply(input::usercmd& cmd)
    {
        if (!cmd)
            return;
        if (m_block_fire || semi_auto_locked(m_last_fired))
            m_fire = false;

        const int count = cmd.history_size();
        const bool late_aim = m_request.active && !m_request.silent && !g_frame_aimed;
        if (m_request.active && (m_request.silent || late_aim))
        {
            for (int i = 0; i < count; ++i)
            {
                math::qangle angle = m_request.angle;
                math::qangle from{};
                if (m_request.silent && smooth_silent && m_request.angle.z == 0.f && count > 1 && cmd.history_angles(i, from) && from.is_valid())
                    angle = lerp_angle(from, m_request.angle, static_cast<float>(i + 1) / static_cast<float>(count));
                cmd.set_history_angles(i, angle);
            }
            if (late_aim)
            {
                g_input.set_view_angles(m_request.angle);
                g_frame_aimed = true;
            }
        }

        if (m_fire && g_user_press && !m_hold)
        {
            if (m_render_tick > 0)
            {
                for (int i = 0; i < count; ++i)
                    cmd.set_history_render_tick(i, m_render_tick);
            }
            m_applied_attack = true;
        }
        else if (m_fire)
        {
            cmd.buttons() |= attack;
            cmd.buttons_changed() |= attack;
            cmd.set_base_buttons(attack, 0);
            if (count > 0 && !m_hold)
                cmd.set_attack1_index(count - 1);
            if (m_render_tick > 0)
            {
                for (int i = 0; i < count; ++i)
                    cmd.set_history_render_tick(i, m_render_tick);
            }
            m_applied_attack = true;
        }
        else if (m_cock && !m_block_fire)
        {
            cmd.buttons() |= attack;
            cmd.buttons_changed() |= attack;
            cmd.set_base_buttons(attack, 0);
        }
        else if (m_last_fired && !g_user_attack)
        {
            cmd.buttons() &= ~attack;
            cmd.buttons_changed() &= ~attack;
        }
    }

    bool view::user_attack() const
    {
        return g_user_press;
    }

    void view::end()
    {
        m_last_fired = m_fire && m_applied_attack;
        m_request = {};
        m_fire = false;
        m_hold = false;
        m_cock = false;
        m_render_tick = 0;
        m_block_fire = false;
        m_applied_attack = false;
        m_fire_when = 0.f;
        g_frame_aimed = false;
        g_tick_open = false;
    }

    void view::correct_movement(float& forward, float& left, float from_yaw, float to_yaw)
    {
        if (!std::isfinite(forward) || !std::isfinite(left) || !std::isfinite(from_yaw) || !std::isfinite(to_yaw))
            return;
        const float delta = math::deg2rad(math::helpers::normalized_angle(from_yaw - to_yaw));
        const float cos_delta = std::cos(delta);
        const float sin_delta = std::sin(delta);
        const float f = forward;
        const float l = left;
        forward = f * cos_delta - l * sin_delta;
        left = f * sin_delta + l * cos_delta;
    }
}
