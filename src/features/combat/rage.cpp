#include "combat.h"
#include "combat_detail.h"
#include "backtrack.h"
#include "hitbox.h"
#include "hitchance.h"
#include "shots.h"
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
#include <Windows.h>
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
    constexpr int autostop_ticks = 1;
    constexpr int backtrack_scans = 3;
    constexpr int landing_ticks = 2;
    constexpr float scope_release_when = 0.5f;
    constexpr float damage_tolerance = 1.f;
    constexpr float percent = 100.f;
    constexpr int point_budget = 192;
    constexpr int autowall_budget = 40;
    constexpr double scan_budget_ms = 2.5;
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
        int tick = 0;
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
        bool fired = false;
        math::qangle desired{};
        math::qangle recoil{};
        math::qangle aim{};
        shots::fired shot{};
    };

    rage_state g_state{};
    hitbox::set g_scan_boxes{};
    hitbox::set g_best_boxes{};
    int g_autowall_calls = 0;
    LONGLONG g_deadline = 0;

    LONGLONG now_ticks()
    {
        LARGE_INTEGER value{};
        QueryPerformanceCounter(&value);
        return value.QuadPart;
    }

    void start_budget()
    {
        LARGE_INTEGER frequency{};
        QueryPerformanceFrequency(&frequency);
        g_deadline = now_ticks() + static_cast<LONGLONG>(static_cast<double>(frequency.QuadPart) * scan_budget_ms / 1000.0);
        g_autowall_calls = 0;
    }

    bool over_budget(int point_total)
    {
        return point_total >= point_budget || now_ticks() > g_deadline;
    }

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

    bool probe(const hitbox::set& boxes, const systems::entities::player& player, std::uintptr_t local_pawn, const weapon_context& ctx, const hitbox::box& box, const math::vector3& point, bool autowall, candidate& out)
    {
        if (autowall)
        {
            if (probe(boxes, player, local_pawn, ctx, box, point, false, out))
                return true;
            if (g_autowall_calls >= autowall_budget)
                return false;
            ++g_autowall_calls;
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
        const int index = hitbox::nearest(boxes, ctx.eye, direction, ctx.range, distance);
        if (index < 0)
            return false;
        const math::vector3 end = ctx.eye + direction * distance;
        if (!systems::g_tracing.is_visible(local_pawn, player.pawn, ctx.eye, end))
            return false;
        out.group = boxes.boxes[index].group;
        out.damage = detail::damage(ctx, player, out.group, distance);
        out.penetrated = false;
        return out.damage > 0.f;
    }

    candidate scan_boxes(const settings::combat::rage& cfg, const hitbox::set& boxes, const systems::entities::player& player, std::uintptr_t local_pawn, const weapon_context& ctx, const math::qangle& reference, float required, bool autowall, std::uint32_t mask, int& point_total)
    {
        candidate best{};
        math::vector3 points[hitbox::max_points]{};
        for (int i = 0; i < boxes.count; ++i)
        {
            const hitbox::box& box = boxes.boxes[i];
            if ((box.bit & mask) == 0)
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
                if (over_budget(point_total))
                    return best;
                ++point_total;
                candidate next{};
                if (!probe(boxes, player, local_pawn, ctx, box, point, autowall, next) || next.damage < required)
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

    std::uint32_t target_mask(const settings::combat::rage& cfg, std::size_t slot)
    {
        const std::uint32_t body = cfg.hitboxes & ~static_cast<std::uint32_t>(settings::combat::hb_head);
        if (body && backtrack::pitch_broken(static_cast<int>(slot)))
            return body;
        return cfg.hitboxes;
    }

    bool landing_soon(std::uintptr_t pawn, const math::vector3& origin, const math::vector3& velocity)
    {
        if (velocity.z >= 0.f)
            return false;
        const float fall = -velocity.z * cstypes::tick_interval * static_cast<float>(landing_ticks);
        const math::vector3 end{ origin.x, origin.y, origin.z - std::max(fall, 1.f) - 1.f };
        const systems::tracing::result tr = systems::g_tracing.trace_line(origin, end, pawn, cstypes::masks::world);
        return tr.ok && tr.fraction < 1.f;
    }

    void cock_revolver(const weapon_context& ctx)
    {
        if (!ctx.can_fire)
            return;
        const bool about_to_fire = ctx.revolver_ready_tick > 0 && ctx.revolver_ready_tick <= ctx.tick_base + 1;
        if (!about_to_fire)
            systems::g_view.hold_attack();
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
        const settings::combat::rage& global = settings::g_rage;
        m_firing = false;
        m_stop = false;
        debug = {};
        g_state.nospread = false;
        g_state.fired = false;
        shots::update();

        if (g_state.scope_wait > 0)
            --g_state.scope_wait;
        const weapon_context& ctx = g_shared.ctx();
        if (global.enabled)
            backtrack::update(ctx.tick_base);
        if (!frame.valid() || !keys::active(global.enabled, global.key))
            return;

        const settings::combat::rage& cfg = settings::rage_for(ctx.group);
        systems::g_view.smooth_silent = cfg.silent_smooth;
        if (!ctx.valid || !ctx.gun || !ctx.eye.is_valid() || !systems::g_tracing.ready())
            return;
        const systems::local_player::data local = systems::g_local.get();
        if (!local.is_alive || !local.pawn)
            return;

        const bool autowall = cfg.autowall && systems::g_tracing.bullets_ready();
        const math::qangle reference = systems::g_view.original();
        const math::qangle recoil = detail::recoil(ctx);
        const int configured = override_active(global) ? cfg.damage_override : cfg.minimum_damage;

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
        start_budget();
        for (std::size_t i = 0; i < order_count && !over_budget(point_total); ++i)
        {
            const systems::entities::player& player = players[order[i].slot];
            const float required = static_cast<float>(required_damage(configured, player.health));
            const std::uint32_t mask = target_mask(cfg, order[i].slot);
            candidate player_best{};
            const hitbox::set* player_boxes = nullptr;
            if (hitbox::collect(player.pawn, g_scan_boxes))
            {
                player_best = scan_boxes(cfg, g_scan_boxes, player, local.pawn, ctx, reference, required, autowall, mask, point_total);
                if (player_best.valid)
                    player_boxes = &g_scan_boxes;
            }
            if (!player_best.valid || effective(player_best) < static_cast<float>(player.health))
            {
                const backtrack::record* records[backtrack::max_records]{};
                const int count = backtrack::collect(static_cast<int>(order[i].slot), ctx.tick_base, records, backtrack::max_records);
                const int picks[backtrack_scans]{ count - 1, count / 2, 1 };
                for (int k = 0; k < backtrack_scans && !over_budget(point_total); ++k)
                {
                    const int index = picks[k];
                    if (index < 1 || index >= count || (k > 0 && index == picks[k - 1]))
                        continue;
                    const backtrack::record& rec = *records[index];
                    candidate old = scan_boxes(cfg, rec.boxes, player, local.pawn, ctx, reference, required, false, mask, point_total);
                    if (!old.valid)
                        continue;
                    if (!player_best.valid || effective(old) > effective(player_best) + damage_tolerance)
                    {
                        old.tick = rec.tick;
                        player_best = old;
                        player_boxes = &rec.boxes;
                    }
                }
            }
            if (player_best.valid && player_boxes && better_target(player_best, best))
            {
                best = player_best;
                g_best_boxes = *player_boxes;
            }
        }

        debug.points = point_total;
        if (!best.valid)
        {
            if (ctx.def == cstypes::weapon_id::revolver && cfg.autofire)
                cock_revolver(ctx);
            return;
        }

        g_state.last_target = best.player.pawn_handle;
        debug.target = true;
        debug.hitgroup = best.group;
        debug.damage = best.damage;

        const bool revolver = ctx.def == cstypes::weapon_id::revolver;
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
        const math::qangle aim = math::helpers::sanitized(desired - recoil);

        const systems::prediction::state& pre = systems::g_prediction.pre();
        const std::uint32_t flags = pre.valid ? pre.flags : local.flags;
        const std::uint32_t stop_flags = cfg.autostop_flags;
        const bool between = (stop_flags & settings::combat::as_between_shots) != 0;
        const bool ready_soon = ctx.clip > 0 && !ctx.reloading && (between || ctx.ticks_to_fire <= autostop_ticks);
        const bool lethal_ok = (stop_flags & settings::combat::as_lethal) == 0 || effective(best) >= static_cast<float>(best.player.health);
        const bool grounded = (flags & cstypes::entity_flags::on_ground) != 0;
        bool air_ok = (stop_flags & settings::combat::as_air) != 0;
        if (!grounded && !air_ok && (stop_flags & settings::combat::as_landing) != 0)
        {
            const math::vector3 velocity = pre.valid ? pre.networked_velocity : local.velocity;
            const math::vector3 origin = pre.valid ? pre.networked_origin : local.origin;
            air_ok = landing_soon(local.pawn, origin, velocity);
        }
        m_stop = cfg.autostop && ready_soon && lethal_ok && (grounded || air_ok);

        if (pass)
        {
            if (cfg.autofire)
                m_firing = systems::g_view.fire();
            else
                m_firing = frame.held(cstypes::command_buttons::in_attack);
            if (m_firing && best.tick > 0)
                systems::g_view.set_render_tick(best.tick + 1);
        }

        if (!cfg.silent || m_firing)
            systems::g_view.aim(aim, cfg.silent, rage_priority, "rage");

        if (m_firing)
        {
            g_state.fired = true;
            g_state.nospread = nospread;
            g_state.desired = desired;
            g_state.recoil = recoil;
            g_state.aim = aim;
            shots::fired& shot = g_state.shot;
            shot.player = best.player;
            shot.slot = best.player.index;
            shot.group = best.group;
            shot.damage = best.damage;
            shot.hitchance = debug.hitchance;
            shot.backtrack_tick = best.tick;
            shot.pitch_broken = backtrack::pitch_broken(best.player.index);
            shot.penetrated = autowall && best.penetrated;
            shot.eye = ctx.eye;
            shot.recoil = recoil;
            shot.boxes = g_best_boxes;
        }
        if (revolver && cfg.autofire && !m_firing)
            cock_revolver(ctx);
        debug.fired = m_firing;
    }

    void rage::on_create_move_post(systems::input::usercmd& cmd)
    {
        if (!g_state.fired)
            return;
        g_state.fired = false;
        const bool nospread = g_state.nospread;
        g_state.nospread = false;
        if (!cmd)
            return;
        const int count = cmd.history_size();
        if (count <= 0)
            return;
        const int tick = cmd.history_player_tick(count - 1);
        if (tick <= 0)
            return;
        math::qangle view = g_state.aim;
        if (nospread)
        {
            math::qangle angle{};
            if (spread::compensate(g_shared.ctx(), g_state.desired, g_state.recoil, tick, angle))
            {
                systems::g_view.aim(angle, true, nospread_priority, "nospread");
                debug.nospread = 1;
                view = angle;
            }
            else
                debug.nospread = 2;
        }
        const systems::local_player::data local = systems::g_local.get();
        g_state.shot.nospread = debug.nospread == 1;
        g_state.shot.view = view;
        g_state.shot.tick = tick;
        shots::on_fire(g_state.shot, g_shared.ctx(), local.pawn);
    }

    void rage::reset()
    {
        m_firing = false;
        m_stop = false;
        debug = {};
        g_state = {};
        backtrack::reset();
        shots::reset();
    }
}
