#include "prediction.h"
#include "game_reads.h"
#include "globals.h"
#include "../core/schema.h"
#include <Windows.h>
#include <algorithm>
#include <cmath>

namespace
{
    constexpr float networked_origin_tolerance = 128.f;
    constexpr float default_max_speed = 250.f;
    constexpr float max_max_speed = 1000.f;
    constexpr float max_gravity_scale = 10.f;
    constexpr float max_surface_friction = 4.f;
    constexpr float max_stamina = 100.f;

    bool read_quantized(std::uintptr_t base, std::uint32_t x, std::uint32_t y, std::uint32_t z, math::vector3& out)
    {
        if (!base || !x || !y || !z)
            return false;
        const float vx = systems::reads::value<float>(base + x, NAN);
        const float vy = systems::reads::value<float>(base + y, NAN);
        const float vz = systems::reads::value<float>(base + z, NAN);
        const math::vector3 v{ vx, vy, vz };
        if (!v.is_valid())
            return false;
        out = v;
        return true;
    }

    math::vector3 networked_velocity(std::uintptr_t pawn)
    {
        const math::vector3 fallback = systems::reads::abs_velocity(pawn);
        const std::uint32_t offset = SCHEMA("C_BaseEntity", "m_vecVelocity"_hash);
        if (!offset)
            return fallback;
        math::vector3 velocity{};
        const bool ok = read_quantized(pawn + offset,
            SCHEMA("CNetworkVelocityVector", "m_vecX"_hash),
            SCHEMA("CNetworkVelocityVector", "m_vecY"_hash),
            SCHEMA("CNetworkVelocityVector", "m_vecZ"_hash),
            velocity);
        if (!ok || velocity.is_zero() || !systems::reads::sane(velocity, systems::reads::velocity_limit))
            return fallback;
        return velocity;
    }

    math::vector3 networked_origin(std::uintptr_t pawn, const math::vector3& abs_origin)
    {
        const std::uintptr_t node = systems::reads::scene_node(pawn);
        const std::uint32_t offset = SCHEMA("CGameSceneNode", "m_vecOrigin"_hash);
        if (!node || !offset)
            return abs_origin;
        math::vector3 origin{};
        const bool ok = read_quantized(node + offset,
            SCHEMA("CNetworkOriginCellCoordQuantizedVector", "m_vecX"_hash),
            SCHEMA("CNetworkOriginCellCoordQuantizedVector", "m_vecY"_hash),
            SCHEMA("CNetworkOriginCellCoordQuantizedVector", "m_vecZ"_hash),
            origin);
        if (!ok || !systems::reads::sane(origin, systems::reads::world_limit) || origin.distance(abs_origin) > networked_origin_tolerance)
            return abs_origin;
        return origin;
    }

    void collect(systems::prediction::state& out, std::uintptr_t controller, std::uintptr_t pawn)
    {
        using namespace systems;
        if (!reads::readable(controller) || !reads::readable(pawn))
            return;
        math::vector3 origin{};
        if (!reads::abs_origin(pawn, origin))
            return;
        out.flags = reads::field<std::uint32_t>(pawn, SCHEMA("C_BaseEntity", "m_fFlags"_hash));
        out.move_type = reads::field<std::uint8_t>(pawn, SCHEMA("C_BaseEntity", "m_MoveType"_hash));
        out.networked_velocity = networked_velocity(pawn);
        out.networked_origin = networked_origin(pawn, origin);
        out.eye = reads::eye_position(pawn, origin);
        const float gravity = reads::field<float>(pawn, SCHEMA("C_BaseEntity", "m_flGravityScale"_hash), 1.f);
        out.gravity_scale = std::isfinite(gravity) && gravity > 0.f && gravity <= max_gravity_scale ? gravity : 1.f;
        const std::uintptr_t services = reads::field_pointer(pawn, SCHEMA("C_BasePlayerPawn", "m_pMovementServices"_hash));
        if (services)
        {
            const float friction = reads::field<float>(services, SCHEMA("CPlayer_MovementServices_Humanoid", "m_flSurfaceFriction"_hash), 1.f);
            out.surface_friction = std::isfinite(friction) && friction > 0.f && friction <= max_surface_friction ? friction : 1.f;
            const float stamina = reads::field<float>(services, SCHEMA("CCSPlayer_MovementServices", "m_flStamina"_hash));
            out.stamina = std::isfinite(stamina) ? std::clamp(stamina, 0.f, max_stamina) : 0.f;
            const float max_speed = reads::field<float>(services, SCHEMA("CPlayer_MovementServices", "m_flMaxspeed"_hash), default_max_speed);
            out.max_speed = std::isfinite(max_speed) && max_speed > 1.f && max_speed < max_max_speed ? max_speed : default_max_speed;
            const float duck = reads::field<float>(services, SCHEMA("CCSPlayer_MovementServices", "m_flDuckAmount"_hash));
            out.duck_amount = std::isfinite(duck) ? std::clamp(duck, 0.f, 1.f) : 0.f;
        }
        out.tick_base = reads::field<int>(controller, SCHEMA("CBasePlayerController", "m_nTickBase"_hash));
        out.tick_count = g_globals.tick_count();
        out.valid = true;
    }

    bool collect_guarded(systems::prediction::state& out, std::uintptr_t controller, std::uintptr_t pawn)
    {
        __try
        {
            collect(out, controller, pawn);
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
    void prediction_system::update(std::uintptr_t controller, std::uintptr_t pawn, const math::vector2& last_impulses)
    {
        prediction::state next{};
        if (!collect_guarded(next, controller, pawn))
            next = {};
        if (std::isfinite(last_impulses.x) && std::isfinite(last_impulses.y))
            next.last_movement_impulses = last_impulses;
        m_pre = next;
    }
}
