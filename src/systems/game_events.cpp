#include "game_events.h"
#include "game_reads.h"
#include "../core/addresses.h"
#include "../core/log.h"
#include "../core/memory.h"
#include "../core/patterns.h"
#include <Windows.h>
#include <cstring>
#include <mutex>

namespace
{
    constexpr std::size_t add_listener_index = 3;
    constexpr std::size_t remove_listener_index = 5;
    constexpr std::size_t event_name_index = 1;
    constexpr int debug_id = 42;
    constexpr int ring_size = 64;
    constexpr const char* bullet_impact = "bullet_impact";

    struct listener
    {
        void* const* vtable;
    };

    void __fastcall listener_destroy(void*, int) {}
    int __fastcall listener_debug_id(void*) { return debug_id; }
    void __fastcall listener_fire(void* self, std::uintptr_t event);

    void* const g_table[] = {
        reinterpret_cast<void*>(&listener_destroy),
        reinterpret_cast<void*>(&listener_fire),
        reinterpret_cast<void*>(&listener_debug_id),
        reinterpret_cast<void*>(&listener_debug_id),
    };

    listener g_listener{ g_table };
    std::uintptr_t g_manager = 0;
    bool g_registered = false;
    bool g_failed = false;
    std::mutex g_mutex;
    systems::game_events::impact g_ring[ring_size]{};
    int g_head = 0;

    std::uintptr_t manager()
    {
        const std::uintptr_t global = PATTERN(patterns::game_event_manager);
        return global ? systems::reads::pointer(global) : 0;
    }

    bool read_impact(std::uintptr_t event, math::vector3& out)
    {
        __try
        {
            const char* name = memory::call_vfunc<const char*>(event, event_name_index);
            if (!name || std::strcmp(name, bullet_impact) != 0)
                return false;
            const std::uintptr_t get_float = PATTERN(patterns::game_event_get_float);
            if (!get_float)
                return false;
            out.x = memory::call<float>(get_float, event, "x", 0.f);
            out.y = memory::call<float>(get_float, event, "y", 0.f);
            out.z = memory::call<float>(get_float, event, "z", 0.f);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void __fastcall listener_fire(void*, std::uintptr_t event)
    {
        if (!event)
            return;
        math::vector3 point{};
        if (!read_impact(event, point) || !point.is_valid())
            return;
        std::lock_guard lock(g_mutex);
        g_ring[g_head] = { GetTickCount64(), point };
        g_head = (g_head + 1) % ring_size;
    }

    bool call_add(std::uintptr_t mgr)
    {
        __try
        {
            memory::call_vfunc<bool>(mgr, add_listener_index, reinterpret_cast<std::uintptr_t>(&g_listener), bullet_impact, false);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void call_remove(std::uintptr_t mgr)
    {
        __try
        {
            memory::call_vfunc<void>(mgr, remove_listener_index, reinterpret_cast<std::uintptr_t>(&g_listener));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }
}

namespace systems::game_events
{
    void update()
    {
        if (g_failed)
            return;
        const std::uintptr_t mgr = manager();
        if (!mgr)
            return;
        if (g_registered && mgr == g_manager)
            return;
        if (g_registered)
            call_remove(g_manager);
        g_registered = call_add(mgr);
        g_manager = g_registered ? mgr : 0;
        if (!g_registered)
        {
            g_failed = true;
            logs::Add(logs::Warning, "Game events: listener registration failed");
        }
    }

    void shutdown()
    {
        if (g_registered && g_manager)
            call_remove(g_manager);
        g_registered = false;
        g_manager = 0;
    }

    bool active()
    {
        return g_registered;
    }

    int impacts_since(std::uint64_t since, impact* out, int max)
    {
        if (!out || max <= 0)
            return 0;
        std::lock_guard lock(g_mutex);
        int written = 0;
        for (int i = 1; i <= ring_size && written < max; ++i)
        {
            const impact& e = g_ring[(g_head + ring_size - i) % ring_size];
            if (e.time == 0 || e.time < since)
                break;
            out[written++] = e;
        }
        return written;
    }
}
