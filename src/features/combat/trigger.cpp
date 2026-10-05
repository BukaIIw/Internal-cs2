#include "combat.h"
#include "combat_detail.h"
#include "hitbox.h"
#include "hitchance.h"
#include "spread.h"
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
    hitbox::set g_safe{};

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

    bool analytic(const settings::combat::trigger& cfg, std::uintptr_t local_pawn, const weapon_context& ctx, const math::vector3& forward, systems::entities::player& out_player, crosshair_hit& out)
    {
        const systems::tracing::result world = systems::g_tracing.trace_line(ctx.eye, ctx.eye + forward * ctx.range, local_pawn, cstypes::masks::world);
        if (!world.ok)
            return false;
        float limit = std::clamp(world.fraction, 0.f, 1.f) * ctx.range;
        bool found = false;
        const auto players = systems::g_entities.players();
        for (const systems::entities::player& player : players)
        {
            if (!detail::valid_target(player, local_pawn, cfg.teammates))
                continue;
            if (!hitbox::collect(player.pawn, g_boxes))
                continue;
            float distance = 0.f;
            const int index = hitbox::nearest(g_boxes, ctx.eye, forward, limit, distance);
            if (index < 0)
                continue;
            const hitbox::box& box = g_boxes.boxes[index];
            if ((box.bit & cfg.hitboxes) == 0)
                continue;
            limit = distance;
            out_player = player;
            out.group = box.group;
            out.bit = box.bit;
            out.distance = distance;
            out.point = ctx.eye + forward * distance;
            found = true;
        }
        if (found)
            hitbox::collect(out_player.pawn, g_boxes);
        return found;
    }

    bool lethal_enough(const settings::combat::trigger& cfg, const systems::entities::player& player, std::uintptr_t local_pawn, const weapon_context& ctx, const crosshair_hit& hit, const math::qangle& view)
    {
        const float required = static_cast<float>(std::max(1, std::min(cfg.minimum_damage, player.health)));
        if (detail::damage(ctx, player, hit.group, hit.distance) < required)
            return false;
        const bool seed = cfg.seed_check && spread::available();
        if (cfg.hitchance <= 0 && !seed)
            return true;
        hitchance::request request{};
        request.local = local_pawn;
        request.target = &player;
        hitbox::shrink(g_boxes, cfg.safe_scale, g_safe);
        request.boxes = &g_safe;
        request.shoot = ctx.eye;
        request.point = hit.point;
        request.minimum_damage = required;
        request.target_health = static_cast<float>(player.health);
        request.range = ctx.range;
        if (seed && hitchance::seed_hit(request, view, detail::recoil(ctx), ctx.tick_base) == 0)
            return false;
        return cfg.hitchance <= 0 || hitchance::evaluate(request, static_cast<float>(cfg.hitchance) / percent).pass;
    }
}

namespace features::combat
{
    void trigger::on_create_move(systems::input::frame& frame)
    {
        const settings::combat::trigger& global = settings::g_trigger;
        if (!frame.valid() || !keys::active(global.enabled, global.key))
        {
            m_seen = 0;
            return;
        }

        const weapon_context& ctx = g_shared.ctx();
        const settings::combat::trigger& cfg = settings::trigger_for(ctx.group);
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
        systems::entities::player player{};
        crosshair_hit target{};
        bool found = false;
        if (hit.ok && hit.entity)
        {
            player = systems::g_entities.player_by_pawn(hit.entity);
            found = detail::valid_target(player, local.pawn, cfg.teammates) && resolve(hit, ctx.eye, forward, ctx.range, target) && (target.bit & cfg.hitboxes) != 0;
        }
        if (!found)
            found = analytic(cfg, local.pawn, ctx, forward, player, target);
        if (!found)
        {
            m_seen = 0;
            return;
        }

        const std::uint64_t now = GetTickCount64();
        if (m_seen == 0)
            m_seen = now;
        if (now - m_seen < static_cast<std::uint64_t>(std::max(0, cfg.delay)))
            return;
        if (!ctx.can_fire || !lethal_enough(cfg, player, local.pawn, ctx, target, base))
            return;
        if (ctx.def == cstypes::weapon_id::revolver && !(ctx.revolver_ready_tick > 0 && ctx.revolver_ready_tick <= ctx.tick_base + 1))
        {
            systems::g_view.hold_attack();
            return;
        }
        systems::g_view.fire();
    }
}
