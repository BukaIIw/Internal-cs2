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
#include "../../systems/prediction.h"
#include "../../systems/tracing.h"
#include "../../systems/view.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace
{
    using namespace features::combat;

    constexpr int rage_priority = 2;
    constexpr int nospread_priority = 3;
    constexpr int scope_wait_ticks = 16;
    constexpr float scope_release_when = 0.5f;
    constexpr float damage_tolerance = 1.f;
    constexpr float percent = 100.f;
    constexpr int point_budget = 192;
    constexpr float sort_center_height = 36.f;
    constexpr std::size_t player_slots = 65;

    struct candidate
    {
        bool valid = false;
        systems::entities::player player{};
        int box = -1;
        int group = -1;
        int point_index = 0;
        bool body = false;
        bool penetrated = false;
        float damage = 0.f;
        float fov = 0.f;
        math::vector3 point{};
    };

    struct scan_entry
    {
        std::size_t slot = 0;
        bool locked = false;
        float fov = 0.f;
    };

    struct rage_state
    {
        std::uint32_t last_target = 0xFFFFFFFF;
        int scope_wait = 0;
        bool nospread = false;
        math::qangle desired{};
        math::qangle recoil{};
    };

    rage_state g_state{};
    hitbox::set g_scan_boxes{};
    hitbox::set g_best_boxes{};

    bool override_active(const settings::combat::rage& cfg)
    {
        const keys::bind& b = cfg.damage_override_key;
        return keys::active(b) && (b.key != 0 || b.type == keys::mode::always);
    }

    float effective(const candidate& c)
    {
        return std::min(c.damage, static_cast<float>(c.player.health));
    }

    bool is_body(int group)
    {
        return group != cstypes::hitgroup::head && group != cstypes::hitgroup::neck;
    }

    bool better_in_target(const candidate& next, const candidate& best, bool prefer_body)
    {
        if (!best.valid)
            return true;
        if (prefer_body && next.body != best.body)
            return next.body;
        const float a = effective(next);
        const float b = effective(best);
        if (std::fabs(a - b) > damage_tolerance)
            return a > b;
        if (next.point_index != best.point_index)
            return next.point_index < best.point_index;
        return next.fov < best.fov;
    }

    bool better_target(const candidate& next, const candidate& best)
    {
        if (!best.valid)
            return true;
        const float a = effective(next);
        const float b = effective(best);
        if (std::fabs(a - b) > damage_tolerance)
            return a > b;
        const bool next_locked = next.player.pawn_handle == g_state.last_target;
        const bool best_locked = best.player.pawn_handle == g_state.last_target;
        if (next_locked != best_locked)
            return next_locked;
        return next.fov < best.fov;
    }

    bool probe(const systems::entities::player& player, std::uintptr_t local_pawn, const weapon_context& ctx, const hitbox::box& box, const math::vector3& point, bool autowall, candidate& out)
    {
        if (autowall)
        {
            const systems::tracing::bullet_result bullet = systems::g_tracing.fire_bullet(local_pawn, player.pawn, ctx.eye, point, true);
            if (!bullet.ok || !bullet.hit_target || !(bullet.damage > 0.f))
                return false;
            out.damage = bullet.damage;
            out.group = bullet.hitgroup >= 0 ? bullet.hitgroup : box.group;
            out.penetrated = bullet.penetrations > 0;
            return true;
        }

        const math::vector3 direction = (point - ctx.eye).normalized();
        if (direction.is_zero())
            return false;
        float distance = 0.f;
        const int index = hitbox::nearest(g_scan_boxes, ctx.eye, direction, ctx.range, distance);
        if (index < 0)
            return false;
        const math::vector3 end = ctx.eye + direction * distance;
        if (!systems::g_tracing.is_visible(local_pawn, player.pawn, ctx.eye, end))
            return false;
        out.group = g_scan_boxes.boxes[index].group;
        out.damage = detail::damage(ctx, player, out.group, distance);
        out.penetrated = false;
        return out.damage > 0.f;
    }

    candidate scan_target(const systems::entities::player& player, std::uintptr_t local_pawn, const weapon_context& ctx, const math::qangle& reference, float required, bool autowall, int& point_total)
    {
        const settings::combat::rage& cfg = settings::g_rage;
        candidate best{};
        if (!hitbox::collect(player.pawn, g_scan_boxes))
            return best;

        math::vector3 points[hitbox::max_points]{};
        for (int i = 0; i < g_scan_boxes.count; ++i)
        {
            const hitbox::box& box = g_scan_boxes.boxes[i];
            if ((box.bit & cfg.hitboxes) == 0)
                continue;
            const bool multipoint = (box.bit & cfg.multipoint) != 0;
            const int count = hitbox::points(box, ctx.eye, multipoint, cfg.head_scale, cfg.body_scale, points, hitbox::max_points);
            for (int p = 0; p < count; ++p)
            {
                const math::vector3& point = points[p];
                if (!point.is_valid())
                    continue;
                const float fov = math::helpers::angle_fov(reference, math::helpers::calc_angle(ctx.eye, point));
                if (fov > cfg.fov)
                    continue;
                if (ctx.eye.distance(point) > ctx.range)
                    continue;
                if (point_total >= point_budget)
                    return best;
                ++point_total;
                candidate next{};
                if (!probe(player, local_pawn, ctx, box, point, autowall, next) || next.damage < required)
                    continue;
                next.valid = true;
                next.player = player;
                next.box = i;
                next.point_index = p;
                next.body = is_body(next.group);
                next.fov = fov;
                next.point = point;
                if (better_in_target(next, best, cfg.prefer_body))
                    best = next;
                break;
            }
        }
        return best;
    }

    int required_damage(int configured, int health)
    {
        return std::max(1, std::min(configured, health));
    }
}

namespace features::combat
{
    void rage::on_frame_stage(int)
    {
    }

    void rage::on_create_move(systems::input::frame& frame)
    {
        const settings::combat::rage& cfg = settings::g_rage;
        m_firing = false;
        m_stop = false;
        debug = {};
        g_state.nospread = false;
        systems::g_view.smooth_silent = cfg.silent_smooth;

        if (g_state.scope_wait > 0)
            --g_state.scope_wait;
        if (!frame.valid() || !keys::active(cfg.enabled, cfg.key))
            return;

        const weapon_context& ctx = g_shared.ctx();
        if (!ctx.valid || !ctx.gun || !ctx.eye.is_valid() || !systems::g_tracing.ready())
            return;
        const systems::local_player::data local = systems::g_local.get();
        if (!local.is_alive || !local.pawn)
            return;

        const bool autowall = cfg.autowall && systems::g_tracing.bullets_ready();
        const math::qangle reference = systems::g_view.original();
        const math::qangle recoil = detail::recoil(ctx);
        const int configured = override_active(cfg) ? cfg.damage_override : cfg.minimum_damage;

        const std::array<systems::entities::player, player_slots> players = systems::g_entities.players();
        std::array<scan_entry, player_slots> order{};
        std::size_t order_count = 0;
        for (std::size_t slot = 0; slot < players.size(); ++slot)
        {
            const systems::entities::player& player = players[slot];
            if (!detail::valid_target(player, local.pawn, cfg.teammates))
                continue;
            if (!detail::in_fov_range(reference, ctx.eye, player.origin, cfg.fov))
                continue;
            const math::vector3 center{ player.origin.x, player.origin.y, player.origin.z + sort_center_height };
            scan_entry& entry = order[order_count++];
            entry.slot = slot;
            entry.locked = player.pawn_handle == g_state.last_target;
            entry.fov = math::helpers::angle_fov(reference, math::helpers::calc_angle(ctx.eye, center));
        }
        std::sort(order.begin(), order.begin() + static_cast<std::ptrdiff_t>(order_count), [](const scan_entry& a, const scan_entry& b)
        {
            if (a.locked != b.locked)
                return a.locked;
            return a.fov < b.fov;
        });

        candidate best{};
        int point_total = 0;
        for (std::size_t i = 0; i < order_count && point_total < point_budget; ++i)
        {
            const systems::entities::player& player = players[order[i].slot];
            const float required = static_cast<float>(required_damage(configured, player.health));
            const candidate next = scan_target(player, local.pawn, ctx, reference, required, autowall, point_total);
            if (next.valid && better_target(next, best))
            {
                best = next;
                g_best_boxes = g_scan_boxes;
            }
        }

        debug.points = point_total;
        if (!best.valid)
            return;

        g_state.last_target = best.player.pawn_handle;
        debug.target = true;
        debug.hitgroup = best.group;
        debug.damage = best.damage;

        bool scoping = false;
        if (cfg.autoscope && ctx.needs_scope && !ctx.scoped)
        {
            scoping = true;
            if (g_state.scope_wait == 0 && !frame.held(cstypes::command_buttons::in_attack2))
            {
                frame.press(cstypes::command_buttons::in_attack2, 0.f);
                frame.release(cstypes::command_buttons::in_attack2, scope_release_when);
                g_state.scope_wait = scope_wait_ticks;
            }
        }

        const bool nospread = cfg.nospread && cfg.silent && spread::available();
        bool pass = false;
        math::vector3 point = best.point;
        if (ctx.can_fire && !scoping)
        {
            if (cfg.hitchance <= 0 || nospread)
            {
                pass = true;
                debug.hitchance = percent;
            }
            else
            {
                hitchance::request request{};
                request.local = local.pawn;
                request.target = &best.player;
                request.boxes = &g_best_boxes;
                request.shoot = ctx.eye;
                request.point = best.point;
                request.minimum_damage = static_cast<float>(required_damage(configured, best.player.health));
                request.target_health = static_cast<float>(best.player.health);
                request.range = ctx.range;
                request.penetration = autowall && best.penetrated;
                request.force_shot = cfg.force_shot;
                request.force_shot_iterations = cfg.force_shot_iterations;
                request.force_shot_min_spread = cfg.force_shot_min_spread;
                const hitchance::result result = hitchance::evaluate(request, static_cast<float>(cfg.hitchance) / percent);
                pass = result.pass;
                if (result.point.is_valid())
                    point = result.point;
                debug.hitchance = result.chance * percent;
            }
        }

        const math::qangle desired = math::helpers::sanitized(math::helpers::calc_angle(ctx.eye, point));
        const math::qangle aim = cfg.remove_recoil ? math::helpers::sanitized(desired - recoil) : desired;

        const systems::prediction::state& pre = systems::g_prediction.pre();
        const std::uint32_t flags = pre.valid ? pre.flags : local.flags;
        m_stop = cfg.autostop && (flags & cstypes::entity_flags::on_ground) != 0 && ctx.clip > 0 && (scoping || !pass);

        if (pass)
        {
            if (cfg.autofire)
                m_firing = systems::g_view.fire();
            else
                m_firing = frame.held(cstypes::command_buttons::in_attack);
        }

        if (!cfg.silent || m_firing)
            systems::g_view.aim(aim, cfg.silent, rage_priority, "rage");

        if (m_firing && nospread)
        {
            g_state.nospread = true;
            g_state.desired = cfg.remove_recoil ? desired : math::helpers::sanitized(desired + recoil);
            g_state.recoil = recoil;
        }
        debug.fired = m_firing;
    }

    void rage::on_create_move_post(systems::input::usercmd& cmd)
    {
        if (!g_state.nospread)
            return;
        g_state.nospread = false;
        if (!cmd)
            return;
        const int count = cmd.history_size();
        if (count <= 0)
            return;
        const int tick = cmd.history_player_tick(count - 1);
        if (tick <= 0)
            return;
        math::qangle angle{};
        if (spread::compensate(g_shared.ctx(), g_state.desired, g_state.recoil, tick, angle))
            systems::g_view.aim(angle, true, nospread_priority, "nospread");
    }

    void rage::reset()
    {
        m_firing = false;
        m_stop = false;
        debug = {};
        g_state = {};
    }
}
