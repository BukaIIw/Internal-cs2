#include "entities.h"
#include "game_reads.h"
#include "local.h"
#include "tracing.h"
#include "../core/addresses.h"
#include "../core/cstypes.h"
#include "../core/patterns.h"
#include "../core/schema.h"
#include <Windows.h>
#include <atomic>
#include <cstring>
#include <string>

namespace
{
    constexpr int max_index = 0x7FFF;
    constexpr int chunk_shift = 9;
    constexpr int chunk_mask = 0x1FF;
    constexpr int chunk_count = 64;
    constexpr int chunk_capacity = 512;
    constexpr std::uintptr_t identity_stride = 0x70;
    constexpr std::uintptr_t identity_entity = 0x00;
    constexpr std::uintptr_t identity_handle = 0x10;
    constexpr std::uintptr_t identity_designer = 0x20;
    constexpr std::size_t identity_size = 0x28;
    constexpr std::uintptr_t system_identity_chunks = 0x10;
    constexpr std::uint32_t entity_identity_fallback = 0x10;
    constexpr std::uintptr_t bone_array_disp = 3;
    constexpr std::uint32_t bone_array_fallback = 0x80;
    constexpr std::int32_t bone_array_limit = 0x4000;
    constexpr int head_bone = 6;
    constexpr float bone_range = 200.f;
    constexpr float hull_limit = 512.f;
    constexpr int max_health = 100000;
    constexpr int max_armor = 1000;
    constexpr std::size_t name_length = 31;
    const math::vector3 default_mins{ -16.f, -16.f, 0.f };
    const math::vector3 default_maxs{ 16.f, 16.f, 72.f };

    std::atomic<std::uintptr_t> g_validated_system{ 0 };

    std::uintptr_t identity_in(std::uintptr_t table, int index)
    {
        if (!table || index < 0 || index > max_index)
            return 0;
        const std::uintptr_t chunk = systems::reads::pointer(table + sizeof(std::uintptr_t) * static_cast<std::uintptr_t>(index >> chunk_shift));
        if (!chunk)
            return 0;
        const std::uintptr_t identity = chunk + static_cast<std::uintptr_t>(index & chunk_mask) * identity_stride;
        return systems::reads::readable(identity, identity_size) ? identity : 0;
    }

    bool validate_system(std::uintptr_t system)
    {
        const std::uintptr_t table = system + system_identity_chunks;
        int found = 0;
        for (int i = 1; i <= cstypes::max_players; ++i)
        {
            const std::uintptr_t identity = identity_in(table, i);
            if (!identity || !memory::read<std::uintptr_t>(identity + identity_entity))
                continue;
            if ((memory::read<std::uint32_t>(identity + identity_handle) & max_index) != static_cast<std::uint32_t>(i))
                return false;
            ++found;
        }
        return found > 0;
    }

    std::uintptr_t resolve_system(std::uintptr_t& table)
    {
        table = 0;
        if (const std::uintptr_t list = PATTERN(patterns::entity_list_legacy))
        {
            table = systems::reads::pointer(list);
            return table;
        }
        const std::uintptr_t system = systems::reads::pointer(PATTERN(patterns::game_entity_system));
        if (!system)
            return 0;
        if (g_validated_system.load(std::memory_order_acquire) != system)
        {
            if (!validate_system(system))
                return 0;
            g_validated_system.store(system, std::memory_order_release);
        }
        table = system + system_identity_chunks;
        return system;
    }

    std::uintptr_t identity_table()
    {
        std::uintptr_t table = 0;
        resolve_system(table);
        return table;
    }

    const char* designer_of(std::uintptr_t identity)
    {
        if (!identity)
            return nullptr;
        const std::uintptr_t name = systems::reads::pointer(identity + identity_designer);
        return name ? reinterpret_cast<const char*>(name) : nullptr;
    }

    std::uintptr_t identity_of(std::uintptr_t entity)
    {
        if (!systems::reads::readable(entity))
            return 0;
        const std::uint32_t schema_offset = SCHEMA("CEntityInstance", "m_pEntity"_hash);
        const std::uint32_t offset = schema_offset ? schema_offset : entity_identity_fallback;
        const std::uintptr_t identity = systems::reads::pointer(entity + offset);
        if (!identity || !systems::reads::readable(identity, identity_size))
            return 0;
        return memory::read<std::uintptr_t>(identity + identity_entity) == entity ? identity : 0;
    }

    std::uint32_t bone_array_offset()
    {
        const std::uintptr_t match = PATTERN(patterns::skeleton_bone_array);
        if (match && memory::is_readable(match + bone_array_disp, sizeof(std::int32_t)))
        {
            const auto disp = memory::read<std::int32_t>(match + bone_array_disp);
            if (disp > 0 && disp < bone_array_limit)
                return static_cast<std::uint32_t>(disp);
        }
        const std::uint32_t model_state = SCHEMA("CSkeletonInstance", "m_modelState"_hash);
        return model_state ? model_state + bone_array_fallback : 0;
    }

    bool head_position(std::uintptr_t pawn, std::uint32_t array_offset, const math::vector3& origin, math::vector3& out)
    {
        const std::uintptr_t node = systems::reads::scene_node(pawn);
        if (!node || !array_offset)
            return false;
        const std::uintptr_t bones = systems::reads::pointer(node + array_offset);
        if (!bones)
            return false;
        const std::uintptr_t at = bones + static_cast<std::uintptr_t>(head_bone) * sizeof(math::bone);
        if (!systems::reads::readable(at, sizeof(math::vector3)))
            return false;
        const auto position = memory::read<math::vector3>(at);
        if (!systems::reads::sane(position, systems::reads::world_limit) || position.distance(origin) > bone_range)
            return false;
        out = position;
        return true;
    }

    void copy_name(char* out, std::uintptr_t controller)
    {
        std::string name;
        if (const std::uintptr_t sanitized = systems::reads::field_pointer(controller, SCHEMA("CCSPlayerController", "m_sSanitizedPlayerName"_hash)))
            name = memory::read_string(sanitized, name_length);
        if (name.empty())
        {
            if (const std::uint32_t inline_name = SCHEMA("CBasePlayerController", "m_iszPlayerName"_hash))
                name = memory::read_string(controller + inline_name, name_length);
        }
        const std::size_t length = name.size() < name_length ? name.size() : name_length;
        std::memcpy(out, name.data(), length);
        out[length] = '\0';
    }

    struct update_context
    {
        std::uintptr_t local_pawn = 0;
        int local_index = 0;
        int local_team = 0;
        bool local_alive = false;
        math::vector3 local_eye{};
        bool trace = false;
        std::uint32_t bones = 0;
    };

    void fill(systems::entities::player& out, int index, const update_context& ctx)
    {
        using namespace systems;
        const std::uintptr_t identity = g_entities.identity(index);
        const std::uintptr_t controller = identity ? reads::pointer(identity + identity_entity) : 0;
        if (!controller || !reads::designer_is(designer_of(identity), "cs_player_controller"))
            return;
        out.index = index;
        out.controller = controller;
        out.pawn_handle = reads::field<std::uint32_t>(controller, SCHEMA("CCSPlayerController", "m_hPlayerPawn"_hash), reads::invalid_handle);
        out.pawn = g_entities.lookup(out.pawn_handle);
        if (!out.pawn || out.pawn == ctx.local_pawn || index == ctx.local_index)
            return;
        math::vector3 origin{};
        if (!reads::abs_origin(out.pawn, origin))
            return;
        copy_name(out.name, controller);
        out.origin = origin;
        out.velocity = reads::abs_velocity(out.pawn);
        out.team = reads::field<std::uint8_t>(out.pawn, SCHEMA("C_BaseEntity", "m_iTeamNum"_hash));
        const int health = reads::field<int>(out.pawn, SCHEMA("C_BaseEntity", "m_iHealth"_hash));
        out.health = health > 0 && health < max_health ? health : 0;
        const auto life_state = reads::field<std::uint8_t>(out.pawn, SCHEMA("C_BaseEntity", "m_lifeState"_hash));
        out.alive = life_state == 0 && out.health > 0;
        const std::uintptr_t node = reads::scene_node(out.pawn);
        out.dormant = !node || reads::field<std::uint8_t>(node, SCHEMA("CGameSceneNode", "m_bDormant"_hash), 1) != 0;
        const int armor = reads::field<int>(out.pawn, SCHEMA("C_CSPlayerPawn", "m_ArmorValue"_hash));
        out.armor = armor > 0 && armor < max_armor ? armor : 0;
        const std::uintptr_t item_services = reads::field_pointer(out.pawn, SCHEMA("C_BasePlayerPawn", "m_pItemServices"_hash));
        out.helmet = reads::field<std::uint8_t>(item_services, SCHEMA("CCSPlayer_ItemServices", "m_bHasHelmet"_hash)) != 0;
        out.mins = default_mins;
        out.maxs = default_maxs;
        const std::uint32_t collision = SCHEMA("C_BaseModelEntity", "m_Collision"_hash);
        if (collision)
        {
            const auto mins = reads::field<math::vector3>(out.pawn + collision, SCHEMA("CCollisionProperty", "m_vecMins"_hash), default_mins);
            const auto maxs = reads::field<math::vector3>(out.pawn + collision, SCHEMA("CCollisionProperty", "m_vecMaxs"_hash), default_maxs);
            if (reads::sane(mins, hull_limit) && reads::sane(maxs, hull_limit) && maxs.x > mins.x && maxs.y > mins.y && maxs.z > mins.z)
            {
                out.mins = mins;
                out.maxs = maxs;
            }
        }
        out.weapon_def = reads::item_definition(reads::active_weapon(out.pawn));
        out.enemy = out.team != ctx.local_team;
        if (ctx.trace && ctx.local_alive && out.alive && !out.dormant)
        {
            math::vector3 head{};
            if (!head_position(out.pawn, ctx.bones, origin, head))
                head = reads::eye_position(out.pawn, origin);
            out.visible = g_tracing.is_visible(ctx.local_pawn, out.pawn, ctx.local_eye, head);
        }
        out.valid = true;
    }

    bool fill_guarded(systems::entities::player& out, int index, const update_context& ctx)
    {
        __try
        {
            fill(out, index, ctx);
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
    std::uintptr_t entity_system::system() const
    {
        std::uintptr_t table = 0;
        return resolve_system(table);
    }

    std::uintptr_t entity_system::identity(int index) const
    {
        return identity_in(identity_table(), index);
    }

    std::uintptr_t entity_system::get(int index) const
    {
        const std::uintptr_t id = identity(index);
        return id ? reads::pointer(id + identity_entity) : 0;
    }

    std::uintptr_t entity_system::lookup(std::uint32_t handle) const
    {
        if (handle == reads::invalid_handle)
            return 0;
        const std::uintptr_t id = identity(static_cast<int>(handle & max_index));
        if (!id || memory::read<std::uint32_t>(id + identity_handle) != handle)
            return 0;
        return reads::pointer(id + identity_entity);
    }

    std::uint32_t entity_system::handle_of(int index) const
    {
        const std::uintptr_t id = identity(index);
        return id ? memory::read<std::uint32_t>(id + identity_handle) : reads::invalid_handle;
    }

    const char* entity_system::get_designer_name(std::uintptr_t entity) const
    {
        return designer_of(identity_of(entity));
    }

    const char* entity_system::get_designer_name(int index) const
    {
        return designer_of(identity(index));
    }

    const char* entity_system::get_schema_name(std::uintptr_t entity) const
    {
        return designer_of(identity_of(entity));
    }

    int entity_system::highest_index() const
    {
        const std::uintptr_t table = identity_table();
        if (!table)
            return 0;
        for (int chunk = chunk_count - 1; chunk >= 0; --chunk)
        {
            if (!reads::pointer(table + sizeof(std::uintptr_t) * static_cast<std::uintptr_t>(chunk)))
                continue;
            for (int slot = chunk_capacity - 1; slot >= 0; --slot)
            {
                const int index = chunk * chunk_capacity + slot;
                const std::uintptr_t id = identity_in(table, index);
                if (!id || !memory::read<std::uintptr_t>(id + identity_entity))
                    continue;
                if ((memory::read<std::uint32_t>(id + identity_handle) & max_index) == static_cast<std::uint32_t>(index))
                    return index;
            }
        }
        return 0;
    }

    void entity_system::update()
    {
        std::array<entities::player, 65> next{};
        const auto local = g_local.get();
        update_context ctx{};
        ctx.local_pawn = local.pawn;
        ctx.local_index = local.index;
        ctx.local_team = local.team;
        ctx.local_alive = local.is_alive;
        ctx.local_eye = local.eye;
        ctx.trace = local.is_alive && local.pawn && g_tracing.ready();
        ctx.bones = ctx.trace ? bone_array_offset() : 0;
        if (identity_table())
        {
            for (int i = 1; i <= cstypes::max_players && i < static_cast<int>(next.size()); ++i)
            {
                if (!fill_guarded(next[i], i, ctx))
                    next[i] = {};
            }
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        m_players = next;
    }

    std::array<entities::player, 65> entity_system::players() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_players;
    }

    entities::player entity_system::player_by_pawn(std::uintptr_t pawn) const
    {
        if (!pawn)
            return {};
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto& p : m_players)
        {
            if (p.valid && p.pawn == pawn)
                return p;
        }
        return {};
    }
}
