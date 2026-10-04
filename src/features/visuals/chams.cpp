#include "visuals.h"
#include "visuals_detail.h"
#include "../changer/changer.h"
#include "../../core/schema.h"
#include "../../core/settings.h"
#include "../../systems/entities.h"
#include "../../systems/game_reads.h"
#include "../../systems/local.h"
#include <Windows.h>
#include <atomic>
#include <cstdint>

namespace
{
    constexpr std::uint32_t invalid_handle = 0xFFFFFFFFu;
    constexpr std::uintptr_t mesh_stride = 0x70;
    constexpr std::uintptr_t mesh_object = 0x18;
    constexpr std::uintptr_t mesh_color = 0x50;
    constexpr std::uintptr_t identity_handle = 0x10;
    constexpr int owner_scan_size = 0x400;
    constexpr int calibration_hits = 8;
    constexpr int max_mesh_count = 4096;

    enum owner_kind : int
    {
        owner_none = 0,
        owner_handle = 1,
        owner_pointer = 2
    };

    struct target
    {
        std::atomic<std::uint32_t> handle{ invalid_handle };
        std::atomic<std::uintptr_t> entity{ 0 };
        std::atomic<std::uint32_t> alt_handle{ invalid_handle };
        std::atomic<std::uintptr_t> alt_entity{ 0 };
        std::atomic<std::uint32_t> tint{ 0 };
    };

    target g_arms{};
    target g_weapon{};
    std::atomic<int> g_owner_offset{ -1 };
    std::atomic<int> g_owner_kind{ owner_none };
    std::atomic<int> g_candidate_offset{ -1 };
    std::atomic<int> g_candidate_kind{ owner_none };
    std::atomic<int> g_candidate_hits{ 0 };

    void set_target(target& t, std::uint32_t handle, std::uintptr_t entity, std::uint32_t alt_handle, std::uintptr_t alt_entity, std::uint32_t tint)
    {
        t.handle.store(entity ? handle : invalid_handle, std::memory_order_relaxed);
        t.entity.store(entity, std::memory_order_relaxed);
        t.alt_handle.store(alt_entity ? alt_handle : invalid_handle, std::memory_order_relaxed);
        t.alt_entity.store(alt_entity, std::memory_order_relaxed);
        t.tint.store(tint, std::memory_order_relaxed);
    }

    void clear_target(target& t)
    {
        set_target(t, invalid_handle, 0, invalid_handle, 0, 0);
    }

    std::uint32_t entity_handle(std::uintptr_t entity)
    {
        const std::uintptr_t identity = systems::reads::field_pointer(entity, SCHEMA("CEntityInstance", "m_pEntity"_hash));
        if (!identity)
            return invalid_handle;
        const auto handle = systems::reads::value<std::uint32_t>(identity + identity_handle, invalid_handle);
        return systems::g_entities.lookup(handle) == entity ? handle : invalid_handle;
    }

    bool read_owner(std::uintptr_t address, int kind, std::uint64_t& out)
    {
        __try
        {
            out = kind == owner_handle ? *reinterpret_cast<const std::uint32_t*>(address) : *reinterpret_cast<const std::uint64_t*>(address);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool matches(std::uintptr_t object, int offset, int kind, const target& t)
    {
        std::uint64_t v = 0;
        if (!read_owner(object + static_cast<std::uintptr_t>(offset), kind, v))
            return false;
        if (kind == owner_handle)
        {
            const auto h = static_cast<std::uint32_t>(v);
            return h != invalid_handle && (h == t.handle.load(std::memory_order_relaxed) || h == t.alt_handle.load(std::memory_order_relaxed));
        }
        const auto e = static_cast<std::uintptr_t>(v);
        return e && (e == t.entity.load(std::memory_order_relaxed) || e == t.alt_entity.load(std::memory_order_relaxed));
    }

    bool scan_owner(std::uintptr_t object, std::uint32_t h, std::uintptr_t e, int& found, int& kind)
    {
        __try
        {
            for (int off = 0; off + 8 <= owner_scan_size; off += 4)
            {
                const std::uintptr_t at = object + static_cast<std::uintptr_t>(off);
                if (!(off & 7) && *reinterpret_cast<const std::uintptr_t*>(at) == e)
                {
                    found = off;
                    kind = owner_pointer;
                    return true;
                }
                if (*reinterpret_cast<const std::uint32_t*>(at) == h)
                {
                    found = off;
                    kind = owner_handle;
                    return true;
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
        return false;
    }

    void calibrate(std::uintptr_t object)
    {
        const std::uint32_t h = g_arms.handle.load(std::memory_order_relaxed);
        const std::uintptr_t e = g_arms.entity.load(std::memory_order_relaxed);
        if (h == invalid_handle || !e)
            return;
        int found = -1;
        int kind = owner_none;
        if (!scan_owner(object, h, e, found, kind))
            return;
        if (g_candidate_offset.load(std::memory_order_relaxed) == found && g_candidate_kind.load(std::memory_order_relaxed) == kind)
        {
            if (g_candidate_hits.fetch_add(1, std::memory_order_relaxed) + 1 >= calibration_hits)
            {
                g_owner_kind.store(kind, std::memory_order_relaxed);
                g_owner_offset.store(found, std::memory_order_release);
            }
        }
        else
        {
            g_candidate_offset.store(found, std::memory_order_relaxed);
            g_candidate_kind.store(kind, std::memory_order_relaxed);
            g_candidate_hits.store(1, std::memory_order_relaxed);
        }
    }
}

namespace features::visuals
{
    namespace detail
    {
        void update_chams_targets()
        {
            const auto& cfg = settings::g_visuals;
            if (!cfg.hands_tint && !cfg.weapon_tint)
            {
                clear_target(g_arms);
                clear_target(g_weapon);
                return;
            }

            const auto local = systems::g_local.get();
            if (!local.pawn || !local.is_alive)
            {
                clear_target(g_arms);
                clear_target(g_weapon);
                return;
            }

            const auto arms_handle = systems::reads::field<std::uint32_t>(local.pawn, SCHEMA("C_CSPlayerPawn", "m_hHudModelArms"_hash), invalid_handle);
            const std::uintptr_t arms = systems::g_entities.lookup(arms_handle);

            const std::uintptr_t hud_weapon = features::changer::find_hud_model_weapon(local.pawn);
            const std::uint32_t hud_handle = hud_weapon ? entity_handle(hud_weapon) : invalid_handle;

            std::uint32_t active_handle = invalid_handle;
            const std::uintptr_t active = systems::reads::active_weapon(local.pawn, &active_handle);

            set_target(g_arms, arms_handle, arms, invalid_handle, 0, cfg.hands_tint ? cfg.hands_color.abgr() : 0);
            set_target(g_weapon, hud_handle, hud_weapon, active_handle, active, cfg.weapon_tint ? cfg.weapon_color.abgr() : 0);
        }
    }

    bool chams::on_draw_object(std::uintptr_t desc, std::uintptr_t meshes, int count) const
    {
        if (!desc || !meshes || count <= 0 || count > max_mesh_count)
            return false;

        const std::uint32_t arms_tint = g_arms.tint.load(std::memory_order_relaxed);
        const std::uint32_t weapon_tint = g_weapon.tint.load(std::memory_order_relaxed);
        if (!arms_tint && !weapon_tint)
            return false;

        const int offset = g_owner_offset.load(std::memory_order_acquire);
        const int kind = g_owner_kind.load(std::memory_order_relaxed);
        bool tinted = false;
        for (int i = 0; i < count; ++i)
        {
            const std::uintptr_t mesh = meshes + static_cast<std::uintptr_t>(i) * mesh_stride;
            const auto object = memory::read<std::uintptr_t>(mesh + mesh_object);
            if (!object)
                continue;
            if (offset < 0)
            {
                calibrate(object);
                continue;
            }
            if (arms_tint && matches(object, offset, kind, g_arms))
                memory::write<std::uint32_t>(mesh + mesh_color, arms_tint);
            else if (weapon_tint && matches(object, offset, kind, g_weapon))
                memory::write<std::uint32_t>(mesh + mesh_color, weapon_tint);
            else
                continue;
            tinted = true;
        }
        return tinted;
    }
}
