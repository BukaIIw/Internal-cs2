#pragma once
#include "../../core/math.h"
#include "../../systems/input.h"
#include <cstdint>

namespace features::movement
{
    class jumpbug
    {
    public:
        void on_create_move(systems::input::frame& frame);
        bool active_this_tick() const { return m_active_this_tick; }
        float landing_fraction() const { return m_landing_fraction; }

    private:
        bool m_active_this_tick = false;
        bool m_ducking = false;
        bool m_release_jump = false;
        float m_landing_fraction = 0.f;
    };

    class bhop
    {
    public:
        void on_create_move(systems::input::frame& frame);

    private:
        bool m_jump_sent = false;
        bool m_air_pressed = false;
        int m_last_jump_tick = 0;
        int m_streak = 0;
        float m_bias = 0.f;
    };

    class airstrafe
    {
    public:
        void on_create_move(systems::input::frame& frame);
        void store_angles(const math::qangle& angles) { m_angles = angles; }
        bool handled_this_tick() const { return m_handled_this_tick; }

    private:
        void check_button(std::uint64_t current_buttons, std::uint64_t button);
        math::qangle m_angles{};
        std::uint64_t m_last_buttons = 0;
        std::uint64_t m_last_pressed = 0;
        bool m_side_switch = false;
        bool m_handled_this_tick = false;
    };

    class test_strafer
    {
    public:
        void on_create_move(systems::input::frame& frame);
        bool is_active() const;
        bool handled_this_tick() const { return m_handled_this_tick; }

    private:
        void check_button(std::uint64_t current_buttons, std::uint64_t button);
        std::uint64_t m_last_buttons = 0;
        std::uint64_t m_last_pressed = 0;
        int m_substep_counter = 0;
        bool m_handled_this_tick = false;
    };

    class fastladder
    {
    public:
        void on_create_move(systems::input::frame& frame) const;
    };

    class quickstop
    {
    public:
        void on_create_move(systems::input::frame& frame) const;
    };

    inline jumpbug g_jumpbug{};
    inline bhop g_bhop{};
    inline airstrafe g_airstrafe{};
    inline test_strafer g_test_strafer{};
    inline fastladder g_fastladder{};
    inline quickstop g_quickstop{};
}
