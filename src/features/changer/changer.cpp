#include "changer.h"
#include "changer_detail.h"
#include "../../core/addresses.h"
#include "../../core/cstypes.h"
#include "../../core/hash.h"
#include "../../core/hooks.h"
#include "../../core/patterns.h"
#include "../../core/schema.h"
#include "../../core/settings.h"
#include "../../systems/entities.h"
#include "../../systems/game_reads.h"
#include "../../systems/local.h"
#include <algorithm>
#include <cmath>
#include <mutex>
#include <string>

namespace
{
    constexpr std::uint32_t composite_offset_fallback = 0x608;
    constexpr std::uint32_t composite_offset_min = 0x100;
    constexpr std::uint32_t composite_offset_max = 0x4000;
    constexpr std::uintptr_t composite_offset_displacement = 3;
    constexpr std::uintptr_t weapons_data_offset = 0x8;
    constexpr int max_scene_children = 64;
    constexpr float default_wear = 0.0001f;
    constexpr int max_seed = 1000;
    constexpr int max_stattrak = 999999;

    bool g_glove_refresh = false;

    bool is_hud_weapon(std::uintptr_t entity)
    {
        const char* name = systems::g_entities.get_designer_name(entity);
        return systems::reads::designer_is(name, "cs2_hudmodel_weapon") || systems::reads::designer_is(name, "C_CS2HudModelWeapon");
    }
}

namespace features::changer::detail
{
    bool has_vfunc(std::uintptr_t object, std::size_t index)
    {
        const std::uintptr_t table = systems::reads::pointer(object);
        return table && systems::reads::readable(table + index * sizeof(std::uintptr_t)) && memory::read<std::uintptr_t>(table + index * sizeof(std::uintptr_t)) != 0;
    }

    std::uintptr_t item_view(std::uintptr_t weapon)
    {
        const std::uint32_t manager = SCHEMA("C_EconEntity", "m_AttributeManager"_hash);
        const std::uint32_t item = SCHEMA("C_AttributeContainer", "m_Item"_hash);
        if (!weapon || !manager || !item)
            return 0;
        const std::uintptr_t view = weapon + manager + item;
        return systems::reads::readable(view) ? view : 0;
    }

    bool vdata_ready(std::uintptr_t weapon)
    {
        const std::uint32_t subclass = SCHEMA("C_BaseEntity", "m_nSubclassID"_hash);
        if (!weapon || !subclass)
            return false;
        return systems::reads::value<std::uintptr_t>(weapon + subclass + subclass_vdata_offset, 0) != 0;
    }

    std::uint32_t account_id(std::uintptr_t controller)
    {
        const auto steam_id = read_field<std::uint64_t>(controller, SCHEMA("CBasePlayerController", "m_steamID"_hash));
        return static_cast<std::uint32_t>(steam_id & 0xFFFFFFFFull);
    }

    std::vector<std::uint32_t> weapon_handles(std::uintptr_t pawn)
    {
        std::vector<std::uint32_t> handles;
        const std::uintptr_t services = systems::reads::field_pointer(pawn, SCHEMA("C_BasePlayerPawn", "m_pWeaponServices"_hash));
        const std::uint32_t offset = SCHEMA("CPlayer_WeaponServices", "m_hMyWeapons"_hash);
        if (!services || !offset)
            return handles;
        const int size = systems::reads::value<int>(services + offset, 0);
        if (size <= 0 || static_cast<std::size_t>(size) > max_weapons)
            return handles;
        const std::uintptr_t data = systems::reads::value<std::uintptr_t>(services + offset + weapons_data_offset, 0);
        if (!data || !systems::reads::readable(data, static_cast<std::size_t>(size) * sizeof(std::uint32_t)))
            return handles;
        handles.reserve(static_cast<std::size_t>(size));
        for (int i = 0; i < size; ++i)
            handles.push_back(memory::read<std::uint32_t>(data + static_cast<std::uintptr_t>(i) * sizeof(std::uint32_t)));
        return handles;
    }

    std::uint32_t composite_offset()
    {
        const std::uintptr_t site = PATTERN(patterns::weapon_composite_offset);
        if (!site || !systems::reads::readable(site + composite_offset_displacement, sizeof(std::int32_t)))
            return composite_offset_fallback;
        const auto value = memory::read<std::int32_t>(site + composite_offset_displacement);
        if (value < static_cast<std::int32_t>(composite_offset_min) || value > static_cast<std::int32_t>(composite_offset_max))
            return composite_offset_fallback;
        return static_cast<std::uint32_t>(value);
    }

    void set_mesh_mask(std::uintptr_t entity, const econ_item_system::paint_kit* pk)
    {
        const std::uintptr_t function = PATTERN(patterns::weapon_set_mesh_group_mask);
        const std::uintptr_t node = systems::reads::scene_node(entity);
        if (!function || !node)
            return;
        const std::uint64_t mask = pk && pk->legacy_model ? 2ull : 1ull;
        memory::call<void>(function, node, mask);
    }

    void post_data_update(std::uintptr_t entity)
    {
        if (has_vfunc(entity, post_data_update_index))
            memory::call_vfunc<void>(entity, post_data_update_index, 1);
    }

    void rebuild_weapon_paint(std::uintptr_t weapon, const econ_item_system::paint_kit* pk)
    {
        if (!weapon)
            return;
        set_mesh_mask(weapon, pk);
        const std::uintptr_t composite = PATTERN(patterns::weapon_update_composite_material);
        const std::uintptr_t update = PATTERN(patterns::weapon_update_skin);
        if (!composite || !update)
            return;
        memory::call<void>(composite, weapon + composite_offset(), true);
        post_data_update(weapon);
        memory::call<void>(update, weapon, true);
    }

    float sanitize_wear(float wear)
    {
        if (!std::isfinite(wear))
            return default_wear;
        return std::clamp(wear, 0.f, 1.f);
    }

    int sanitize_seed(int seed)
    {
        return std::clamp(seed, 0, max_seed);
    }

    int sanitize_stattrak(int stattrak)
    {
        return stattrak < 0 ? -1 : std::min(stattrak, max_stattrak);
    }

    bool selected_skin(std::int16_t def_index, int team, settings::changer::applied_skin& out)
    {
        std::lock_guard lock(settings::g_changer_mutex);
        const auto* skin = settings::g_changer.find(def_index, team);
        if (!skin)
            return false;
        out = *skin;
        return true;
    }

    std::int16_t selected_knife(int team)
    {
        std::lock_guard lock(settings::g_changer_mutex);
        return settings::g_changer.knife(team);
    }

    std::int16_t selected_glove(int team)
    {
        std::lock_guard lock(settings::g_changer_mutex);
        return settings::g_changer.glove(team);
    }

    std::int16_t selected_agent(int team)
    {
        std::lock_guard lock(settings::g_changer_mutex);
        if (team == 3)
            return settings::g_changer.agents.ct_def;
        if (team == 2)
            return settings::g_changer.agents.t_def;
        return 0;
    }

    bool knife_animations()
    {
        std::lock_guard lock(settings::g_changer_mutex);
        return settings::g_changer.knife_animations;
    }

    void request_glove_refresh()
    {
        g_glove_refresh = true;
    }

    bool consume_glove_refresh()
    {
        const bool value = g_glove_refresh;
        g_glove_refresh = false;
        return value;
    }
}

namespace features::changer
{
    std::uintptr_t find_hud_model_weapon(std::uintptr_t pawn)
    {
        const std::uint32_t arms_offset = SCHEMA("C_CSPlayerPawn", "m_hHudModelArms"_hash);
        const std::uint32_t child_offset = SCHEMA("CGameSceneNode", "m_pChild"_hash);
        const std::uint32_t sibling_offset = SCHEMA("CGameSceneNode", "m_pNextSibling"_hash);
        const std::uint32_t owner_offset = SCHEMA("CGameSceneNode", "m_pOwner"_hash);
        if (!pawn || !arms_offset || !child_offset || !sibling_offset || !owner_offset)
            return 0;
        const auto handle = systems::reads::field<std::uint32_t>(pawn, arms_offset, systems::reads::invalid_handle);
        const std::uintptr_t arms = systems::g_entities.lookup(handle);
        const std::uintptr_t node = systems::reads::scene_node(arms);
        if (!node)
            return 0;
        std::uintptr_t child = systems::reads::pointer(node + child_offset);
        for (int i = 0; child && i < max_scene_children; ++i)
        {
            const std::uintptr_t owner = systems::reads::pointer(child + owner_offset);
            if (owner && is_hud_weapon(owner))
                return owner;
            child = systems::reads::pointer(child + sibling_offset);
        }
        return 0;
    }

    void update_hud_mesh(std::uintptr_t pawn, const econ_item_system::paint_kit* pk)
    {
        detail::set_mesh_mask(find_hud_model_weapon(pawn), pk);
    }

    void invalidate_hud_icon(std::uintptr_t item_view)
    {
        const std::uintptr_t invalidate = PATTERN(patterns::econ_item_view_invalidate_description);
        if (item_view && invalidate)
            memory::call<void>(invalidate, item_view);
    }

    std::uint32_t make_subclass_token(std::int16_t def_index)
    {
        return hash::murmur2_lower(std::to_string(def_index).c_str(), 0x31415926u);
    }

    void on_frame_stage(int stage)
    {
        if (stage != cstypes::frame_stage::update || hooks::unloading.load())
            return;

        bool enabled = false;
        {
            std::lock_guard lock(settings::g_changer_mutex);
            enabled = settings::g_changer.enabled;
        }

        if (!enabled)
        {
            g_guns.restore();
            g_knives.restore();
            g_gloves.restore();
            g_agents.restore();
            return;
        }

        if (!g_econ_item_system.ready())
            return;

        const auto local = systems::g_local.get();
        if (!local.pawn || !local.controller || !local.is_alive || (local.team != 2 && local.team != 3))
            return;
        if (systems::g_local.is_in_cinematic())
            return;

        g_agents.on_frame_stage_notify();
        g_gloves.on_frame_stage_notify();
        g_knives.on_frame_stage_notify();
        g_guns.on_frame_stage_notify();
    }

    void on_unload()
    {
        g_guns.restore();
        g_knives.restore();
        g_gloves.restore();
        g_agents.restore();
        detail::shutdown_econ();
    }
}
