#include "local.h"
#include "entities.h"
#include "game_reads.h"
#include "../core/cstypes.h"
#include "../core/patterns.h"
#include "../core/schema.h"
#include <Windows.h>

namespace
{
    constexpr std::uintptr_t vdata_after_subclass = 0x8;
    constexpr int max_health = 100000;

    bool is_controller(std::uintptr_t entity)
    {
        return entity && systems::reads::designer_is(systems::g_entities.get_designer_name(entity), "cs_player_controller");
    }

    bool is_local(std::uintptr_t controller)
    {
        const std::uint32_t offset = SCHEMA("CBasePlayerController", "m_bIsLocalPlayerController"_hash);
        return !offset || systems::reads::field<std::uint8_t>(controller, offset) != 0;
    }

    int index_of(std::uintptr_t controller)
    {
        for (int i = 1; i <= cstypes::max_players; ++i)
        {
            if (systems::g_entities.get(i) == controller)
                return i;
        }
        return 0;
    }

    std::uintptr_t controller_from_pattern(int& index)
    {
        index = 0;
        const std::uintptr_t controller = systems::reads::pointer(PATTERN(patterns::local_player_controller));
        if (!is_controller(controller) || !is_local(controller))
            return 0;
        index = index_of(controller);
        return index > 0 ? controller : 0;
    }

    std::uintptr_t controller_from_scan(int& index)
    {
        index = 0;
        const std::uint32_t offset = SCHEMA("CBasePlayerController", "m_bIsLocalPlayerController"_hash);
        if (!offset)
            return 0;
        for (int i = 1; i <= cstypes::max_players; ++i)
        {
            const std::uintptr_t entity = systems::g_entities.get(i);
            if (!entity || !systems::reads::designer_is(systems::g_entities.get_designer_name(i), "cs_player_controller"))
                continue;
            if (systems::reads::field<std::uint8_t>(entity, offset) != 0)
            {
                index = i;
                return entity;
            }
        }
        return 0;
    }

    std::uintptr_t weapon_vdata(std::uintptr_t weapon)
    {
        const std::uint32_t subclass = SCHEMA("C_BaseEntity", "m_nSubclassID"_hash);
        if (!weapon || !subclass)
            return 0;
        return systems::reads::pointer(weapon + subclass + vdata_after_subclass);
    }

    void collect(systems::local_player::data& out)
    {
        using namespace systems;
        int index = 0;
        std::uintptr_t controller = controller_from_pattern(index);
        if (!controller)
            controller = controller_from_scan(index);
        if (!controller)
            return;
        out.controller = controller;
        out.index = index;
        out.tick_base = reads::field<int>(controller, SCHEMA("CBasePlayerController", "m_nTickBase"_hash));
        out.pawn_handle = reads::field<std::uint32_t>(controller, SCHEMA("CCSPlayerController", "m_hPlayerPawn"_hash), reads::invalid_handle);
        if (reads::field<std::uint8_t>(controller, SCHEMA("CCSPlayerController", "m_bControllingBot"_hash)) != 0)
        {
            const std::uint32_t bot = reads::field<std::uint32_t>(controller, SCHEMA("CBasePlayerController", "m_hPawn"_hash), reads::invalid_handle);
            if (g_entities.lookup(bot))
                out.pawn_handle = bot;
        }
        out.pawn = g_entities.lookup(out.pawn_handle);
        const std::uint32_t team_offset = SCHEMA("C_BaseEntity", "m_iTeamNum"_hash);
        out.team = reads::field<std::uint8_t>(out.pawn ? out.pawn : controller, team_offset);
        if (!out.pawn)
            return;
        const int health = reads::field<int>(out.pawn, SCHEMA("C_BaseEntity", "m_iHealth"_hash));
        out.health = health > 0 && health < max_health ? health : 0;
        const auto life_state = reads::field<std::uint8_t>(out.pawn, SCHEMA("C_BaseEntity", "m_lifeState"_hash));
        out.move_type = reads::field<std::uint8_t>(out.pawn, SCHEMA("C_BaseEntity", "m_MoveType"_hash));
        out.flags = reads::field<std::uint32_t>(out.pawn, SCHEMA("C_BaseEntity", "m_fFlags"_hash));
        out.velocity = reads::abs_velocity(out.pawn);
        math::vector3 origin{};
        const bool has_origin = reads::abs_origin(out.pawn, origin);
        if (has_origin)
        {
            out.origin = origin;
            out.eye = reads::eye_position(out.pawn, origin);
        }
        out.is_alive = has_origin && life_state == 0 && out.health > 0;
        out.weapon = reads::active_weapon(out.pawn, &out.weapon_handle);
        if (!out.weapon)
            return;
        out.weapon_def = reads::item_definition(out.weapon);
        out.weapon_vdata = weapon_vdata(out.weapon);
        const int type = reads::field<int>(out.weapon_vdata, SCHEMA("CCSWeaponBaseVData", "m_WeaponType"_hash), -1);
        out.weapon_type = type >= cstypes::weapon_type::knife && type <= cstypes::weapon_type::equipment ? type : -1;
    }

    bool collect_guarded(systems::local_player::data& out)
    {
        __try
        {
            collect(out);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }
}

namespace systems
{
    local_player::data local_system::get() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_data;
    }

    void local_system::update()
    {
        local_player::data next{};
        if (!collect_guarded(next))
            next = {};
        std::lock_guard<std::mutex> lock(m_mutex);
        m_data = next;
    }

    bool local_system::is_in_cinematic() const
    {
        return false;
    }

    int local_system::index() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_data.index;
    }
}
