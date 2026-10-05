#include "changer.h"
#include "changer_detail.h"
#include "../../core/schema.h"
#include "../../systems/entities.h"
#include "../../systems/game_reads.h"
#include "../../systems/local.h"
#include <algorithm>
#include <vector>

namespace
{
    namespace changer = features::changer;

    constexpr int refresh_count = 4;
    constexpr int refresh_interval = 8;

    template <typename Original>
    void capture_weapon(std::uintptr_t weapon, std::uintptr_t iv, Original& original)
    {
        using changer::detail::read_field;
        original.id_high = read_field<std::uint32_t>(iv, SCHEMA("C_EconItemView", "m_iItemIDHigh"_hash));
        original.id_low = read_field<std::uint32_t>(iv, SCHEMA("C_EconItemView", "m_iItemIDLow"_hash));
        original.account_id = read_field<std::uint32_t>(iv, SCHEMA("C_EconItemView", "m_iAccountID"_hash));
        original.initialized = read_field<bool>(iv, SCHEMA("C_EconItemView", "m_bInitialized"_hash));
        original.paint_kit = read_field<int>(weapon, SCHEMA("C_EconEntity", "m_nFallbackPaintKit"_hash));
        original.seed = read_field<int>(weapon, SCHEMA("C_EconEntity", "m_nFallbackSeed"_hash));
        original.wear = read_field<float>(weapon, SCHEMA("C_EconEntity", "m_flFallbackWear"_hash));
        original.stattrak = read_field<int>(weapon, SCHEMA("C_EconEntity", "m_nFallbackStatTrak"_hash), -1);
    }

    template <typename Original>
    void write_weapon(std::uintptr_t weapon, std::uintptr_t iv, const Original& original)
    {
        using changer::detail::write_field;
        write_field<std::uint32_t>(iv, SCHEMA("C_EconItemView", "m_iItemIDHigh"_hash), original.id_high);
        write_field<std::uint32_t>(iv, SCHEMA("C_EconItemView", "m_iItemIDLow"_hash), original.id_low);
        write_field<std::uint32_t>(iv, SCHEMA("C_EconItemView", "m_iAccountID"_hash), original.account_id);
        write_field<bool>(iv, SCHEMA("C_EconItemView", "m_bInitialized"_hash), original.initialized);
        write_field<int>(weapon, SCHEMA("C_EconEntity", "m_nFallbackPaintKit"_hash), original.paint_kit);
        write_field<int>(weapon, SCHEMA("C_EconEntity", "m_nFallbackSeed"_hash), original.seed);
        write_field<float>(weapon, SCHEMA("C_EconEntity", "m_flFallbackWear"_hash), original.wear);
        write_field<int>(weapon, SCHEMA("C_EconEntity", "m_nFallbackStatTrak"_hash), original.stattrak);
    }

    template <typename Applied>
    bool same_skin(const Applied& a, const Applied& b)
    {
        return a.paint_kit == b.paint_kit && a.seed == b.seed && a.wear == b.wear && a.stattrak == b.stattrak;
    }

    bool fallback_intact(std::uintptr_t weapon, std::uintptr_t iv, int paint_kit)
    {
        using changer::detail::read_field;
        return read_field<int>(weapon, SCHEMA("C_EconEntity", "m_nFallbackPaintKit"_hash)) == paint_kit &&
            read_field<std::uint32_t>(iv, SCHEMA("C_EconItemView", "m_iItemIDHigh"_hash)) == changer::detail::faux_id_high;
    }
}

namespace features::changer
{
    void guns::on_frame_stage_notify()
    {
        const auto local = systems::g_local.get();
        if (!local.pawn || !local.controller)
            return;

        if (m_tracked_pawn != local.pawn)
        {
            m_applied.clear();
            m_originals.clear();
            m_last_active_handle = 0;
            m_tracked_pawn = local.pawn;
        }

        std::uint32_t active_handle = systems::reads::invalid_handle;
        systems::reads::active_weapon(local.pawn, &active_handle);

        const auto handles = detail::weapon_handles(local.pawn);
        const std::uint32_t account = detail::account_id(local.controller);
        std::vector<std::uint32_t> owned;
        owned.reserve(handles.size());

        for (const std::uint32_t handle : handles)
        {
            const std::uintptr_t weapon = systems::g_entities.lookup(handle);
            const std::uintptr_t iv = detail::item_view(weapon);
            if (!weapon || !iv)
                continue;

            const auto* def = g_econ_item_system.find_def(static_cast<std::int16_t>(systems::reads::item_definition(weapon)));
            if (!def || def->category != econ_item_system::item_category::gun)
                continue;

            owned.push_back(handle);

            settings::changer::applied_skin skin{};
            if (!detail::selected_skin(def->def_index, local.team, skin) || skin.paint_kit_id <= 0)
            {
                const auto original = m_originals.find(handle);
                if (original != m_originals.end())
                {
                    write_weapon(weapon, iv, original->second);
                    rebuild_paint(weapon, handle, active_handle, local.pawn, g_econ_item_system.find_paint_kit(original->second.paint_kit));
                    invalidate_hud_icon(iv);
                    m_originals.erase(original);
                }
                m_applied.erase(handle);
                continue;
            }

            if (!detail::vdata_ready(weapon))
                continue;

            applied_state wanted{ skin.paint_kit_id, detail::sanitize_seed(skin.seed), detail::sanitize_wear(skin.wear), detail::sanitize_stattrak(skin.stattrak), refresh_count, refresh_interval };
            const auto applied = m_applied.find(handle);
            if (applied != m_applied.end() && same_skin(applied->second, wanted) && fallback_intact(weapon, iv, wanted.paint_kit))
            {
                applied_state& state = applied->second;
                if (state.refreshes > 0 && --state.countdown <= 0)
                {
                    --state.refreshes;
                    state.countdown = refresh_interval;
                    rebuild_paint(weapon, handle, active_handle, local.pawn, g_econ_item_system.find_paint_kit(state.paint_kit));
                    invalidate_hud_icon(iv);
                }
                continue;
            }

            if (!m_originals.contains(handle))
            {
                original_state original{};
                capture_weapon(weapon, iv, original);
                m_originals.emplace(handle, original);
            }

            skin.seed = wanted.seed;
            skin.wear = wanted.wear;
            skin.stattrak = wanted.stattrak;
            apply(weapon, iv, handle, active_handle, local.pawn, skin, account);
            m_applied[handle] = wanted;
        }

        std::erase_if(m_applied, [&owned](const auto& entry) { return std::find(owned.begin(), owned.end(), entry.first) == owned.end(); });
        std::erase_if(m_originals, [&owned](const auto& entry) { return std::find(owned.begin(), owned.end(), entry.first) == owned.end(); });

        if (active_handle != m_last_active_handle)
        {
            m_last_active_handle = active_handle;
            const auto applied = m_applied.find(active_handle);
            if (applied != m_applied.end())
            {
                const auto* pk = g_econ_item_system.find_paint_kit(applied->second.paint_kit);
                update_hud_mesh(local.pawn, pk);
                detail::rebuild_weapon_paint(systems::g_entities.lookup(active_handle), pk);
                applied->second.refreshes = refresh_count;
                applied->second.countdown = refresh_interval;
            }
        }
    }

    void guns::restore()
    {
        if (m_originals.empty())
        {
            m_applied.clear();
            m_last_active_handle = 0;
            return;
        }

        const auto local = systems::g_local.get();
        if (local.pawn && local.pawn == m_tracked_pawn)
        {
            std::uint32_t active_handle = systems::reads::invalid_handle;
            systems::reads::active_weapon(local.pawn, &active_handle);
            for (const auto& [handle, original] : m_originals)
            {
                const std::uintptr_t weapon = systems::g_entities.lookup(handle);
                const std::uintptr_t iv = detail::item_view(weapon);
                if (!weapon || !iv)
                    continue;
                write_weapon(weapon, iv, original);
                rebuild_paint(weapon, handle, active_handle, local.pawn, g_econ_item_system.find_paint_kit(original.paint_kit));
                invalidate_hud_icon(iv);
            }
        }

        m_applied.clear();
        m_originals.clear();
        m_last_active_handle = 0;
    }

    void guns::apply(std::uintptr_t weapon, std::uintptr_t iv, std::uint32_t handle, std::uint32_t active_handle, std::uintptr_t pawn, const settings::changer::applied_skin& skin, std::uint32_t account_id)
    {
        detail::write_field<std::uint32_t>(iv, SCHEMA("C_EconItemView", "m_iItemIDHigh"_hash), detail::faux_id_high);
        detail::write_field<std::uint32_t>(iv, SCHEMA("C_EconItemView", "m_iItemIDLow"_hash), detail::faux_id_low);
        detail::write_field<std::uint32_t>(iv, SCHEMA("C_EconItemView", "m_iAccountID"_hash), account_id);
        detail::write_field<bool>(iv, SCHEMA("C_EconItemView", "m_bInitialized"_hash), true);

        detail::write_field<int>(weapon, SCHEMA("C_EconEntity", "m_nFallbackPaintKit"_hash), skin.paint_kit_id);
        detail::write_field<int>(weapon, SCHEMA("C_EconEntity", "m_nFallbackSeed"_hash), skin.seed);
        detail::write_field<float>(weapon, SCHEMA("C_EconEntity", "m_flFallbackWear"_hash), skin.wear);
        detail::write_field<int>(weapon, SCHEMA("C_EconEntity", "m_nFallbackStatTrak"_hash), skin.stattrak);

        rebuild_paint(weapon, handle, active_handle, pawn, g_econ_item_system.find_paint_kit(skin.paint_kit_id));
        invalidate_hud_icon(iv);
    }

    void guns::rebuild_paint(std::uintptr_t weapon, std::uint32_t handle, std::uint32_t active_handle, std::uintptr_t pawn, const econ_item_system::paint_kit* pk)
    {
        if (handle == active_handle)
            update_hud_mesh(pawn, pk);
        detail::rebuild_weapon_paint(weapon, pk);
    }
}
