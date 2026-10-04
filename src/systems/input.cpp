#include "input.h"
#include "game_reads.h"
#include "../core/addresses.h"
#include "../core/patterns.h"
#include "../core/memory.h"
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
    constexpr std::uintptr_t cmd_sequence = 0x8;
    constexpr std::uintptr_t cmd_has_bits = 0x20;
    constexpr std::uintptr_t cmd_history_size = 0x30;
    constexpr std::uintptr_t cmd_history_rep = 0x38;
    constexpr std::uintptr_t cmd_base = 0x40;
    constexpr std::uintptr_t cmd_attack1 = 0x4C;
    constexpr std::uintptr_t cmd_buttons_value = 0x60;
    constexpr std::uintptr_t cmd_buttons_changed = 0x68;
    constexpr std::uintptr_t cmd_buttons_scroll = 0x70;
    constexpr std::size_t cmd_size = 0x78;
    constexpr std::uint32_t cmd_has_attack1 = 0x40;

    constexpr std::uintptr_t rep_allocated = 0x0;
    constexpr std::uintptr_t rep_items = 0x8;
    constexpr int max_history = 256;

    constexpr std::uintptr_t pb_has_bits = 0x10;

    constexpr std::uintptr_t base_buttons = 0x38;
    constexpr std::uintptr_t base_view_angles = 0x40;
    constexpr std::uintptr_t base_forward = 0x58;
    constexpr std::uintptr_t base_left = 0x5C;
    constexpr std::size_t base_size = 0x60;
    constexpr std::uint32_t base_has_buttons = 1u << 1;

    constexpr std::uintptr_t buttons_state1 = 0x18;
    constexpr std::uintptr_t buttons_state2 = 0x20;
    constexpr std::size_t buttons_size = 0x30;
    constexpr std::uint32_t buttons_has_all = 7;

    constexpr std::uintptr_t angle_x = 0x18;
    constexpr std::uintptr_t angle_y = 0x1C;
    constexpr std::uintptr_t angle_z = 0x20;
    constexpr std::size_t angle_size = 0x24;
    constexpr std::uint32_t angle_has_all = 7;

    constexpr std::uintptr_t history_view_angles = 0x18;
    constexpr std::uintptr_t history_cl_interp = 0x20;
    constexpr std::uintptr_t history_sv_interp0 = 0x28;
    constexpr std::uintptr_t history_sv_interp1 = 0x30;
    constexpr std::uintptr_t history_render_tick_count = 0x60;
    constexpr std::uintptr_t history_render_tick_fraction = 0x64;
    constexpr std::uint32_t history_has_cl_interp = 0x2;
    constexpr std::uint32_t history_has_sv_interp0 = 0x4;
    constexpr std::uint32_t history_has_sv_interp1 = 0x8;
    constexpr std::uint32_t history_has_render_tick = 0x200 | 0x400;

    constexpr std::uintptr_t interp_frac = 0x18;
    constexpr std::uintptr_t interp_src_tick = 0x1C;
    constexpr std::uintptr_t interp_dst_tick = 0x20;
    constexpr std::size_t interp_size = 0x24;
    constexpr std::size_t interp_cl_size = 0x1C;
    constexpr std::uint32_t interp_has_all = 7;
    constexpr std::uint32_t interp_has_frac = 1;
    constexpr std::uintptr_t history_player_tick_count = 0x68;
    constexpr std::uintptr_t history_player_tick_fraction = 0x6C;
    constexpr std::size_t history_entry_size = 0x70;
    constexpr std::uint32_t history_has_view_angles = 1;

    constexpr std::uintptr_t controller_sequence = 0x5910;

    using systems::input::max_steps;
    using systems::input::subtick_event;
    using systems::input::subtick_input;

    std::uint64_t& scratch()
    {
        thread_local std::uint64_t value = 0;
        value = 0;
        return value;
    }

    bool valid_cmd(std::uintptr_t cmd)
    {
        return systems::reads::readable(cmd, cmd_size);
    }

    bool read_angles(std::uintptr_t angles, math::qangle& out)
    {
        if (!systems::reads::readable(angles, angle_size))
            return false;
        const math::qangle value{ memory::read<float>(angles + angle_x), memory::read<float>(angles + angle_y), memory::read<float>(angles + angle_z) };
        if (!value.is_valid())
            return false;
        out = value;
        return true;
    }

    bool write_angles(std::uintptr_t angles, const math::qangle& value)
    {
        if (!systems::reads::readable(angles, angle_size) || !value.is_valid())
            return false;
        math::qangle clean = math::helpers::sanitized(value);
        clean.z = math::helpers::normalized_angle(value.z);
        memory::write<float>(angles + angle_x, clean.x);
        memory::write<float>(angles + angle_y, clean.y);
        memory::write<float>(angles + angle_z, clean.z);
        memory::ref<std::uint32_t>(angles + pb_has_bits) |= angle_has_all;
        return true;
    }

    float clamp_pitch(float pitch)
    {
        return std::clamp(pitch, -89.f, 89.f);
    }

    std::uintptr_t view_angles_function()
    {
        if (const std::uintptr_t full = PATTERN(patterns::set_view_angles_full))
            return full;
        return PATTERN(patterns::set_view_angles);
    }

    bool call_set_view_angles(std::uintptr_t function, std::uintptr_t input, float* angles)
    {
        __try
        {
            memory::call<void>(function, input, 0, angles);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::uintptr_t query_cmd(std::uintptr_t base_function, std::uintptr_t cmd_function, std::uintptr_t controller)
    {
        __try
        {
            const auto base = memory::call<std::uintptr_t>(base_function, controller);
            if (!base || !systems::reads::readable(base + controller_sequence, sizeof(int)))
                return 0;
            const int sequence = memory::read<int>(base + controller_sequence);
            if (sequence <= 0)
                return 0;
            const auto cmd = memory::call<std::uintptr_t>(cmd_function, controller, sequence);
            if (!valid_cmd(cmd) || !systems::reads::pointer(cmd + cmd_base))
                return 0;
            return cmd;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }
}

namespace systems
{
    namespace input
    {
        void frame::set_view(const math::qangle& angles)
        {
            if (!data || !angles.is_valid())
                return;
            const math::qangle target = math::helpers::sanitized(angles);
            const float yaw = data->yaw + math::helpers::normalized_angle(target.y - data->yaw);
            if (!std::isfinite(yaw))
                return;
            data->pitch = target.x;
            data->yaw = yaw;
            const int count = step_count();
            for (int i = 0; i < count; ++i)
            {
                data->events[i].pitch = target.x;
                data->events[i].yaw = yaw;
            }
        }

        void frame::rotate_view(float pitch_delta, float yaw_delta)
        {
            if (!data || !std::isfinite(pitch_delta) || !std::isfinite(yaw_delta))
                return;
            data->pitch = clamp_pitch(data->pitch + pitch_delta);
            data->yaw += yaw_delta;
            const int count = step_count();
            for (int i = 0; i < count; ++i)
            {
                data->events[i].pitch = clamp_pitch(data->events[i].pitch + pitch_delta);
                data->events[i].yaw += yaw_delta;
            }
        }

        int frame::step_count() const
        {
            if (!data)
                return 0;
            return std::clamp(data->count, 0, max_steps);
        }

        subtick_event* frame::step(int index)
        {
            if (!data || index < 0 || index >= step_count())
                return nullptr;
            return &data->events[index];
        }

        subtick_event* frame::add_step(float when)
        {
            if (!data || !std::isfinite(when))
                return nullptr;
            const int count = data->count;
            if (count < 0 || count >= max_steps)
                return nullptr;
            when = std::clamp(when, 0.f, 1.f);
            int at = 0;
            while (at < count && data->events[at].when <= when)
                ++at;
            const subtick_event* neighbour = at > 0 ? &data->events[at - 1] : at < count ? &data->events[at] : nullptr;
            const float pitch = neighbour ? neighbour->pitch : data->pitch;
            const float yaw = neighbour ? neighbour->yaw : data->yaw;
            std::memmove(&data->events[at + 1], &data->events[at], static_cast<std::size_t>(count - at) * sizeof(subtick_event));
            data->count = count + 1;
            subtick_event* event = &data->events[at];
            std::memset(event, 0, sizeof(subtick_event));
            event->when = when;
            event->pitch = pitch;
            event->yaw = yaw;
            return event;
        }

        void frame::remove_steps(std::uint64_t button)
        {
            if (!data || !button)
                return;
            const int count = step_count();
            int kept = 0;
            for (int i = 0; i < count; ++i)
            {
                if (data->events[i].button & button)
                    continue;
                if (kept != i)
                    data->events[kept] = data->events[i];
                ++kept;
            }
            data->count = kept;
        }

        void frame::remove_analog_steps()
        {
            if (!data)
                return;
            const int count = step_count();
            int kept = 0;
            for (int i = 0; i < count; ++i)
            {
                if (data->events[i].button == 0)
                    continue;
                if (kept != i)
                    data->events[kept] = data->events[i];
                ++kept;
            }
            data->count = kept;
        }

        void frame::press(std::uint64_t button, float when)
        {
            if (!data || !button)
                return;
            if (subtick_event* event = add_step(when))
            {
                event->button = button;
                event->pressed = true;
            }
            data->pressed |= button;
            data->down |= button;
        }

        void frame::release(std::uint64_t button, float when)
        {
            if (!data || !button)
                return;
            if (subtick_event* event = add_step(when))
            {
                event->button = button;
                event->pressed = false;
            }
            data->released |= button;
            data->down &= ~button;
        }

        math::qangle frame::angles_at(float when) const
        {
            if (!data)
                return {};
            math::qangle result{ data->pitch, data->yaw, 0.f };
            const int count = step_count();
            for (int i = 0; i < count; ++i)
            {
                const subtick_event& event = data->events[i];
                if (event.when > when)
                    break;
                result = { event.pitch, event.yaw, 0.f };
            }
            return result;
        }

        int usercmd::sequence() const
        {
            return valid_cmd(ptr) ? memory::read<int>(ptr + cmd_sequence) : 0;
        }

        std::uint64_t& usercmd::buttons()
        {
            return valid_cmd(ptr) ? memory::ref<std::uint64_t>(ptr + cmd_buttons_value) : scratch();
        }

        std::uint64_t& usercmd::buttons_changed()
        {
            return valid_cmd(ptr) ? memory::ref<std::uint64_t>(ptr + cmd_buttons_changed) : scratch();
        }

        std::uint64_t& usercmd::buttons_scroll()
        {
            return valid_cmd(ptr) ? memory::ref<std::uint64_t>(ptr + cmd_buttons_scroll) : scratch();
        }

        std::uintptr_t usercmd::base() const
        {
            if (!valid_cmd(ptr))
                return 0;
            const std::uintptr_t value = reads::pointer(ptr + cmd_base);
            return reads::readable(value, base_size) ? value : 0;
        }

        bool usercmd::base_angles(math::qangle& out) const
        {
            const std::uintptr_t b = base();
            return b && read_angles(reads::pointer(b + base_view_angles), out);
        }

        void usercmd::set_base_angles(const math::qangle& angles)
        {
            if (const std::uintptr_t b = base())
                write_angles(reads::pointer(b + base_view_angles), angles);
        }

        float* usercmd::forwardmove()
        {
            const std::uintptr_t b = base();
            return b ? reinterpret_cast<float*>(b + base_forward) : nullptr;
        }

        float* usercmd::leftmove()
        {
            const std::uintptr_t b = base();
            return b ? reinterpret_cast<float*>(b + base_left) : nullptr;
        }

        void usercmd::set_base_buttons(std::uint64_t set, std::uint64_t clear)
        {
            const std::uintptr_t b = base();
            if (!b)
                return;
            const std::uintptr_t states = reads::pointer(b + base_buttons);
            if (!reads::readable(states, buttons_size))
                return;
            auto& held = memory::ref<std::uint64_t>(states + buttons_state1);
            auto& changed = memory::ref<std::uint64_t>(states + buttons_state2);
            held = (held | set) & ~clear;
            changed = (changed | set) & ~clear;
            memory::ref<std::uint32_t>(states + pb_has_bits) |= buttons_has_all;
            memory::ref<std::uint32_t>(b + pb_has_bits) |= base_has_buttons;
        }

        int usercmd::history_size() const
        {
            if (!valid_cmd(ptr))
                return 0;
            const int size = memory::read<int>(ptr + cmd_history_size);
            const std::uintptr_t rep = reads::pointer(ptr + cmd_history_rep);
            if (size <= 0 || !reads::readable(rep, rep_items))
                return 0;
            const int allocated = memory::read<int>(rep + rep_allocated);
            const int count = std::min({ size, allocated, max_history });
            if (count <= 0 || !reads::readable(rep + rep_items, static_cast<std::size_t>(count) * sizeof(std::uintptr_t)))
                return 0;
            return count;
        }

        std::uintptr_t usercmd::history(int index) const
        {
            if (index < 0 || index >= history_size())
                return 0;
            const std::uintptr_t rep = reads::pointer(ptr + cmd_history_rep);
            if (!rep)
                return 0;
            const std::uintptr_t entry = reads::pointer(rep + rep_items + static_cast<std::uintptr_t>(index) * sizeof(std::uintptr_t));
            return reads::readable(entry, history_entry_size) ? entry : 0;
        }

        bool usercmd::history_angles(int index, math::qangle& out) const
        {
            const std::uintptr_t entry = history(index);
            return entry && read_angles(reads::pointer(entry + history_view_angles), out);
        }

        void usercmd::set_history_angles(int index, const math::qangle& angles)
        {
            const std::uintptr_t entry = history(index);
            if (!entry)
                return;
            if (write_angles(reads::pointer(entry + history_view_angles), angles))
                memory::ref<std::uint32_t>(entry + pb_has_bits) |= history_has_view_angles;
        }

        int usercmd::history_render_tick(int index) const
        {
            const std::uintptr_t entry = history(index);
            if (!entry)
                return -1;
            const int tick = memory::read<int>(entry + history_render_tick_count);
            return tick >= 0 ? tick : -1;
        }

        void usercmd::set_history_render_tick(int index, int tick)
        {
            const std::uintptr_t entry = history(index);
            if (!entry || tick <= 0)
                return;
            std::uint32_t& bits = memory::ref<std::uint32_t>(entry + pb_has_bits);
            memory::write<int>(entry + history_render_tick_count, tick);
            memory::write<float>(entry + history_render_tick_fraction, 0.f);
            bits |= history_has_render_tick;
            const auto clear_server = [&](std::uintptr_t slot, std::uint32_t has)
            {
                if (!(bits & has))
                    return;
                const std::uintptr_t info = reads::pointer(entry + slot);
                if (!reads::readable(info, interp_size))
                    return;
                memory::write<float>(info + interp_frac, 0.f);
                memory::write<int>(info + interp_src_tick, -1);
                memory::write<int>(info + interp_dst_tick, -1);
                memory::ref<std::uint32_t>(info + pb_has_bits) |= interp_has_all;
            };
            clear_server(history_sv_interp0, history_has_sv_interp0);
            clear_server(history_sv_interp1, history_has_sv_interp1);
            if (bits & history_has_cl_interp)
            {
                const std::uintptr_t info = reads::pointer(entry + history_cl_interp);
                if (reads::readable(info, interp_cl_size))
                {
                    memory::write<float>(info + interp_frac, 0.f);
                    memory::ref<std::uint32_t>(info + pb_has_bits) |= interp_has_frac;
                }
            }
        }

        int usercmd::history_player_tick(int index) const
        {
            const std::uintptr_t entry = history(index);
            if (!entry)
                return -1;
            const int tick = memory::read<int>(entry + history_player_tick_count);
            return tick >= 0 ? tick : -1;
        }

        float usercmd::history_player_fraction(int index) const
        {
            const std::uintptr_t entry = history(index);
            if (!entry)
                return 0.f;
            const float fraction = memory::read<float>(entry + history_player_tick_fraction);
            return std::isfinite(fraction) && fraction >= 0.f && fraction <= 1.f ? fraction : 0.f;
        }

        int usercmd::attack1_index() const
        {
            return valid_cmd(ptr) ? memory::read<int>(ptr + cmd_attack1) : -1;
        }

        void usercmd::set_attack1_index(int index)
        {
            if (!valid_cmd(ptr) || index < -1)
                return;
            memory::write<int>(ptr + cmd_attack1, index);
            if (index >= 0)
                memory::ref<std::uint32_t>(ptr + cmd_has_bits) |= cmd_has_attack1;
        }
    }

    bool input_manager::initialize()
    {
        return csgo_input() != 0;
    }

    std::uintptr_t input_manager::csgo_input() const
    {
        return addresses::globals::csgo_input();
    }

    math::qangle input_manager::get_view_angles() const
    {
        if (m_frame.valid())
            return m_frame.view();
        const std::uintptr_t object = csgo_input();
        if (!object)
            return {};
        const std::uintptr_t angles = object + input::slot_offset + offsetof(input::subtick_input, pitch);
        if (!reads::readable(angles, sizeof(float) * 2))
            return {};
        const math::qangle value{ memory::read<float>(angles), memory::read<float>(angles + sizeof(float)), 0.f };
        return value.is_valid() ? value : math::qangle{};
    }

    void input_manager::set_view_angles(const math::qangle& angles) const
    {
        if (!angles.is_valid())
            return;
        const std::uintptr_t function = view_angles_function();
        const std::uintptr_t object = csgo_input();
        if (!function || !object)
            return;
        const math::qangle clean = math::helpers::sanitized(angles);
        float values[3]{ clean.x, clean.y, 0.f };
        call_set_view_angles(function, object, values);
    }

    void input_manager::begin_frame(std::uintptr_t csgo_input, input::subtick_input* data, const math::vector2& last_impulses, std::uint64_t real_buttons)
    {
        m_frame = {};
        if (!csgo_input || !data || !reads::readable(reinterpret_cast<std::uintptr_t>(data), sizeof(input::subtick_input)))
            return;
        m_frame.csgo_input = csgo_input;
        m_frame.data = data;
        m_frame.last_impulses = last_impulses;
        m_frame.real_buttons = real_buttons;
    }

    void input_manager::end_frame()
    {
        m_frame = {};
    }

    input::usercmd input_manager::find_cmd(std::uintptr_t controller) const
    {
        input::usercmd result{};
        const std::uintptr_t base_function = PATTERN(patterns::get_usercmd_base);
        const std::uintptr_t cmd_function = PATTERN(patterns::get_usercmd);
        if (!controller || !base_function || !cmd_function)
            return result;
        result.ptr = query_cmd(base_function, cmd_function, controller);
        return result;
    }
}
