#include "changer.h"
#include "changer_detail.h"
#include "../../core/addresses.h"
#include "../../core/patterns.h"
#include "../../core/schema.h"
#include "../../systems/entities.h"
#include "../../systems/game_reads.h"
#include "../../systems/local.h"
#include <string>

namespace
{
    namespace changer = features::changer;

    constexpr int deploy_min_frames = 2;
    constexpr int deploy_max_frames = 64;
    constexpr int anim_graph_rebuild_mode = 2;

    struct deploy_state
    {
        bool pending = false;
        int frames = 0;
    };

    deploy_state g_deploy{};

    std::string model_path(std::uintptr_t iv, std::int16_t def_index)
    {
        if (const std::uintptr_t function = PATTERN(patterns::weapon_get_model_path))
        {
            const auto* path = memory::call<const char*>(function, iv);
            if (path)
            {
                std::string out = memory::read_string(reinterpret_cast<std::uintptr_t>(path));
                if (!out.empty())
                    return out;
            }
        }
        const auto* def = changer::g_econ_item_system.find_def(def_index);
        return def ? def->model_player : std::string{};
    }

    void swap_model(std::uintptr_t weapon, std::uintptr_t hud, std::uintptr_t iv, std::uint32_t token, std::int16_t def_index)
    {
        const std::uint32_t subclass = SCHEMA("C_BaseEntity", "m_nSubclassID"_hash);
        if (!subclass || !weapon || !iv)
            return;

        changer::detail::write_field<std::uint32_t>(weapon, subclass, token);
        if (hud)
            changer::detail::write_field<std::uint32_t>(hud, subclass, token);

        const std::string path = model_path(iv, def_index);
        const std::uintptr_t set_model = PATTERN(patterns::set_player_model);
        if (set_model && !path.empty())
        {
            memory::call<void>(set_model, weapon, path.c_str());
            if (hud)
                memory::call<void>(set_model, hud, path.c_str());
        }

        if (const std::uintptr_t update_subclass = PATTERN(patterns::weapon_get_viewmodel))
        {
            memory::call<void>(update_subclass, weapon);
            if (hud)
                memory::call<void>(update_subclass, hud);
        }
    }

    std::uintptr_t animation_controller(std::uintptr_t entity)
    {
        const std::uint32_t body_offset = SCHEMA("C_BaseEntity", "m_CBodyComponent"_hash);
        const std::uint32_t controller_offset = SCHEMA("CBodyComponentBaseAnimGraph", "m_animationController"_hash);
        if (!entity || !body_offset || !controller_offset)
            return 0;
        const std::uintptr_t body = systems::reads::pointer(entity + body_offset);
        if (!body || !systems::reads::readable(body + controller_offset))
            return 0;
        return body + controller_offset;
    }
}

namespace features::changer
{
    void knives::on_frame_stage_notify()
    {
        const auto local = systems::g_local.get();
        if (!local.pawn || !local.controller)
            return;

        if (m_tracked_pawn != local.pawn)
        {
            m_original = {};
            m_overridden = false;
            m_last_active_handle = 0;
            m_weapon_handle = 0;
            m_tracked_pawn = local.pawn;
            g_deploy = {};
        }

        std::uint32_t active_handle = systems::reads::invalid_handle;
        const std::uintptr_t active_weapon = systems::reads::active_weapon(local.pawn, &active_handle);

        const std::int16_t knife_def = detail::selected_knife(local.team);
        const auto* selected = knife_def ? g_econ_item_system.find_def(knife_def) : nullptr;
        if (selected && selected->category != econ_item_system::item_category::knife)
            selected = nullptr;
        settings::changer::applied_skin skin{};
        if (selected)
            detail::selected_skin(knife_def, local.team, skin);

        std::uint32_t knife_handle = 0;
        std::uintptr_t weapon = 0;
        for (const std::uint32_t handle : detail::weapon_handles(local.pawn))
        {
            const std::uintptr_t candidate = systems::g_entities.lookup(handle);
            const auto* def = g_econ_item_system.find_def(static_cast<std::int16_t>(systems::reads::item_definition(candidate)));
            if (candidate && def && def->category == econ_item_system::item_category::knife)
            {
                knife_handle = handle;
                weapon = candidate;
                break;
            }
        }

        if (!weapon)
        {
            m_original = {};
            m_overridden = false;
            m_weapon_handle = 0;
            m_last_active_handle = active_handle;
            g_deploy = {};
            return;
        }

        if (knife_handle != m_weapon_handle)
        {
            m_original = {};
            m_overridden = false;
            m_weapon_handle = knife_handle;
            g_deploy = {};
        }

        const std::uintptr_t iv = detail::item_view(weapon);
        if (!iv || !detail::vdata_ready(weapon))
            return;

        if (!selected)
        {
            if (m_overridden)
                restore(weapon, iv, active_weapon, local.pawn);
            m_last_active_handle = active_handle;
            return;
        }

        capture_original(weapon, iv);

        skin.seed = detail::sanitize_seed(skin.seed);
        skin.wear = detail::sanitize_wear(skin.wear);
        skin.stattrak = detail::sanitize_stattrak(skin.stattrak);
        if (skin.paint_kit_id < 0)
            skin.paint_kit_id = 0;

        const std::uint32_t token = make_subclass_token(selected->def_index);
        const bool model_ready = detail::read_field<std::uint32_t>(weapon, SCHEMA("C_BaseEntity", "m_nSubclassID"_hash)) == token;
        const bool matches = m_overridden && model_ready &&
            detail::read_field<std::uint16_t>(iv, SCHEMA("C_EconItemView", "m_iItemDefinitionIndex"_hash)) == static_cast<std::uint16_t>(selected->def_index) &&
            detail::read_field<std::uint32_t>(iv, SCHEMA("C_EconItemView", "m_iItemIDHigh"_hash)) == detail::faux_id_high &&
            detail::read_field<int>(weapon, SCHEMA("C_EconEntity", "m_nFallbackPaintKit"_hash)) == skin.paint_kit_id &&
            detail::read_field<int>(weapon, SCHEMA("C_EconEntity", "m_nFallbackSeed"_hash)) == skin.seed &&
            detail::read_field<float>(weapon, SCHEMA("C_EconEntity", "m_flFallbackWear"_hash)) == skin.wear &&
            detail::read_field<int>(weapon, SCHEMA("C_EconEntity", "m_nFallbackStatTrak"_hash)) == skin.stattrak;

        if (!matches)
        {
            apply(weapon, iv, selected, skin, detail::account_id(local.controller), active_weapon, local.pawn);
            if (!model_ready && active_handle == m_weapon_handle)
                g_deploy = { true, 0 };
        }

        if (active_handle != m_last_active_handle)
        {
            m_last_active_handle = active_handle;
            if (m_overridden && active_handle == m_weapon_handle)
                g_deploy = { true, 0 };
            else
                g_deploy = {};
        }

        on_deploy(local.pawn);
    }

    void knives::restore()
    {
        if (m_overridden)
        {
            const auto local = systems::g_local.get();
            const std::uintptr_t weapon = local.pawn && local.pawn == m_tracked_pawn ? systems::g_entities.lookup(m_weapon_handle) : 0;
            const std::uintptr_t iv = detail::item_view(weapon);
            if (weapon && iv)
                restore(weapon, iv, systems::reads::active_weapon(local.pawn), local.pawn);
        }
        m_original = {};
        m_overridden = false;
        m_last_active_handle = 0;
        g_deploy = {};
    }

    void knives::capture_original(std::uintptr_t weapon, std::uintptr_t iv)
    {
        if (m_original.captured)
            return;
        m_original.def_index = detail::read_field<std::uint16_t>(iv, SCHEMA("C_EconItemView", "m_iItemDefinitionIndex"_hash));
        m_original.id_high = detail::read_field<std::uint32_t>(iv, SCHEMA("C_EconItemView", "m_iItemIDHigh"_hash));
        m_original.id_low = detail::read_field<std::uint32_t>(iv, SCHEMA("C_EconItemView", "m_iItemIDLow"_hash));
        m_original.account_id = detail::read_field<std::uint32_t>(iv, SCHEMA("C_EconItemView", "m_iAccountID"_hash));
        m_original.initialized = detail::read_field<bool>(iv, SCHEMA("C_EconItemView", "m_bInitialized"_hash));
        m_original.paint_kit = detail::read_field<int>(weapon, SCHEMA("C_EconEntity", "m_nFallbackPaintKit"_hash));
        m_original.seed = detail::read_field<int>(weapon, SCHEMA("C_EconEntity", "m_nFallbackSeed"_hash));
        m_original.wear = detail::read_field<float>(weapon, SCHEMA("C_EconEntity", "m_flFallbackWear"_hash));
        m_original.stattrak = detail::read_field<int>(weapon, SCHEMA("C_EconEntity", "m_nFallbackStatTrak"_hash), -1);
        m_original.subclass = detail::read_field<std::uint32_t>(weapon, SCHEMA("C_BaseEntity", "m_nSubclassID"_hash));
        m_original.captured = true;
    }

    void knives::apply(std::uintptr_t weapon, std::uintptr_t iv, const econ_item_system::item_def* def, const settings::changer::applied_skin& skin, std::uint32_t account_id, std::uintptr_t active_weapon, std::uintptr_t pawn)
    {
        if (!def)
            return;

        const auto def_index = static_cast<std::uint16_t>(def->def_index);
        const bool model_matches =
            detail::read_field<std::uint32_t>(weapon, SCHEMA("C_BaseEntity", "m_nSubclassID"_hash)) == make_subclass_token(def->def_index) &&
            detail::read_field<std::uint16_t>(iv, SCHEMA("C_EconItemView", "m_iItemDefinitionIndex"_hash)) == def_index;

        detail::write_field<std::uint16_t>(iv, SCHEMA("C_EconItemView", "m_iItemDefinitionIndex"_hash), def_index);
        detail::write_field<std::uint32_t>(iv, SCHEMA("C_EconItemView", "m_iItemIDHigh"_hash), detail::faux_id_high);
        detail::write_field<std::uint32_t>(iv, SCHEMA("C_EconItemView", "m_iItemIDLow"_hash), detail::faux_id_low);
        detail::write_field<std::uint32_t>(iv, SCHEMA("C_EconItemView", "m_iAccountID"_hash), account_id);
        detail::write_field<bool>(iv, SCHEMA("C_EconItemView", "m_bInitialized"_hash), true);

        detail::write_field<int>(weapon, SCHEMA("C_EconEntity", "m_nFallbackPaintKit"_hash), skin.paint_kit_id);
        detail::write_field<int>(weapon, SCHEMA("C_EconEntity", "m_nFallbackSeed"_hash), skin.seed);
        detail::write_field<float>(weapon, SCHEMA("C_EconEntity", "m_flFallbackWear"_hash), skin.wear);
        detail::write_field<int>(weapon, SCHEMA("C_EconEntity", "m_nFallbackStatTrak"_hash), skin.stattrak);

        if (!model_matches)
            update_model(weapon, iv, def_index, pawn);

        rebuild_paint(weapon, active_weapon, pawn, g_econ_item_system.find_paint_kit(skin.paint_kit_id));
        invalidate_hud_icon(iv);
        m_overridden = true;
    }

    void knives::restore(std::uintptr_t weapon, std::uintptr_t iv, std::uintptr_t active_weapon, std::uintptr_t pawn)
    {
        if (!m_overridden || !m_original.captured)
        {
            m_overridden = false;
            return;
        }

        detail::write_field<std::uint16_t>(iv, SCHEMA("C_EconItemView", "m_iItemDefinitionIndex"_hash), m_original.def_index);
        detail::write_field<std::uint32_t>(iv, SCHEMA("C_EconItemView", "m_iItemIDHigh"_hash), m_original.id_high);
        detail::write_field<std::uint32_t>(iv, SCHEMA("C_EconItemView", "m_iItemIDLow"_hash), m_original.id_low);
        detail::write_field<std::uint32_t>(iv, SCHEMA("C_EconItemView", "m_iAccountID"_hash), m_original.account_id);
        detail::write_field<bool>(iv, SCHEMA("C_EconItemView", "m_bInitialized"_hash), m_original.initialized);

        detail::write_field<int>(weapon, SCHEMA("C_EconEntity", "m_nFallbackPaintKit"_hash), m_original.paint_kit);
        detail::write_field<int>(weapon, SCHEMA("C_EconEntity", "m_nFallbackSeed"_hash), m_original.seed);
        detail::write_field<float>(weapon, SCHEMA("C_EconEntity", "m_flFallbackWear"_hash), m_original.wear);
        detail::write_field<int>(weapon, SCHEMA("C_EconEntity", "m_nFallbackStatTrak"_hash), m_original.stattrak);

        const auto original_def = static_cast<std::int16_t>(m_original.def_index);
        const std::uint32_t token = m_original.subclass ? m_original.subclass : make_subclass_token(original_def);
        const std::uintptr_t hud = weapon == active_weapon ? find_hud_model_weapon(pawn) : 0;
        swap_model(weapon, hud, iv, token, original_def);

        rebuild_paint(weapon, active_weapon, pawn, g_econ_item_system.find_paint_kit(m_original.paint_kit));
        invalidate_hud_icon(iv);

        m_original = {};
        m_overridden = false;
        g_deploy = {};
    }

    void knives::update_model(std::uintptr_t weapon, std::uintptr_t iv, std::uint16_t def_index, std::uintptr_t pawn)
    {
        const auto index = static_cast<std::int16_t>(def_index);
        const std::uintptr_t hud = systems::reads::active_weapon(pawn) == weapon ? find_hud_model_weapon(pawn) : 0;
        swap_model(weapon, hud, iv, make_subclass_token(index), index);
    }

    void knives::rebuild_paint(std::uintptr_t weapon, std::uintptr_t active_weapon, std::uintptr_t pawn, const econ_item_system::paint_kit* pk)
    {
        if (weapon == active_weapon)
            update_hud_mesh(pawn, pk);
        detail::rebuild_weapon_paint(weapon, pk);
    }

    void knives::on_deploy(std::uintptr_t pawn)
    {
        if (!g_deploy.pending)
            return;
        if (!m_overridden || ++g_deploy.frames > deploy_max_frames)
        {
            g_deploy = {};
            return;
        }
        if (g_deploy.frames < deploy_min_frames)
            return;

        const std::uintptr_t weapon = systems::g_entities.lookup(m_weapon_handle);
        const std::uintptr_t iv = detail::item_view(weapon);
        const std::uintptr_t hud = find_hud_model_weapon(pawn);
        const std::uint32_t subclass = SCHEMA("C_BaseEntity", "m_nSubclassID"_hash);
        if (!weapon || !iv || !hud || !subclass)
            return;

        const auto def_index = static_cast<std::int16_t>(detail::read_field<std::uint16_t>(iv, SCHEMA("C_EconItemView", "m_iItemDefinitionIndex"_hash)));
        const std::uint32_t token = make_subclass_token(def_index);
        const auto hud_subclass = detail::read_field<std::uint32_t>(hud, subclass);
        if (hud_subclass != token)
        {
            const std::uint32_t original_token = m_original.subclass ? m_original.subclass : make_subclass_token(static_cast<std::int16_t>(m_original.def_index));
            if (hud_subclass != original_token)
                return;
            detail::write_field<std::uint32_t>(hud, subclass, token);
            const std::string path = model_path(iv, def_index);
            const std::uintptr_t set_model = PATTERN(patterns::set_player_model);
            if (set_model && !path.empty())
                memory::call<void>(set_model, hud, path.c_str());
            if (const std::uintptr_t update_subclass = PATTERN(patterns::weapon_get_viewmodel))
                memory::call<void>(update_subclass, hud);
        }

        update_hud_mesh(pawn, g_econ_item_system.find_paint_kit(detail::read_field<int>(weapon, SCHEMA("C_EconEntity", "m_nFallbackPaintKit"_hash))));

        if (detail::knife_animations())
        {
            const std::uintptr_t rebuild = PATTERN(patterns::anim_graph_rebuild);
            const std::uintptr_t controller = animation_controller(hud);
            if (rebuild && controller)
                memory::call<void>(rebuild, controller, anim_graph_rebuild_mode);
        }

        g_deploy = {};
    }
}
