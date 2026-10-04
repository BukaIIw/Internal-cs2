#pragma once
#include "entities.h"
#include "../core/hash.h"
#include "../core/math.h"
#include "../core/memory.h"
#include "../core/schema.h"
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace systems
{
    namespace reads
    {
        constexpr float world_limit = 65536.f;
        constexpr float velocity_limit = 10000.f;
        constexpr float default_eye_height = 64.f;
        constexpr float min_eye_height = 10.f;
        constexpr float max_eye_height = 80.f;
        constexpr std::uint32_t invalid_handle = 0xFFFFFFFFu;

        inline bool readable(std::uintptr_t address, std::size_t size = sizeof(std::uintptr_t))
        {
            return address && memory::is_readable(address, size);
        }

        inline std::uintptr_t pointer(std::uintptr_t address)
        {
            if (!readable(address))
                return 0;
            const auto value = memory::read<std::uintptr_t>(address);
            return readable(value, 1) ? value : 0;
        }

        template <typename T>
        inline T value(std::uintptr_t address, T fallback = T{})
        {
            return readable(address, sizeof(T)) ? memory::read<T>(address) : fallback;
        }

        template <typename T>
        inline T field(std::uintptr_t base, std::uint32_t offset, T fallback = T{})
        {
            if (!base || !offset)
                return fallback;
            return value<T>(base + offset, fallback);
        }

        inline std::uintptr_t field_pointer(std::uintptr_t base, std::uint32_t offset)
        {
            if (!base || !offset)
                return 0;
            return pointer(base + offset);
        }

        inline bool designer_is(const char* name, const char* expected)
        {
            if (!name || !expected)
                return false;
            const std::size_t length = std::strlen(expected) + 1;
            if (!readable(reinterpret_cast<std::uintptr_t>(name), length))
                return false;
            return std::memcmp(name, expected, length) == 0;
        }

        inline bool sane(const math::vector3& v, float limit)
        {
            return v.is_valid() && std::fabs(v.x) < limit && std::fabs(v.y) < limit && std::fabs(v.z) < limit;
        }

        inline std::uintptr_t scene_node(std::uintptr_t entity)
        {
            return field_pointer(entity, SCHEMA("C_BaseEntity", "m_pGameSceneNode"_hash));
        }

        inline bool abs_origin(std::uintptr_t entity, math::vector3& out)
        {
            const std::uintptr_t node = scene_node(entity);
            const std::uint32_t offset = SCHEMA("CGameSceneNode", "m_vecAbsOrigin"_hash);
            if (!node || !offset || !readable(node + offset, sizeof(math::vector3)))
                return false;
            const auto origin = memory::read<math::vector3>(node + offset);
            if (!sane(origin, world_limit))
                return false;
            out = origin;
            return true;
        }

        inline math::vector3 abs_velocity(std::uintptr_t entity)
        {
            const auto velocity = field<math::vector3>(entity, SCHEMA("C_BaseEntity", "m_vecAbsVelocity"_hash));
            return sane(velocity, velocity_limit) ? velocity : math::vector3{};
        }

        inline float view_offset_z(std::uintptr_t pawn)
        {
            const std::uint32_t offset = SCHEMA("C_BaseModelEntity", "m_vecViewOffset"_hash);
            const std::uint32_t z = SCHEMA("CNetworkViewOffsetVector", "m_vecZ"_hash);
            if (!pawn || !offset || !z)
                return default_eye_height;
            const float height = value<float>(pawn + offset + z, default_eye_height);
            return std::isfinite(height) && height > min_eye_height && height < max_eye_height ? height : default_eye_height;
        }

        inline math::vector3 eye_position(std::uintptr_t pawn, const math::vector3& origin)
        {
            return { origin.x, origin.y, origin.z + view_offset_z(pawn) };
        }

        inline std::uintptr_t active_weapon(std::uintptr_t pawn, std::uint32_t* handle_out = nullptr)
        {
            if (handle_out)
                *handle_out = invalid_handle;
            const std::uintptr_t services = field_pointer(pawn, SCHEMA("C_BasePlayerPawn", "m_pWeaponServices"_hash));
            const std::uint32_t offset = SCHEMA("CPlayer_WeaponServices", "m_hActiveWeapon"_hash);
            if (!services || !offset)
                return 0;
            const auto handle = value<std::uint32_t>(services + offset, invalid_handle);
            if (handle_out)
                *handle_out = handle;
            return g_entities.lookup(handle);
        }

        inline std::uint16_t item_definition(std::uintptr_t weapon)
        {
            const std::uint32_t manager = SCHEMA("C_EconEntity", "m_AttributeManager"_hash);
            const std::uint32_t item = SCHEMA("C_AttributeContainer", "m_Item"_hash);
            const std::uint32_t definition = SCHEMA("C_EconItemView", "m_iItemDefinitionIndex"_hash);
            if (!weapon || !manager || !item || !definition)
                return 0;
            return value<std::uint16_t>(weapon + manager + item + definition, 0);
        }
    }
}
