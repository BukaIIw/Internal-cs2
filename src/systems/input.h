#pragma once
#include "../core/math.h"
#include <cstddef>
#include <cstdint>

namespace systems
{
    namespace input
    {
        constexpr int max_steps = 32;

        struct subtick_event
        {
            float when;
            std::uint32_t pad;
            std::uint64_t button;
            union
            {
                bool pressed;
                struct
                {
                    float forward, left;
                } analog;
            };
            float pitch, yaw;
        };

        struct subtick_input
        {
            std::uint8_t pad0[0x28];
            std::uint64_t previous;
            std::uint64_t down;
            std::uint64_t pressed;
            std::uint64_t released;
            float forward, left, up;
            std::uint8_t pad1[0x8];
            int count;
            subtick_event events[max_steps];
            float pitch, yaw, roll;
        };

        static_assert(sizeof(subtick_event) == 0x20);
        static_assert(offsetof(subtick_input, down) == 0x30 && offsetof(subtick_input, forward) == 0x48 && offsetof(subtick_input, count) == 0x5C);
        static_assert(offsetof(subtick_input, events) == 0x60 && offsetof(subtick_input, pitch) == 0x460);

        constexpr std::uintptr_t slot_offset = 0x228;
        constexpr std::uintptr_t slot_stride = 0x928;

        class frame
        {
        public:
            std::uintptr_t csgo_input = 0;
            subtick_input* data = nullptr;
            math::vector2 last_impulses{};
            std::uint64_t real_buttons = 0;

            bool valid() const { return data != nullptr; }
            std::uint64_t& down() { return data->down; }
            std::uint64_t& pressed() { return data->pressed; }
            std::uint64_t& released() { return data->released; }
            bool held(std::uint64_t button) const { return ((data->down | data->pressed) & button) != 0; }
            bool really_held(std::uint64_t button) const { return (real_buttons & button) != 0; }
            float& forward() { return data->forward; }
            float& left() { return data->left; }

            math::qangle view() const { return { data->pitch, data->yaw, 0.f }; }
            void set_view(const math::qangle& angles);
            void rotate_view(float pitch_delta, float yaw_delta);

            int step_count() const;
            subtick_event* step(int index);
            subtick_event* add_step(float when);
            void remove_steps(std::uint64_t button);
            void remove_analog_steps();
            void press(std::uint64_t button, float when);
            void release(std::uint64_t button, float when);
            math::qangle angles_at(float when) const;
        };

        class usercmd
        {
        public:
            std::uintptr_t ptr = 0;

            explicit operator bool() const { return ptr != 0; }

            int sequence() const;
            std::uint64_t& buttons();
            std::uint64_t& buttons_changed();
            std::uint64_t& buttons_scroll();

            std::uintptr_t base() const;
            bool base_angles(math::qangle& out) const;
            void set_base_angles(const math::qangle& angles);
            float* forwardmove();
            float* leftmove();
            void set_base_buttons(std::uint64_t set, std::uint64_t clear);

            int history_size() const;
            std::uintptr_t history(int index) const;
            bool history_angles(int index, math::qangle& out) const;
            void set_history_angles(int index, const math::qangle& angles);
            int history_render_tick(int index) const;
            void set_history_render_tick(int index, int tick);
            int history_player_tick(int index) const;
            float history_player_fraction(int index) const;

            int attack1_index() const;
            void set_attack1_index(int index);
        };
    }

    class input_manager
    {
    public:
        bool initialize();
        std::uintptr_t csgo_input() const;

        math::qangle get_view_angles() const;
        void set_view_angles(const math::qangle& angles) const;

        input::frame* current_frame() { return m_frame.valid() ? &m_frame : nullptr; }
        input::usercmd current_cmd() const { return m_cmd; }

        void begin_frame(std::uintptr_t csgo_input, input::subtick_input* data, const math::vector2& last_impulses, std::uint64_t real_buttons);
        void end_frame();
        void set_cmd(std::uintptr_t cmd) { m_cmd.ptr = cmd; }
        void clear_cmd() { m_cmd.ptr = 0; }
        input::usercmd find_cmd(std::uintptr_t controller) const;

    private:
        input::frame m_frame{};
        input::usercmd m_cmd{};
    };

    inline input_manager g_input{};
}
