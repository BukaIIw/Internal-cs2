#pragma once
#include "../core/math.h"
#include <cstdint>

namespace systems
{
    namespace tracing
    {
        struct filter
        {
            alignas(16) std::uint8_t data[0x100]{};
            bool valid = false;
        };

        struct result
        {
            bool ok = false;
            std::uintptr_t entity = 0;
            float fraction = 1.f;
            math::vector3 start{};
            math::vector3 end{};
            math::vector3 normal{};
            int hitgroup = -1;
            int hitbox = -1;
            bool start_solid = false;
        };

        struct bullet_result
        {
            bool ok = false;
            bool hit_target = false;
            float damage = 0.f;
            int hitgroup = -1;
            int penetrations = 0;
            math::vector3 end{};
        };

        struct hull
        {
            math::vector3 mins{};
            math::vector3 maxs{};
        };
    }

    class tracing_system
    {
    public:
        bool initialize();
        bool ready() const;
        bool bullets_ready() const;

        tracing::filter make_filter(std::uintptr_t skip, std::uint64_t mask, int layer = 3) const;
        tracing::filter make_player_movement_filter(std::uintptr_t pawn, std::uint64_t mask, int layer) const;

        tracing::result trace_line(const math::vector3& start, const math::vector3& end, std::uintptr_t skip, std::uint64_t mask) const;
        tracing::result trace_ray(const math::vector3& start, const math::vector3& end, const tracing::filter& f) const;
        tracing::result trace_player_bbox(const math::vector3& start, const math::vector3& end, const tracing::hull& bounds, const tracing::filter& f, std::uintptr_t movement_services) const;

        tracing::bullet_result fire_bullet(std::uintptr_t local_pawn, std::uintptr_t target, const math::vector3& start, const math::vector3& end, bool allow_penetration) const;
        bool is_visible(std::uintptr_t local_pawn, std::uintptr_t target, const math::vector3& start, const math::vector3& end) const;
    };

    inline tracing_system g_tracing{};
}
