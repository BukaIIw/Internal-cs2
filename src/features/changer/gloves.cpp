#include "changer.h"
#include "changer_detail.h"
#include "../../core/addresses.h"
#include "../../core/patterns.h"
#include "../../core/schema.h"
#include "../../systems/entities.h"
#include "../../systems/game_reads.h"
#include "../../systems/local.h"
#include <array>

namespace
{
    constexpr std::array<std::uint16_t, 3> attribute_indices{ 6, 7, 8 };
    constexpr std::array<const char*, 3> attribute_names{
        "set item texture prefab",
        "set item texture seed",
        "set item texture wear"
    };

    constexpr std::uintptr_t attribute_count_offset = 0x210;
    constexpr std::uintptr_t attribute_data_offset = 0x218;
    constexpr std::uintptr_t attribute_stride = 0x48;
    constexpr std::uintptr_t attribute_definition_offset = 0x30;
    constexpr std::uintptr_t attribute_value_offset = 0x34;
    constexpr int max_attribute_count = 16384;
    constexpr int attribute_retry_frames = 32;
    constexpr int header_retry_frames = 4;
    constexpr int bodygroup_index = 0;
    constexpr std::uint32_t bodygroup_value = 1;

    struct applied_extra
    {
        int seed = 0;
        float wear = 0.f;
        int frames_since_apply = 0;
    };

    applied_extra g_extra{};
}

namespace features::changer
{
    void gloves::on_frame_stage_notify()
    {
        const auto local = systems::g_local.get();
        if (!local.pawn || !local.controller)
            return;

        if (m_tracked_pawn != local.pawn)
        {
            reset();
            m_tracked_pawn = local.pawn;
        }

        const std::uint32_t econ_offset = SCHEMA("C_CSPlayerPawn", "m_EconGloves"_hash);
        if (!econ_offset)
            return;
        const std::uintptr_t item_view = local.pawn + econ_offset;
        if (!systems::reads::readable(item_view))
            return;

        const std::int16_t glove_def = detail::selected_glove(local.team);
        const auto* def = glove_def ? g_econ_item_system.find_def(glove_def) : nullptr;
        if (def && def->category != econ_item_system::item_category::glove)
            def = nullptr;
        settings::changer::applied_skin skin{};
        if (def && !detail::selected_skin(glove_def, local.team, skin))
            def = nullptr;

        if (!def)
        {
            if (m_overridden)
                restore(local.pawn, item_view, local.team);
            return;
        }

        const auto arms_handle = detail::read_field<std::uint32_t>(local.pawn, SCHEMA("C_CSPlayerPawn", "m_hHudModelArms"_hash), systems::reads::invalid_handle);
        if (!systems::g_entities.lookup(arms_handle))
            return;

        if (!m_original.captured && !capture_original(item_view))
            return;

        skin.seed = detail::sanitize_seed(skin.seed);
        skin.wear = detail::sanitize_wear(skin.wear);

        ++g_extra.frames_since_apply;
        const bool forced = detail::consume_glove_refresh();
        const bool same_selection = m_overridden && m_applied_def == def->def_index && m_applied_paint == skin.paint_kit_id && g_extra.seed == skin.seed && g_extra.wear == skin.wear;
        const bool header_ok =
            detail::read_field<std::uint16_t>(item_view, SCHEMA("C_EconItemView", "m_iItemDefinitionIndex"_hash)) == static_cast<std::uint16_t>(def->def_index) &&
            detail::read_field<std::uint64_t>(item_view, SCHEMA("C_EconItemView", "m_iItemID"_hash)) == detail::faux_item_id;

        if (!forced && same_selection)
        {
            if (header_ok && paint_attributes_match(item_view, skin))
                return;
            if (g_extra.frames_since_apply < (header_ok ? attribute_retry_frames : header_retry_frames))
                return;
        }

        apply(local.pawn, item_view, local.team, *def, skin, detail::account_id(local.controller));
    }

    void gloves::restore()
    {
        if (m_overridden)
        {
            const auto local = systems::g_local.get();
            const std::uint32_t econ_offset = SCHEMA("C_CSPlayerPawn", "m_EconGloves"_hash);
            if (local.pawn && local.pawn == m_tracked_pawn && econ_offset)
                restore(local.pawn, local.pawn + econ_offset, local.team);
        }
        reset();
    }

    bool gloves::capture_original(std::uintptr_t item_view)
    {
        if (!read_paint_attributes(item_view, m_original_attributes))
            return false;

        m_original.def_index = detail::read_field<std::uint16_t>(item_view, SCHEMA("C_EconItemView", "m_iItemDefinitionIndex"_hash));
        m_original.item_id = detail::read_field<std::uint64_t>(item_view, SCHEMA("C_EconItemView", "m_iItemID"_hash));
        m_original.id_high = detail::read_field<std::uint32_t>(item_view, SCHEMA("C_EconItemView", "m_iItemIDHigh"_hash));
        m_original.id_low = detail::read_field<std::uint32_t>(item_view, SCHEMA("C_EconItemView", "m_iItemIDLow"_hash));
        m_original.account_id = detail::read_field<std::uint32_t>(item_view, SCHEMA("C_EconItemView", "m_iAccountID"_hash));
        m_original.restore_custom_material = detail::read_field<bool>(item_view, SCHEMA("C_EconItemView", "m_bRestoreCustomMaterialAfterPrecache"_hash));
        m_original.initialized = detail::read_field<bool>(item_view, SCHEMA("C_EconItemView", "m_bInitialized"_hash));
        m_original.disallow_soc = detail::read_field<bool>(item_view, SCHEMA("C_EconItemView", "m_bDisallowSOC"_hash));
        m_original.captured = true;
        return true;
    }

    bool gloves::read_paint_attributes(std::uintptr_t item_view, std::array<attribute_state, 3>& attributes) const
    {
        attributes = {};

        if (!systems::reads::readable(item_view + attribute_count_offset, sizeof(int)) || !systems::reads::readable(item_view + attribute_data_offset))
            return false;

        const int count = memory::read<int>(item_view + attribute_count_offset);
        if (count < 0 || count > max_attribute_count)
            return false;
        if (!count)
            return true;

        const auto data = memory::read<std::uintptr_t>(item_view + attribute_data_offset);
        if (!data || !systems::reads::readable(data, static_cast<std::size_t>(count) * attribute_stride))
            return false;

        for (int i = 0; i < count; ++i)
        {
            const std::uintptr_t attribute = data + static_cast<std::uintptr_t>(i) * attribute_stride;
            const auto definition = memory::read<std::uint16_t>(attribute + attribute_definition_offset);
            for (std::size_t slot = 0; slot < attribute_indices.size(); ++slot)
            {
                if (definition == attribute_indices[slot] && !attributes[slot].present)
                {
                    attributes[slot].value = memory::read<float>(attribute + attribute_value_offset);
                    attributes[slot].present = true;
                    break;
                }
            }
        }

        return true;
    }

    bool gloves::restore_paint_attributes(std::uintptr_t item_view) const
    {
        const std::uintptr_t set_attribute = PATTERN(patterns::econ_item_view_set_attribute);
        const std::uintptr_t remove_attribute = PATTERN(patterns::econ_item_view_remove_attribute);
        if (!set_attribute || !remove_attribute)
            return false;

        for (std::size_t slot = 0; slot < attribute_indices.size(); ++slot)
        {
            if (m_original_attributes[slot].present)
                memory::call<void>(set_attribute, item_view, attribute_names[slot], m_original_attributes[slot].value);
            else
                memory::call<void>(remove_attribute, item_view, static_cast<int>(attribute_indices[slot]));
        }

        return true;
    }

    bool gloves::paint_attributes_match(std::uintptr_t item_view, const settings::changer::applied_skin& skin) const
    {
        std::array<attribute_state, 3> attributes{};
        if (!read_paint_attributes(item_view, attributes))
            return false;

        const std::array<float, 3> expected{ static_cast<float>(skin.paint_kit_id), static_cast<float>(skin.seed), skin.wear };
        for (std::size_t slot = 0; slot < expected.size(); ++slot)
        {
            if (!attributes[slot].present || attributes[slot].value != expected[slot])
                return false;
        }

        return true;
    }

    void gloves::apply(std::uintptr_t pawn, std::uintptr_t item_view, int team, const econ_item_system::item_def& def, const settings::changer::applied_skin& skin, std::uint32_t account_id)
    {
        const std::uintptr_t set_attribute = PATTERN(patterns::econ_item_view_set_attribute);
        if (!set_attribute)
            return;

        detail::write_field<std::uint16_t>(item_view, SCHEMA("C_EconItemView", "m_iItemDefinitionIndex"_hash), static_cast<std::uint16_t>(def.def_index));
        detail::write_field<std::uint64_t>(item_view, SCHEMA("C_EconItemView", "m_iItemID"_hash), detail::faux_item_id);
        detail::write_field<std::uint32_t>(item_view, SCHEMA("C_EconItemView", "m_iItemIDHigh"_hash), static_cast<std::uint32_t>(detail::faux_item_id >> 32));
        detail::write_field<std::uint32_t>(item_view, SCHEMA("C_EconItemView", "m_iItemIDLow"_hash), static_cast<std::uint32_t>(detail::faux_item_id & 0xFFFFFFFFull));
        detail::write_field<std::uint32_t>(item_view, SCHEMA("C_EconItemView", "m_iAccountID"_hash), account_id);
        detail::write_field<bool>(item_view, SCHEMA("C_EconItemView", "m_bRestoreCustomMaterialAfterPrecache"_hash), true);
        detail::write_field<bool>(item_view, SCHEMA("C_EconItemView", "m_bInitialized"_hash), true);
        detail::write_field<bool>(item_view, SCHEMA("C_EconItemView", "m_bDisallowSOC"_hash), true);

        memory::call<void>(set_attribute, item_view, attribute_names[0], static_cast<float>(skin.paint_kit_id));
        memory::call<void>(set_attribute, item_view, attribute_names[1], static_cast<float>(skin.seed));
        memory::call<void>(set_attribute, item_view, attribute_names[2], skin.wear);

        m_overridden = true;
        m_applied_def = def.def_index;
        m_applied_paint = skin.paint_kit_id;
        g_extra = { skin.seed, skin.wear, 0 };
        refresh(pawn, item_view, team);
    }

    void gloves::restore(std::uintptr_t pawn, std::uintptr_t item_view, int team)
    {
        if (m_original.captured)
        {
            restore_paint_attributes(item_view);

            detail::write_field<std::uint16_t>(item_view, SCHEMA("C_EconItemView", "m_iItemDefinitionIndex"_hash), m_original.def_index);
            detail::write_field<std::uint64_t>(item_view, SCHEMA("C_EconItemView", "m_iItemID"_hash), m_original.item_id);
            detail::write_field<std::uint32_t>(item_view, SCHEMA("C_EconItemView", "m_iItemIDHigh"_hash), m_original.id_high);
            detail::write_field<std::uint32_t>(item_view, SCHEMA("C_EconItemView", "m_iItemIDLow"_hash), m_original.id_low);
            detail::write_field<std::uint32_t>(item_view, SCHEMA("C_EconItemView", "m_iAccountID"_hash), m_original.account_id);
            detail::write_field<bool>(item_view, SCHEMA("C_EconItemView", "m_bRestoreCustomMaterialAfterPrecache"_hash), m_original.restore_custom_material);
            detail::write_field<bool>(item_view, SCHEMA("C_EconItemView", "m_bInitialized"_hash), m_original.initialized);
            detail::write_field<bool>(item_view, SCHEMA("C_EconItemView", "m_bDisallowSOC"_hash), m_original.disallow_soc);

            refresh(pawn, item_view, team);
        }

        m_original = {};
        m_original_attributes = {};
        m_applied_def = 0;
        m_applied_paint = 0;
        m_overridden = false;
        g_extra = {};
    }

    void gloves::refresh(std::uintptr_t pawn, std::uintptr_t item_view, int) const
    {
        invalidate_hud_icon(item_view);
        detail::write_field<bool>(pawn, SCHEMA("C_CSPlayerPawn", "m_bNeedToReApplyGloves"_hash), true);
        detail::post_data_update(pawn);
        if (const std::uintptr_t set_bodygroup = PATTERN(patterns::set_bodygroup))
            memory::call<void>(set_bodygroup, pawn, bodygroup_index, bodygroup_value);
    }

    void gloves::reset()
    {
        m_original = {};
        m_original_attributes = {};
        m_tracked_pawn = 0;
        m_applied_def = 0;
        m_applied_paint = 0;
        m_overridden = false;
        g_extra = {};
    }
}
