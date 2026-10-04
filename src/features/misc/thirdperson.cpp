#include "misc.h"
#include "../../core/addresses.h"
#include "../../core/convar.h"
#include "../../core/hooks.h"
#include "../../core/keys.h"
#include "../../core/memory.h"
#include "../../core/settings.h"
#include "../../systems/game_reads.h"
#include "../../systems/local.h"
#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <cmath>

namespace
{
    constexpr std::uintptr_t input_thirdperson = 0x229;
    constexpr std::uintptr_t input_camera_pitch = 0x230;
    constexpr std::uintptr_t input_camera_yaw = 0x234;
    constexpr std::uintptr_t input_camera_extra = 0x238;
    constexpr std::uintptr_t input_view_pitch = 0x688;
    constexpr std::uintptr_t input_view_yaw = 0x68C;
    constexpr std::uintptr_t input_camera_state = 0x6A8;
    constexpr std::size_t input_span = 0x6B0;
    constexpr std::size_t pawn_thirdperson_index = 315;
    constexpr float camera_extra_value = 30.f;
    constexpr float min_distance = 30.f;
    constexpr float max_distance = 500.f;
    constexpr std::uint64_t camera_timeout_ms = 500;

    std::atomic<std::uint64_t> g_camera_tick{ 0 };

    std::uintptr_t input_object()
    {
        const std::uintptr_t input = addresses::globals::csgo_input();
        return systems::reads::readable(input, input_span) ? input : 0;
    }

    bool camera_hook_alive()
    {
        const std::uint64_t last = g_camera_tick.load(std::memory_order_relaxed);
        return last && GetTickCount64() - last < camera_timeout_ms;
    }

    std::uintptr_t convar_value(const convars::convar* var, std::size_t size)
    {
        if (!var || !systems::reads::readable(var->value, size))
            return 0;
        return var->value;
    }

    float wanted_distance()
    {
        const float d = settings::g_misc.thirdperson_distance;
        return std::isfinite(d) ? std::clamp(d, min_distance, max_distance) : 120.f;
    }

    void write_distance()
    {
        if (const std::uintptr_t value = convar_value(CONVAR("cam_idealdist"), sizeof(float)))
            memory::write<float>(value, wanted_distance());
    }

    void set_pawn_thirdperson(std::uintptr_t pawn, bool enabled)
    {
        if (!systems::reads::readable(pawn))
            return;
        const std::uintptr_t vtable = memory::read<std::uintptr_t>(pawn);
        if (!systems::reads::readable(vtable + pawn_thirdperson_index * sizeof(std::uintptr_t)))
            return;
        const std::uintptr_t function = memory::read<std::uintptr_t>(vtable + pawn_thirdperson_index * sizeof(std::uintptr_t));
        const auto& client = memory::get_module("client.dll");
        if (!client || !client.text.contains(function))
            return;
        memory::call_vfunc<void>(pawn, pawn_thirdperson_index, enabled);
    }
}

namespace features::misc
{
    void thirdperson::on_create_move()
    {
        const std::uintptr_t input = input_object();
        if (!input)
            return;

        const auto local = systems::g_local.get();
        const bool want = !hooks::unloading.load() && local.pawn && local.is_alive && keys::active(settings::g_misc.thirdperson, settings::g_misc.thirdperson_key);

        if (want == m_applied)
        {
            if (want && local.pawn != m_pawn)
            {
                m_pawn = local.pawn;
                set_pawn_thirdperson(local.pawn, true);
            }
            return;
        }

        const std::uintptr_t distance = convar_value(CONVAR("cam_idealdist"), sizeof(float));
        if (want)
        {
            memory::write<float>(input + input_camera_pitch, memory::read<float>(input + input_view_pitch));
            memory::write<float>(input + input_camera_yaw, memory::read<float>(input + input_view_yaw));
            memory::write<float>(input + input_camera_extra, camera_extra_value);
            if (distance)
            {
                const float current = memory::read<float>(distance);
                m_distance = std::isfinite(current) && current > 0.f ? current : 0.f;
            }
        }
        else if (distance && m_distance > 0.f)
        {
            memory::write<float>(distance, m_distance);
            m_distance = 0.f;
        }

        m_applied = want;
        m_pawn = want ? local.pawn : 0;
        memory::write<bool>(input + input_thirdperson, want);
        memory::write<std::uint32_t>(input + input_camera_state, 0u);
        if (local.pawn)
            set_pawn_thirdperson(local.pawn, want);
    }

    void thirdperson::on_override_view(std::uintptr_t client_mode, std::uintptr_t view_setup)
    {
        if (!client_mode || !view_setup || !m_applied || hooks::unloading.load() || camera_hook_alive())
            return;
        const std::uintptr_t input = input_object();
        if (!input)
            return;
        memory::write<bool>(input + input_thirdperson, true);
        write_distance();
    }

    bool thirdperson::on_camera_think(std::uintptr_t input, int slot)
    {
        g_camera_tick.store(GetTickCount64(), std::memory_order_relaxed);
        if (slot != 0 || !m_applied || hooks::unloading.load())
            return false;
        if (!systems::reads::readable(input, input_span))
            return false;
        const std::uintptr_t cheats = convar_value(CONVAR("sv_cheats"), sizeof(std::uint8_t));
        if (!cheats)
            return false;

        memory::write<bool>(input + input_thirdperson, true);
        write_distance();
        m_cheats = memory::read<std::uint8_t>(cheats);
        m_cheats_saved = true;
        memory::write<std::uint8_t>(cheats, 1);
        return true;
    }

    void thirdperson::after_camera_think(std::uintptr_t input, int slot)
    {
        if (m_cheats_saved)
        {
            if (const std::uintptr_t cheats = convar_value(CONVAR("sv_cheats"), sizeof(std::uint8_t)))
                memory::write<std::uint8_t>(cheats, m_cheats);
            m_cheats_saved = false;
        }
        if (slot == 0 && m_applied && systems::reads::readable(input, input_span))
            memory::write<bool>(input + input_thirdperson, true);
    }

    void thirdperson::restore()
    {
        if (m_cheats_saved)
        {
            if (const std::uintptr_t cheats = convar_value(CONVAR("sv_cheats"), sizeof(std::uint8_t)))
                memory::write<std::uint8_t>(cheats, m_cheats);
            m_cheats_saved = false;
        }

        if (m_applied)
        {
            if (const std::uintptr_t input = input_object())
            {
                memory::write<bool>(input + input_thirdperson, false);
                memory::write<std::uint32_t>(input + input_camera_state, 0u);
            }
            if (const std::uintptr_t pawn = systems::g_local.get().pawn)
                set_pawn_thirdperson(pawn, false);
        }

        if (m_distance > 0.f)
            if (const std::uintptr_t distance = convar_value(CONVAR("cam_idealdist"), sizeof(float)))
                memory::write<float>(distance, m_distance);

        m_distance = 0.f;
        m_applied = false;
        m_pawn = 0;
    }
}
