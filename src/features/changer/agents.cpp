#include "changer.h"
#include "changer_detail.h"
#include "../../core/addresses.h"
#include "../../core/math.h"
#include "../../core/patterns.h"
#include "../../core/schema.h"
#include "../../systems/game_reads.h"
#include "../../systems/local.h"
#include <string>

namespace
{
    constexpr math::vector3 agent_mins{ -16.f, -16.f, 0.f };
    constexpr math::vector3 agent_maxs{ 16.f, 16.f, 72.f };

    struct model_state_fields
    {
        std::uintptr_t state = 0;
        std::uint32_t name = 0;
        std::uint32_t handle = 0;
    };

    model_state_fields model_state_of(std::uintptr_t pawn)
    {
        const std::uintptr_t node = systems::reads::scene_node(pawn);
        const std::uint32_t state = SCHEMA("CSkeletonInstance", "m_modelState"_hash);
        const std::uint32_t name = SCHEMA("CModelState", "m_ModelName"_hash);
        const std::uint32_t handle = SCHEMA("CModelState", "m_hModel"_hash);
        if (!node || !state || !name || !handle)
            return {};
        return { node + state, name, handle };
    }

    std::string current_model(const model_state_fields& fields)
    {
        const std::uintptr_t text = systems::reads::pointer(fields.state + fields.name);
        return text ? memory::read_string(text) : std::string{};
    }

    void write_bounds(std::uintptr_t pawn)
    {
        const std::uint32_t collision = SCHEMA("C_BaseModelEntity", "m_Collision"_hash);
        const std::uint32_t mins = SCHEMA("CCollisionProperty", "m_vecMins"_hash);
        const std::uint32_t maxs = SCHEMA("CCollisionProperty", "m_vecMaxs"_hash);
        if (!collision || !mins || !maxs)
            return;
        features::changer::detail::write_field<math::vector3>(pawn + collision, mins, agent_mins);
        features::changer::detail::write_field<math::vector3>(pawn + collision, maxs, agent_maxs);
    }
}

namespace features::changer
{
    void agents::on_frame_stage_notify()
    {
        const auto local = systems::g_local.get();
        if (!local.pawn)
            return;

        if (m_tracked_pawn != local.pawn)
        {
            m_original_model.clear();
            m_overridden = false;
            m_applied_handle = 0;
            m_applied_def = 0;
            m_tracked_team = 0;
            m_tracked_pawn = local.pawn;
        }

        const int team = detail::read_field<std::uint8_t>(local.pawn, SCHEMA("C_BaseEntity", "m_iTeamNum"_hash));
        if (team != 2 && team != 3)
            return;

        const std::int16_t selected_def = detail::selected_agent(team);
        const auto* selected = selected_def ? g_econ_item_system.find_def(selected_def) : nullptr;
        if (selected && (selected->category != econ_item_system::item_category::agent || selected->model_player.empty()))
            selected = nullptr;

        const auto fields = model_state_of(local.pawn);
        const std::uintptr_t set_model = PATTERN(patterns::set_player_model);
        if (!fields.state || !set_model)
            return;

        if (!selected)
        {
            if (m_overridden && !m_original_model.empty())
            {
                memory::call<void>(set_model, local.pawn, m_original_model.c_str());
                detail::request_glove_refresh();
            }
            if (m_overridden)
            {
                m_applied_handle = 0;
                m_applied_def = 0;
                m_tracked_team = 0;
                m_overridden = false;
            }
            return;
        }

        const auto current_handle = systems::reads::value<std::uintptr_t>(fields.state + fields.handle, 0);
        if (m_overridden && m_applied_def == selected->def_index && m_tracked_team == team && m_applied_handle && current_handle == m_applied_handle)
            return;

        const std::string current = current_model(fields);
        if (m_overridden && m_applied_def == selected->def_index && m_tracked_team == team && !m_applied_handle && current == selected->model_player)
        {
            m_applied_handle = current_handle;
            return;
        }

        const auto* previous = m_applied_def ? g_econ_item_system.find_def(m_applied_def) : nullptr;
        if (!current.empty() && current != selected->model_player && (!previous || current != previous->model_player))
            m_original_model = current;

        memory::call<void>(set_model, local.pawn, selected->model_player.c_str());
        write_bounds(local.pawn);

        m_applied_handle = systems::reads::value<std::uintptr_t>(fields.state + fields.handle, 0);
        m_applied_def = selected->def_index;
        m_tracked_team = team;
        m_overridden = true;
        detail::request_glove_refresh();
    }

    void agents::restore()
    {
        if (m_overridden && !m_original_model.empty())
        {
            const auto local = systems::g_local.get();
            const std::uintptr_t set_model = PATTERN(patterns::set_player_model);
            if (local.pawn && local.pawn == m_tracked_pawn && set_model && model_state_of(local.pawn).state)
            {
                memory::call<void>(set_model, local.pawn, m_original_model.c_str());
                detail::request_glove_refresh();
            }
        }
        m_original_model.clear();
        m_applied_handle = 0;
        m_applied_def = 0;
        m_tracked_team = 0;
        m_overridden = false;
    }
}
