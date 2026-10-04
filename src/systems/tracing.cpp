#include "tracing.h"
#include "entities.h"
#include "game_reads.h"
#include "../core/addresses.h"
#include "../core/convar.h"
#include "../core/cstypes.h"
#include "../core/log.h"
#include "../core/memory.h"
#include "../core/patterns.h"
#include "../core/schema.h"
#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>

namespace
{
    constexpr std::size_t filter_vtable = 0x00;
    constexpr std::size_t filter_mask = 0x08;
    constexpr std::size_t filter_skip = 0x20;
    constexpr std::size_t filter_objects = 0x34;
    constexpr std::size_t filter_collision_group = 0x36;
    constexpr std::size_t filter_channel = 0x37;
    constexpr std::size_t filter_flags = 0x38;
    constexpr std::size_t filter_bits = 0x39;
    constexpr std::uint16_t filter_objects_value = 0xFFFF;
    constexpr std::uint8_t filter_channel_value = 0x0F;
    constexpr std::uint8_t filter_flags_value = 0x0B;
    constexpr std::uint8_t filter_bits_value = 0x49;
    constexpr std::uint8_t line_collision_group = 0;
    constexpr int skip_slots = 4;

    constexpr std::size_t ray_size = 0x40;
    constexpr std::size_t ray_mins = 0x00;
    constexpr std::size_t ray_maxs = 0x0C;
    constexpr std::size_t ray_type = 0x28;
    constexpr std::uint8_t ray_type_hull = 2;

    constexpr std::size_t trace_size = 0x200;
    constexpr std::size_t trace_surface = 0x00;
    constexpr std::size_t trace_entity = 0x08;
    constexpr std::size_t trace_hitbox = 0x10;
    constexpr std::size_t trace_end = 0x84;
    constexpr std::size_t trace_normal = 0x90;
    constexpr std::size_t trace_position = 0x9C;
    constexpr std::size_t trace_fraction = 0xAC;
    constexpr std::size_t trace_start_solid = 0xBA;
    constexpr std::size_t hitbox_group = 0x38;
    constexpr std::size_t hitbox_id = 0x40;
    constexpr std::size_t surface_penetration = 0x08;

    constexpr std::size_t bullet_data_size = 0x3000;
    constexpr std::size_t bullet_array_pointer = 0x08;
    constexpr std::size_t bullet_array = 0x18;
    constexpr std::size_t bullet_array_stride = 0x38;
    constexpr int bullet_array_count = 128;
    constexpr std::size_t bullet_modulate_count = 0x1C20;
    constexpr std::size_t bullet_entries = 0x1C28;
    constexpr std::size_t bullet_start = 0x1CF8;
    constexpr std::size_t entry_stride = 0x18;
    constexpr std::size_t entry_previous = 0x00;
    constexpr std::size_t entry_current = 0x04;
    constexpr std::size_t entry_handle = 0x10;
    constexpr std::uint16_t entry_handle_mask = 0x7FFF;
    constexpr std::uint8_t bullet_filter_layer = 3;
    constexpr std::uint16_t bullet_filter_flags = 15;
    constexpr int bullet_penetration_count = 4;

    constexpr std::uintptr_t identity_fallback = 0x10;
    constexpr std::uintptr_t identity_entity = 0x00;
    constexpr std::uintptr_t identity_handle = 0x10;
    constexpr std::uintptr_t vdata_after_subclass = 0x8;

    constexpr float min_length = 1.f;
    constexpr float world_tolerance = 2.f;
    constexpr float shot_extension = 16.f;
    constexpr float end_tolerance = 8.f;
    constexpr float start_tolerance = 1.f;
    constexpr float hull_limit = 512.f;
    constexpr int max_players = 64;
    constexpr int max_hitgroup = 10;
    constexpr int max_hitbox = 64;
    constexpr int max_health = 100000;
    constexpr int max_armor = 1000;

    constexpr float default_range = 8192.f;
    constexpr float default_headshot = 4.f;
    constexpr float default_armor_ratio = 1.f;
    constexpr float default_penetration = 1.f;
    constexpr float range_distance_unit = 500.f;
    constexpr float stomach_scale = 1.25f;
    constexpr float leg_scale = 0.75f;
    constexpr float damage_lost_percent = 0.16f;
    constexpr float max_wall_thickness = 90.f;
    constexpr float min_damage = 1.f;

    std::atomic<std::uintptr_t> g_filter_vtable{ 0 };
    std::atomic<bool> g_filter_vtable_attempted{ false };
    std::atomic<bool> g_bullets_broken{ false };

    struct weapon_params
    {
        float damage = 0.f;
        float penetration = default_penetration;
        float range = default_range;
        float range_modifier = 1.f;
        float armor_ratio = default_armor_ratio;
        float headshot = default_headshot;
    };

    struct shape_call
    {
        std::uintptr_t function = 0;
        std::uintptr_t manager = 0;
        std::uint8_t* ray = nullptr;
        math::vector3 start{};
        math::vector3 end{};
        std::uint8_t* filter = nullptr;
        std::uint8_t* trace = nullptr;
    };

    struct bullet_call
    {
        std::uintptr_t init_filter = 0;
        std::uintptr_t create_trace = 0;
        std::uintptr_t skip = 0;
        std::uint64_t mask = 0;
        math::vector3 start{};
        math::vector3 delta{};
        std::uint8_t* data = nullptr;
        std::uint8_t* filter = nullptr;
    };

    std::uintptr_t filter_vtable_address(bool retry = false)
    {
        std::uintptr_t vtable = g_filter_vtable.load(std::memory_order_acquire);
        if (vtable)
            return vtable;
        if (g_filter_vtable_attempted.exchange(true) && !retry)
            return 0;
        vtable = memory::find_vtable("client.dll", ".?AVCTraceFilter@@");
        if (vtable)
            g_filter_vtable.store(vtable, std::memory_order_release);
        return vtable;
    }

    std::uintptr_t trace_manager()
    {
        const std::uintptr_t manager = addresses::globals::trace_manager();
        return systems::reads::readable(manager) ? manager : 0;
    }

    std::uint32_t entity_handle(std::uintptr_t entity)
    {
        if (!systems::reads::readable(entity))
            return systems::reads::invalid_handle;
        const std::uint32_t schema_offset = SCHEMA("CEntityInstance", "m_pEntity"_hash);
        const std::uintptr_t identity = systems::reads::pointer(entity + (schema_offset ? schema_offset : identity_fallback));
        if (!identity || !systems::reads::readable(identity, identity_handle + sizeof(std::uint32_t)))
            return systems::reads::invalid_handle;
        if (memory::read<std::uintptr_t>(identity + identity_entity) != entity)
            return systems::reads::invalid_handle;
        return memory::read<std::uint32_t>(identity + identity_handle);
    }

    void fill_filter(std::uint8_t* data, std::uintptr_t vtable, std::uint32_t skip, std::uint64_t mask, std::uint8_t group)
    {
        std::memset(data, 0, sizeof(systems::tracing::filter::data));
        std::memcpy(data + filter_vtable, &vtable, sizeof(vtable));
        std::memcpy(data + filter_mask, &mask, sizeof(mask));
        for (int i = 0; i < skip_slots; ++i)
        {
            const std::uint32_t value = i == 0 ? skip : systems::reads::invalid_handle;
            std::memcpy(data + filter_skip + i * sizeof(std::uint32_t), &value, sizeof(value));
        }
        std::memcpy(data + filter_objects, &filter_objects_value, sizeof(filter_objects_value));
        data[filter_collision_group] = group;
        data[filter_channel] = filter_channel_value;
        data[filter_flags] = filter_flags_value;
        data[filter_bits] = filter_bits_value;
    }

    template <typename T>
    T buffer_read(const std::uint8_t* buffer, std::size_t offset)
    {
        T value{};
        std::memcpy(&value, buffer + offset, sizeof(T));
        return value;
    }

    template <typename T>
    void buffer_write(std::uint8_t* buffer, std::size_t offset, const T& value)
    {
        std::memcpy(buffer + offset, &value, sizeof(T));
    }

    bool run_shape(shape_call& call)
    {
        __try
        {
            memory::call<bool>(call.function, call.manager, call.ray, &call.start, &call.end, call.filter, call.trace);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool run_bullet(bullet_call& call)
    {
        __try
        {
            memory::call<std::uintptr_t>(call.init_filter, call.filter, call.skip, call.mask, bullet_filter_layer, bullet_filter_flags);
            memory::call<void>(call.create_trace, call.data, &call.start, &call.delta, call.filter, bullet_penetration_count, true);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool run_bullet_info(std::uintptr_t function, std::uint8_t* data, std::uint8_t* trace, std::uintptr_t element)
    {
        __try
        {
            memory::call<void>(function, data, trace, 0.f, element);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    systems::tracing::result parse_trace(const std::uint8_t* trace, const math::vector3& start, const math::vector3& end)
    {
        systems::tracing::result out{};
        const float fraction = buffer_read<float>(trace, trace_fraction);
        if (!std::isfinite(fraction))
            return out;
        out.ok = true;
        out.fraction = std::clamp(fraction, 0.f, 1.f);
        out.start = start;
        const std::uintptr_t entity = buffer_read<std::uintptr_t>(trace, trace_entity);
        out.entity = systems::reads::readable(entity) ? entity : 0;
        const auto hit_end = buffer_read<math::vector3>(trace, trace_end);
        out.end = systems::reads::sane(hit_end, systems::reads::world_limit) ? hit_end : start + (end - start) * out.fraction;
        const auto normal = buffer_read<math::vector3>(trace, trace_normal);
        out.normal = normal.is_valid() && normal.length_sqr() <= 1.1f ? normal : math::vector3{};
        const std::uintptr_t hitbox = buffer_read<std::uintptr_t>(trace, trace_hitbox);
        if (hitbox && systems::reads::readable(hitbox + hitbox_group, hitbox_id + sizeof(int) - hitbox_group))
        {
            const int group = memory::read<int>(hitbox + hitbox_group);
            const int id = memory::read<int>(hitbox + hitbox_id);
            out.hitgroup = group >= 0 && group <= max_hitgroup ? group : -1;
            out.hitbox = id >= 0 && id < max_hitbox ? id : -1;
        }
        out.start_solid = trace[trace_start_solid] != 0;
        return out;
    }

    systems::tracing::result shape_trace(const math::vector3& start, const math::vector3& end, const std::uint8_t* filter_data, const systems::tracing::hull* bounds)
    {
        systems::tracing::result out{};
        if (!start.is_valid() || !end.is_valid())
            return out;
        const std::uintptr_t function = PATTERN(patterns::trace_shape);
        const std::uintptr_t manager = trace_manager();
        if (!function || !manager)
            return out;
        alignas(16) std::uint8_t ray[ray_size]{};
        alignas(16) std::uint8_t trace[trace_size]{};
        alignas(16) std::uint8_t filter[sizeof(systems::tracing::filter::data)]{};
        std::memcpy(filter, filter_data, sizeof(filter));
        if (bounds)
        {
            buffer_write(ray, ray_mins, bounds->mins);
            buffer_write(ray, ray_maxs, bounds->maxs);
            ray[ray_type] = ray_type_hull;
        }
        shape_call call{ function, manager, ray, start, end, filter, trace };
        if (!run_shape(call))
            return out;
        return parse_trace(trace, start, end);
    }

    systems::tracing::result line_trace(const math::vector3& start, const math::vector3& end, std::uint32_t skip, std::uint64_t mask)
    {
        const std::uintptr_t vtable = filter_vtable_address();
        if (!vtable)
            return {};
        alignas(16) std::uint8_t filter[sizeof(systems::tracing::filter::data)]{};
        fill_filter(filter, vtable, skip, mask, line_collision_group);
        return shape_trace(start, end, filter, nullptr);
    }

    bool is_live_player(std::uintptr_t entity)
    {
        if (!entity)
            return false;
        const std::uint32_t pawn_offset = SCHEMA("CCSPlayerController", "m_hPlayerPawn"_hash);
        const std::uint32_t health_offset = SCHEMA("C_BaseEntity", "m_iHealth"_hash);
        if (!pawn_offset || !health_offset)
            return false;
        for (int i = 1; i <= max_players; ++i)
        {
            const std::uintptr_t controller = systems::g_entities.get(i);
            if (!controller || !systems::reads::designer_is(systems::g_entities.get_designer_name(i), "cs_player_controller"))
                continue;
            const auto handle = systems::reads::field<std::uint32_t>(controller, pawn_offset, systems::reads::invalid_handle);
            if (systems::g_entities.lookup(handle) != entity)
                continue;
            const int health = systems::reads::field<int>(entity, health_offset);
            return health > 0 && health < max_health;
        }
        return false;
    }

    float sane_float(float value, float low, float high, float fallback)
    {
        return std::isfinite(value) && value >= low && value <= high ? value : fallback;
    }

    bool read_weapon(std::uintptr_t pawn, weapon_params& out)
    {
        const std::uintptr_t weapon = systems::reads::active_weapon(pawn);
        const std::uint32_t subclass = SCHEMA("C_BaseEntity", "m_nSubclassID"_hash);
        if (!weapon || !subclass)
            return false;
        const std::uintptr_t vdata = systems::reads::pointer(weapon + subclass + vdata_after_subclass);
        if (!vdata)
            return false;
        const int damage = systems::reads::field<int>(vdata, SCHEMA("CCSWeaponBaseVData", "m_nDamage"_hash));
        if (damage < 1 || damage > 1000)
            return false;
        out.damage = static_cast<float>(damage);
        out.penetration = sane_float(systems::reads::field<float>(vdata, SCHEMA("CCSWeaponBaseVData", "m_flPenetration"_hash), default_penetration), 0.f, 10.f, default_penetration);
        out.range = sane_float(systems::reads::field<float>(vdata, SCHEMA("CCSWeaponBaseVData", "m_flRange"_hash), default_range), 1.f, systems::reads::world_limit, default_range);
        out.range_modifier = sane_float(systems::reads::field<float>(vdata, SCHEMA("CCSWeaponBaseVData", "m_flRangeModifier"_hash), 1.f), 0.01f, 1.f, 1.f);
        out.armor_ratio = sane_float(systems::reads::field<float>(vdata, SCHEMA("CCSWeaponBaseVData", "m_flArmorRatio"_hash), default_armor_ratio), 0.f, 2.f, default_armor_ratio);
        out.headshot = sane_float(systems::reads::field<float>(vdata, SCHEMA("CCSWeaponBaseVData", "m_flHeadshotMultiplier"_hash), default_headshot), 0.f, 10.f, default_headshot);
        return true;
    }

    float convar_scale(const convars::convar* cvar)
    {
        const float value = cvar->get<float>();
        return std::isfinite(value) && value > 0.f && value <= 10.f ? value : 1.f;
    }

    float scale_damage(std::uintptr_t target, float damage, int hitgroup, const weapon_params& weapon)
    {
        const int team = systems::reads::field<std::uint8_t>(target, SCHEMA("C_BaseEntity", "m_iTeamNum"_hash));
        const bool ct = team == cstypes::team::ct;
        const float head_scale = convar_scale(ct ? CONVAR("mp_damage_scale_ct_head") : CONVAR("mp_damage_scale_t_head"));
        const float body_scale = convar_scale(ct ? CONVAR("mp_damage_scale_ct_body") : CONVAR("mp_damage_scale_t_body"));
        const bool head = hitgroup == cstypes::hitgroup::head;
        const bool leg = hitgroup == cstypes::hitgroup::left_leg || hitgroup == cstypes::hitgroup::right_leg;
        if (head)
            damage *= weapon.headshot * head_scale;
        else
            damage *= body_scale;
        if (hitgroup == cstypes::hitgroup::stomach)
            damage *= stomach_scale;
        else if (leg)
            damage *= leg_scale;

        int armor = systems::reads::field<int>(target, SCHEMA("C_CSPlayerPawn", "m_ArmorValue"_hash));
        if (armor < 0 || armor > max_armor)
            armor = 0;
        const std::uintptr_t services = systems::reads::field_pointer(target, SCHEMA("C_BasePlayerPawn", "m_pItemServices"_hash));
        const bool helmet = systems::reads::field<std::uint8_t>(services, SCHEMA("CCSPlayer_ItemServices", "m_bHasHelmet"_hash)) != 0;
        const bool armored = armor > 0 && (head ? helmet : !leg);
        if (armored)
        {
            float reduced = damage * weapon.armor_ratio * 0.5f;
            if ((damage - reduced) * 0.5f > static_cast<float>(armor))
                reduced = damage - static_cast<float>(armor) / 0.5f;
            damage = reduced;
        }
        return std::isfinite(damage) && damage > 0.f ? damage : 0.f;
    }

    float range_damage(float damage, float distance, const weapon_params& weapon)
    {
        if (!std::isfinite(distance) || distance > weapon.range)
            return 0.f;
        return damage * std::pow(weapon.range_modifier, distance / range_distance_unit);
    }

    float penetration_loss(float damage, float thickness, float surface_modifier, const weapon_params& weapon)
    {
        const float modifier = 1.f / surface_modifier;
        const float chunk = damage * damage_lost_percent;
        const float weapon_loss = chunk + std::max(0.f, (3.f / weapon.penetration) * 1.25f) * (modifier * 3.f);
        const float object_loss = modifier * thickness * thickness / 24.f;
        return std::max(0.f, weapon_loss + object_loss);
    }

    void mark_bullets_broken(const char* reason)
    {
        if (!g_bullets_broken.exchange(true))
            logs::Add(logs::Warning, "tracing: autowall disabled (%s)", reason);
    }

    systems::tracing::bullet_result engine_bullet(std::uintptr_t local_pawn, std::uintptr_t target, const math::vector3& start, const math::vector3& direction, const weapon_params& weapon)
    {
        systems::tracing::bullet_result out{};
        alignas(16) thread_local std::uint8_t data[bullet_data_size];
        alignas(16) std::uint8_t filter[sizeof(systems::tracing::filter::data)]{};
        std::memset(data, 0, sizeof(data));
        buffer_write(data, bullet_array_pointer, reinterpret_cast<std::uintptr_t>(data + bullet_array));

        bullet_call call{};
        call.init_filter = PATTERN(patterns::trace_filter_init);
        call.create_trace = PATTERN(patterns::trace_bullet);
        call.skip = local_pawn;
        call.mask = cstypes::masks::wallbang;
        call.start = start;
        call.delta = direction * weapon.range;
        call.data = data;
        call.filter = filter;
        const std::uintptr_t info = PATTERN(patterns::trace_bullet_update);
        if (!call.init_filter || !call.create_trace || !info)
            return out;
        if (!run_bullet(call))
        {
            mark_bullets_broken("fault in create trace");
            return out;
        }

        const int count = buffer_read<int>(data, bullet_modulate_count);
        const std::uintptr_t entries = buffer_read<std::uintptr_t>(data, bullet_entries);
        const auto traced_start = buffer_read<math::vector3>(data, bullet_start);
        if (count < 0 || count > bullet_array_count || !traced_start.is_valid() || traced_start.distance(start) > start_tolerance)
        {
            mark_bullets_broken("trace data layout mismatch");
            return out;
        }
        if (count > 0 && !systems::reads::readable(entries, static_cast<std::size_t>(count) * entry_stride))
        {
            mark_bullets_broken("unreadable entries");
            return out;
        }

        float damage = weapon.damage;
        int penetrations = 0;
        out.ok = true;
        out.end = start + call.delta;
        for (int i = 0; i < count; ++i)
        {
            const std::uintptr_t entry = entries + static_cast<std::size_t>(i) * entry_stride;
            const auto index = static_cast<std::uint16_t>(memory::read<std::uint16_t>(entry + entry_handle) & entry_handle_mask);
            if (index >= bullet_array_count)
            {
                mark_bullets_broken("entry index out of range");
                return {};
            }
            alignas(16) std::uint8_t trace[trace_size]{};
            if (!run_bullet_info(info, data, trace, reinterpret_cast<std::uintptr_t>(data + bullet_array + index * bullet_array_stride)))
            {
                mark_bullets_broken("fault in trace info");
                return {};
            }

            const std::uintptr_t entity = buffer_read<std::uintptr_t>(trace, trace_entity);
            const auto position = buffer_read<math::vector3>(trace, trace_position);
            if (systems::reads::sane(position, systems::reads::world_limit))
                out.end = position;

            if (entity && entity == target)
            {
                const systems::tracing::result parsed = parse_trace(trace, start, start + call.delta);
                const float distance = systems::reads::sane(position, systems::reads::world_limit) ? position.distance(start) : parsed.end.distance(start);
                out.hitgroup = parsed.hitgroup;
                out.penetrations = penetrations;
                out.damage = scale_damage(target, range_damage(damage, distance, weapon), parsed.hitgroup, weapon);
                out.hit_target = out.damage > 0.f;
                return out;
            }

            if (entity && is_live_player(entity))
            {
                out.penetrations = penetrations;
                return out;
            }

            if (weapon.penetration <= 0.f || penetrations >= bullet_penetration_count)
            {
                out.penetrations = penetrations;
                return out;
            }

            const float previous = memory::read<float>(entry + entry_previous);
            const float current = memory::read<float>(entry + entry_current);
            float thickness = 0.f;
            if (std::isfinite(previous) && std::isfinite(current) && current >= previous)
                thickness = previous >= 0.f && current <= 1.f ? (current - previous) * weapon.range : current - previous;
            if (!std::isfinite(thickness) || thickness > max_wall_thickness)
            {
                out.penetrations = penetrations;
                return out;
            }

            float surface_modifier = 1.f;
            const std::uintptr_t surface = buffer_read<std::uintptr_t>(trace, trace_surface);
            if (surface && systems::reads::readable(surface + surface_penetration, sizeof(float)))
                surface_modifier = sane_float(memory::read<float>(surface + surface_penetration), 0.01f, 10.f, 1.f);

            damage -= penetration_loss(damage, thickness, surface_modifier, weapon);
            ++penetrations;
            if (damage < min_damage)
            {
                out.penetrations = penetrations;
                return out;
            }
        }
        out.penetrations = penetrations;
        return out;
    }

    bool engine_bullet_guarded(std::uintptr_t local_pawn, std::uintptr_t target, const math::vector3& start, const math::vector3& direction, const weapon_params& weapon, systems::tracing::bullet_result& out)
    {
        try
        {
            out = engine_bullet(local_pawn, target, start, direction, weapon);
            return true;
        }
        catch (...)
        {
            mark_bullets_broken("exception");
            out = {};
            return false;
        }
    }
}

namespace systems
{
    bool tracing_system::initialize()
    {
        const std::uintptr_t vtable = filter_vtable_address(true);
        const std::uintptr_t shape = PATTERN(patterns::trace_shape);
        const std::uintptr_t manager = trace_manager();
        if (!vtable)
            logs::Add(logs::Error, "tracing: CTraceFilter vtable not found");
        if (!shape)
            logs::Add(logs::Error, "tracing: trace_shape not found");
        if (!manager)
            logs::Add(logs::Warning, "tracing: trace manager not available yet");
        if (!PATTERN(patterns::trace_bullet) || !PATTERN(patterns::trace_bullet_update) || !PATTERN(patterns::trace_filter_init))
            logs::Add(logs::Warning, "tracing: autowall patterns missing, penetration disabled");
        const bool ok = ready();
        if (ok)
            logs::Add(logs::Success, "tracing: ready");
        return ok;
    }

    bool tracing_system::ready() const
    {
        return filter_vtable_address() && PATTERN(patterns::trace_shape) && trace_manager();
    }

    bool tracing_system::bullets_ready() const
    {
        if (g_bullets_broken.load(std::memory_order_acquire) || !ready())
            return false;
        return PATTERN(patterns::trace_bullet) && PATTERN(patterns::trace_bullet_update) && PATTERN(patterns::trace_filter_init);
    }

    tracing::filter tracing_system::make_filter(std::uintptr_t skip, std::uint64_t mask, int layer) const
    {
        tracing::filter out{};
        const std::uintptr_t vtable = filter_vtable_address();
        if (!vtable || layer < 0 || layer > 0xFF)
            return out;
        fill_filter(out.data, vtable, skip ? entity_handle(skip) : reads::invalid_handle, mask, static_cast<std::uint8_t>(layer));
        out.valid = true;
        return out;
    }

    tracing::filter tracing_system::make_player_movement_filter(std::uintptr_t pawn, std::uint64_t mask, int layer) const
    {
        return make_filter(pawn, mask, layer);
    }

    tracing::result tracing_system::trace_line(const math::vector3& start, const math::vector3& end, std::uintptr_t skip, std::uint64_t mask) const
    {
        return line_trace(start, end, skip ? entity_handle(skip) : reads::invalid_handle, mask);
    }

    tracing::result tracing_system::trace_ray(const math::vector3& start, const math::vector3& end, const tracing::filter& f) const
    {
        if (!f.valid)
            return {};
        return shape_trace(start, end, f.data, nullptr);
    }

    tracing::result tracing_system::trace_player_bbox(const math::vector3& start, const math::vector3& end, const tracing::hull& bounds, const tracing::filter& f, std::uintptr_t) const
    {
        if (!f.valid || !reads::sane(bounds.mins, hull_limit) || !reads::sane(bounds.maxs, hull_limit))
            return {};
        if (bounds.mins.x > bounds.maxs.x || bounds.mins.y > bounds.maxs.y || bounds.mins.z > bounds.maxs.z)
            return {};
        return shape_trace(start, end, f.data, &bounds);
    }

    tracing::bullet_result tracing_system::fire_bullet(std::uintptr_t local_pawn, std::uintptr_t target, const math::vector3& start, const math::vector3& end, bool allow_penetration) const
    {
        tracing::bullet_result out{};
        if (!local_pawn || !target || !reads::sane(start, reads::world_limit) || !reads::sane(end, reads::world_limit))
            return out;
        const math::vector3 delta = end - start;
        const float length = delta.length();
        if (!std::isfinite(length) || length < min_length)
            return out;
        weapon_params weapon{};
        if (!read_weapon(local_pawn, weapon))
            return out;
        const math::vector3 direction = delta / length;

        if (allow_penetration)
        {
            if (!bullets_ready())
                return out;
            engine_bullet_guarded(local_pawn, target, start, direction, weapon, out);
            return out;
        }

        if (!ready())
            return out;
        const tracing::result shot = line_trace(start, end + direction * shot_extension, entity_handle(local_pawn), cstypes::masks::shot);
        if (!shot.ok)
            return out;
        out.ok = true;
        out.end = shot.end;
        if (!is_visible(local_pawn, target, start, end))
            return out;
        out.hitgroup = shot.entity == target ? shot.hitgroup : -1;
        out.damage = scale_damage(target, range_damage(weapon.damage, length, weapon), out.hitgroup, weapon);
        out.hit_target = out.damage > 0.f;
        return out;
    }

    bool tracing_system::is_visible(std::uintptr_t local_pawn, std::uintptr_t target, const math::vector3& start, const math::vector3& end) const
    {
        if (!reads::sane(start, reads::world_limit) || !reads::sane(end, reads::world_limit))
            return false;
        const math::vector3 delta = end - start;
        const float length = delta.length();
        if (length < min_length)
            return true;
        const std::uint32_t skip = local_pawn ? entity_handle(local_pawn) : reads::invalid_handle;
        const tracing::result world = line_trace(start, end, skip, cstypes::masks::world);
        if (!world.ok || (1.f - world.fraction) * length > world_tolerance)
            return false;
        if (!target)
            return true;
        const math::vector3 extended = end + delta / length * shot_extension;
        const tracing::result shot = line_trace(start, extended, skip, cstypes::masks::shot);
        if (!shot.ok)
            return false;
        if (shot.entity == target || shot.fraction >= 1.f)
            return true;
        if (shot.entity && is_live_player(shot.entity))
            return false;
        return shot.fraction * (length + shot_extension) >= length - end_tolerance;
    }
}
