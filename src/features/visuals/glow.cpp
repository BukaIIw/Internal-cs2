#include "visuals.h"
#include "../../core/cstypes.h"
#include "../../core/schema.h"
#include "../../core/settings.h"
#include "../../systems/entities.h"
#include "../../systems/game_reads.h"
#include "../../systems/local.h"
#include <array>
#include <atomic>
#include <cstdint>

namespace
{
    constexpr int forced_glow_type = 3;
    constexpr int slot_count = 65;

    struct glow_offsets
    {
        std::uint32_t glow = 0;
        std::uint32_t type = 0;
        std::uint32_t color = 0;
        std::uint32_t glowing = 0;
        std::uint32_t range = 0;
        std::uint32_t range_min = 0;
        std::uint32_t flashing = 0;

        bool valid() const { return glow && type && color && glowing; }
    };

    struct saved_glow
    {
        std::uintptr_t pawn = 0;
        std::uint32_t handle = 0xFFFFFFFFu;
        int type = 0;
        int range = 0;
        int range_min = 0;
        std::uint32_t color = 0;
        bool flashing = false;
        bool glowing = false;
    };

    std::array<saved_glow, slot_count> g_saved{};
    std::array<std::atomic<std::uintptr_t>, slot_count> g_properties{};
    std::array<std::atomic<std::uint32_t>, slot_count> g_colors{};

    glow_offsets offsets()
    {
        glow_offsets o{};
        o.glow = SCHEMA("C_BaseModelEntity", "m_Glow"_hash);
        o.type = SCHEMA("CGlowProperty", "m_iGlowType"_hash);
        o.color = SCHEMA("CGlowProperty", "m_glowColorOverride"_hash);
        o.glowing = SCHEMA("CGlowProperty", "m_bGlowing"_hash);
        o.range = SCHEMA("CGlowProperty", "m_nGlowRange"_hash);
        o.range_min = SCHEMA("CGlowProperty", "m_nGlowRangeMin"_hash);
        o.flashing = SCHEMA("CGlowProperty", "m_bFlashing"_hash);
        return o;
    }

    bool property_ready(std::uintptr_t property, const glow_offsets& o)
    {
        if (!systems::reads::readable(property + o.type, sizeof(int)))
            return false;
        if (!systems::reads::readable(property + o.color, sizeof(std::uint32_t)))
            return false;
        if (!systems::reads::readable(property + o.glowing, sizeof(bool)))
            return false;
        if (o.range && !systems::reads::readable(property + o.range, sizeof(int)))
            return false;
        if (o.range_min && !systems::reads::readable(property + o.range_min, sizeof(int)))
            return false;
        if (o.flashing && !systems::reads::readable(property + o.flashing, sizeof(bool)))
            return false;
        return true;
    }

    void save(saved_glow& s, std::uintptr_t pawn, std::uint32_t handle, std::uintptr_t property, const glow_offsets& o)
    {
        s.pawn = pawn;
        s.handle = handle;
        s.type = memory::read<int>(property + o.type);
        s.range = o.range ? memory::read<int>(property + o.range) : 0;
        s.range_min = o.range_min ? memory::read<int>(property + o.range_min) : 0;
        s.color = memory::read<std::uint32_t>(property + o.color);
        s.flashing = o.flashing ? memory::read<bool>(property + o.flashing) : false;
        s.glowing = memory::read<bool>(property + o.glowing);
    }

    void restore_slot(int index, const glow_offsets& o)
    {
        auto& s = g_saved[index];
        g_properties[index].store(0, std::memory_order_relaxed);
        if (s.pawn && o.valid() && systems::g_entities.lookup(s.handle) == s.pawn)
        {
            const std::uintptr_t property = s.pawn + o.glow;
            if (property_ready(property, o))
            {
                memory::write<int>(property + o.type, s.type);
                if (o.range)
                    memory::write<int>(property + o.range, s.range);
                if (o.range_min)
                    memory::write<int>(property + o.range_min, s.range_min);
                memory::write<std::uint32_t>(property + o.color, s.color);
                if (o.flashing)
                    memory::write<bool>(property + o.flashing, s.flashing);
                memory::write<bool>(property + o.glowing, s.glowing);
            }
        }
        s = saved_glow{};
    }

    void write(std::uintptr_t property, std::uint32_t color, const glow_offsets& o)
    {
        memory::write<std::uint32_t>(property + o.color, color);
        memory::write<int>(property + o.type, forced_glow_type);
        if (o.range)
            memory::write<int>(property + o.range, 0);
        if (o.range_min)
            memory::write<int>(property + o.range_min, 0);
        if (o.flashing)
            memory::write<bool>(property + o.flashing, false);
        memory::write<bool>(property + o.glowing, true);
    }

    void restore_all()
    {
        const glow_offsets o = offsets();
        for (int i = 0; i < slot_count; ++i)
            if (g_saved[i].pawn)
                restore_slot(i, o);
            else
                g_properties[i].store(0, std::memory_order_relaxed);
    }

    int slot_of(std::uintptr_t property)
    {
        if (!property)
            return -1;
        for (int i = 1; i < slot_count; ++i)
            if (g_properties[i].load(std::memory_order_relaxed) == property)
                return i;
        return -1;
    }
}

namespace features::visuals
{
    void glow::on_frame_stage(int stage)
    {
        if (stage != cstypes::frame_stage::update)
            return;

        const auto& cfg = settings::g_visuals;
        const glow_offsets o = offsets();
        const auto local = systems::g_local.get();
        if (!cfg.glow || !o.valid() || !local.controller)
        {
            restore_all();
            return;
        }

        const auto players = systems::g_entities.players();
        for (int i = 1; i < slot_count; ++i)
        {
            const auto& p = players[i];
            auto& s = g_saved[i];

            bool want = p.valid && p.pawn && p.alive && !p.dormant && p.health > 0 && p.pawn != local.pawn && p.index == i;
            const bool team = want && !p.enemy;
            if (team && !cfg.glow_teammates)
                want = false;

            const std::uintptr_t property = want ? p.pawn + o.glow : 0;
            if (want && !property_ready(property, o))
                want = false;

            if (s.pawn && (!want || s.pawn != p.pawn))
                restore_slot(i, o);
            if (!want)
                continue;

            if (!s.pawn)
                save(s, p.pawn, p.pawn_handle, property, o);

            const settings::color& c = team ? cfg.glow_team : !cfg.glow_by_visibility || p.visible ? cfg.glow_visible : cfg.glow_hidden;
            const std::uint32_t color = c.abgr();
            write(property, color, o);
            g_colors[i].store(color, std::memory_order_relaxed);
            g_properties[i].store(property, std::memory_order_relaxed);
        }
    }

    bool glow::override_glow(std::uintptr_t glow_property, bool original) const
    {
        if (!settings::g_visuals.glow)
            return original;
        return slot_of(glow_property) > 0 ? true : original;
    }

    bool glow::override_color(std::uintptr_t glow_property, float* rgba) const
    {
        if (!rgba || !settings::g_visuals.glow)
            return false;
        const int slot = slot_of(glow_property);
        if (slot <= 0)
            return false;
        const std::uint32_t color = g_colors[slot].load(std::memory_order_relaxed);
        for (int i = 0; i < 4; ++i)
            rgba[i] = static_cast<float>((color >> (i * 8)) & 0xFF) / 255.f;
        return true;
    }

    void glow::restore()
    {
        restore_all();
    }
}
