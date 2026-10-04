#pragma once
#include "input.h"
#include "../core/math.h"

namespace systems
{
    class view
    {
    public:
        struct request
        {
            math::qangle angle{};
            bool active = false;
            bool silent = false;
            int priority = -1;
            const char* owner = nullptr;
        };

        void begin(input::frame& frame);
        const math::qangle& original() const { return m_original; }

        bool aim(const math::qangle& angle, bool silent, int priority, const char* owner);
        bool fire(float when = -1.f);
        void block_fire() { m_block_fire = true; }
        void hold_attack() { m_cock = true; }
        void set_render_tick(int tick) { m_render_tick = tick; }

        bool aiming() const { return m_request.active; }
        bool firing() const { return m_fire; }
        const request& current() const { return m_request; }
        float fire_when() const { return m_fire_when; }
        bool last_tick_fired() const { return m_last_fired; }
        bool user_attack() const;

        void apply(input::frame& frame);
        void apply(input::usercmd& cmd);
        void end();

        static void correct_movement(float& forward, float& left, float from_yaw, float to_yaw);

        bool smooth_silent = false;

    private:
        math::qangle m_original{};
        request m_request{};
        bool m_fire = false;
        bool m_hold = false;
        bool m_cock = false;
        int m_render_tick = 0;
        bool m_block_fire = false;
        float m_fire_when = 0.f;
        bool m_last_fired = false;
        bool m_applied_attack = false;
    };

    inline view g_view{};
}
