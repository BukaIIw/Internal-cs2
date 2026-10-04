#include "combat.h"
#include "combat_detail.h"
#include "hitbox.h"
#include "hitchance.h"
#include "../../core/cstypes.h"
#include "../../core/keys.h"
#include "../../core/math.h"
#include "../../core/settings.h"
#include "../../systems/entities.h"
#include "../../systems/local.h"
#include "../../systems/tracing.h"
#include "../../systems/view.h"
#include <Windows.h>
#include <algorithm>
#include <cstdint>

namespace
{
    using namespace features::combat;

    constexpr float percent = 100.f;

    struct crosshair_hit
    {
        int group = -1;
        std::uint32_t bit = 0;
        float distance = 0.f;
        math::vector3 point{};
    };

    hitbox::set g_boxes{};

    bool resolve(const systems::tracing::result& hit, const math::vector3& eye, const math::vector3& forward, float range, crosshair_hit& out)
    {
        if (!hitbox::collect(hit.entity, g_boxes))
            return false;
        if (hit.hitgroup >= 0)
        {
            out.group = hit.hitgroup;
            out.bit = hitbox::bit_for(hit.hitbox, hit.hitgroup);
            out.point = hit.end;
            out.distance = eye.distance(hit.end);
            return true;
        }
        float distance = 0.f;
        const int index = hitbox::nearest(g_boxes, eye, forward, range, distance);
        if (index < 0)
            return false;
        const hitbox::box& box = g_boxes.boxes[index];
        out.group = box.group;
        out.bit = box.bit;
        out.distance = distance;
        out.point = eye + forward * distance;
        return true;
    }

    bool lethal_enough(const settings::combat::trigger& cfg, const systems::entities::player& player, std::uintptr_t local_pawn, const weapon_context& ctx, const crosshair_hit& hit)
    {
        const float required = static_cast<float>(std::max(1, std::min(cfg.minimum_damage, player.health)));
        if (detail::damage(ctx, player, hit.group, hit.distance) < required)
            return false;
        if (cfg.hitchance <= 0)
            return true;
        hitchance::request request{};
        request.local = local_pawn;
        request.target = &player;
        request.boxes = &g_boxes;
        request.shoot = ctx.eye;
        request.point = hit.point;
        request.minimum_damage = required;
        request.target_health = static_cast<float>(player.health);
        request.range = ctx.range;
        return hitchance::evaluate(request, static_cast<float>(cfg.hitchance) / percent).pass;
    }
}

namespace features::combat
{
    void trigger::on_create_move(systems::input::frame& frame)
    {
        const settings::combat::trigger& cfg = settings::g_trigger;
        if (!frame.valid() || !keys::active(cfg.enabled, cfg.key))
        {
            m_seen = 0;
            return;
        }

        const weapon_context& ctx = g_shared.ctx();
        const systems::local_player::data local = systems::g_local.get();
        if (!ctx.valid || !ctx.gun || !ctx.eye.is_valid() || !local.is_alive || !local.pawn || !systems::g_tracing.ready())
        {
            m_seen = 0;
            return;
        }

        const systems::view::request& request = systems::g_view.current();
        const math::qangle base = request.active ? request.angle : systems::g_view.original();
        const math::qangle angle = math::helpers::sanitized(base + detail::recoil(ctx));
        math::vector3 forward{};
        math::helpers::angle_vectors(angle, forward);
        const systems::tracing::result hit = systems::g_tracing.trace_line(ctx.eye, ctx.eye + forward * ctx.range, local.pawn, cstypes::masks::shot);
        if (!hit.ok || !hit.entity)
        {
            m_seen = 0;
            return;
        }

        const systems::entities::player player = systems::g_entities.player_by_pawn(hit.entity);
        crosshair_hit target{};
        if (!detail::valid_target(player, local.pawn, cfg.teammates) || !resolve(hit, ctx.eye, forward, ctx.range, target) || (target.bit & cfg.hitboxes) == 0)
        {
            m_seen = 0;
            return;
        }

        const std::uint64_t now = GetTickCount64();
        if (m_seen == 0)
            m_seen = now;
        if (now - m_seen < static_cast<std::uint64_t>(std::max(0, cfg.delay)))
            return;
        if (!ctx.can_fire || !lethal_enough(cfg, player, local.pawn, ctx, target))
            return;
        systems::g_view.fire();
    }
}
