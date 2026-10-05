#include "combat.h"
#include "combat_detail.h"
#include "backtrack.h"
#include "hitbox.h"
#include "hitchance.h"
#include "shots.h"
#include "spread.h"
#include "../../core/convar.h"
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
    constexpr int server_ticks = 1;
    constexpr int server_shoot_window = 4;
    constexpr int scope_wait_ticks = 16;
    constexpr int max_stop_ticks = 8;
    constexpr float accurate_speed_fraction = 0.34f;
    constexpr float default_friction = 5.2f;
    constexpr float default_stop_speed = 80.f;
    constexpr float default_accelerate = 5.5f;
    constexpr int backtrack_scans = 3;
    constexpr int landing_ticks = 2;
    constexpr float scope_release_when = 0.5f;
    constexpr float damage_tolerance = 1.f;
    constexpr float percent = 100.f;
    constexpr int point_budget = 192;
    constexpr int autowall_budget = 40;
    constexpr int autowall_per_target = 16;
    constexpr std::size_t primary_targets = 2;
    constexpr double scan_budget_ms = 2.5;
    constexpr float sort_center_height = 36.f;
    constexpr std::size_t player_slots = 65;
    constexpr int confirm_ticks = 16;
    constexpr float angle_epsilon = 1e-3f;

    struct unconfirmed
    {
        bool active = false;
        int clip = 0;
        int ticks = 0;
        std::uintptr_t weapon = 0;
        shots::fired shot{};
    };

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
        bool verify = false;
        bool cancel = false;
        int seed_tick = 0;
        hitchance::request seed_request{};
        math::qangle desired{};
        math::qangle recoil{};
        math::qangle aim{};
        shots::fired shot{};
        unconfirmed confirm{};
    };

    rage_state g_state{};
    hitbox::set g_scan_boxes{};
    hitbox::set g_best_boxes{};
    int g_autowall_calls = 0;
    std::size_t g_rotation = 0;
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

    bool penetrate(const systems::entities::player& player, std::uintptr_t local_pawn, const weapon_context& ctx, const hitbox::box& box, const math::vector3& point, candidate& out)
    {
        ++g_autowall_calls;
        const systems::tracing::bullet_result bullet = systems::g_tracing.fire_bullet(local_pawn, player.pawn, ctx.eye, point, true);
        if (!bullet.ok || !bullet.hit_target || !(bullet.damage > 0.f))
            return false;
        out.damage = bullet.damage;
        out.group = bullet.hitgroup >= 0 ? bullet.hitgroup : box.group;
        out.penetrated = bullet.penetrations > 0;
        return true;
    }

    bool probe(const hitbox::set& boxes, const systems::entities::player& player, std::uintptr_t local_pawn, const weapon_context& ctx, const math::vector3& point, candidate& out)
    {
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
                if (!probe(boxes, player, local_pawn, ctx, point, next) || next.damage < required)
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
        if (best.valid || !autowall)
            return best;

        int calls = 0;
        for (int i = 0; i < boxes.count && calls < autowall_per_target && g_autowall_calls < autowall_budget; ++i)
        {
            const hitbox::box& box = boxes.boxes[i];
            if ((box.bit & mask) == 0 || !box.center.is_valid())
                continue;
            const float fov = math::helpers::angle_fov(reference, math::helpers::calc_angle(ctx.eye, box.center));
            if (fov > cfg.fov || ctx.eye.distance(box.center) > ctx.range)
                continue;
            if (now_ticks() > g_deadline)
                break;
            ++calls;
            ++point_total;
            candidate next{};
            if (!penetrate(player, local_pawn, ctx, box, box.center, next) || next.damage < required)
                continue;
            next.valid = true;
            next.player = player;
            next.box = i;
            next.point_index = 0;
            next.body = is_body(next.group);
            next.fov = fov;
            next.point = box.center;
            if (better_in_target(next, best, cfg.prefer_body))
                best = next;
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

    float convar_or(const convars::convar* cvar, float fallback)
    {
        if (!cvar || !cvar->value)
            return fallback;
        const float value = cvar->get<float>();
        return std::isfinite(value) && value > 0.f ? value : fallback;
    }

    int stop_ticks(const weapon_context& ctx, const math::vector3& velocity, float surface_friction)
    {
        float speed = velocity.length_2d();
        const float max_speed = std::max(ctx.max_speed, 1.f);
        const float accurate = max_speed * accurate_speed_fraction;
        const float friction = convar_or(CONVAR("sv_friction"), default_friction) * surface_friction;
        const float stop_speed = convar_or(CONVAR("sv_stopspeed"), default_stop_speed);
        const float accelerate = convar_or(CONVAR("sv_accelerate"), default_accelerate) * max_speed * cstypes::tick_interval * surface_friction;
        int ticks = 0;
        while (std::isfinite(speed) && speed > accurate && ticks < max_stop_ticks)
        {
            speed = std::max(0.f, speed - std::max(speed, stop_speed) * friction * cstypes::tick_interval);
            speed = std::max(0.f, speed - accelerate);
            ++ticks;
        }
        return ticks;
    }

    int shot_tick(systems::input::usercmd& cmd, int& index)
    {
        index = -1;
        const int count = cmd.history_size();
        const int tick_base = g_shared.ctx().tick_base;
        if (count <= 0 || tick_base <= 0)
            return 0;
        if (settings::g_rage.tick_source != server_ticks)
        {
            index = count - 1;
            return cmd.history_player_tick(index);
        }
        const int attack = cmd.attack1_index();
        if (attack < 0 || attack >= count)
            return tick_base;
        index = attack;
        return std::clamp(cmd.history_player_tick(attack), tick_base - server_shoot_window, tick_base);
    }

    int required_damage(int configured, int health)
    {
        return std::max(1, std::min(configured, health));
    }

    void confirm_shot(const weapon_context& ctx)
    {
        unconfirmed& c = g_state.confirm;
        if (!c.active)
            return;
        if (!ctx.valid || ctx.weapon != c.weapon)
        {
            c = {};
            return;
        }
        if (ctx.clip < c.clip)
        {
            shots::on_fire(c.shot, ctx, systems::g_local.get().pawn);
            c = {};
            return;
        }
        if (++c.ticks > confirm_ticks)
            c = {};
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
        g_state.verify = false;
        g_state.cancel = false;
        confirm_shot(g_shared.ctx());
        shots::update();

        if (g_state.scope_wait > 0)
            --g_state.scope_wait;
        const weapon_context& ctx = g_shared.ctx();
        if (global.enabled)
            backtrack::update(ctx.tick_base);
        if (!frame.valid() || !keys::active(global.enabled, global.key))
            return;

        const settings::combat::rage& cfg = settings::rage_for(ctx.group);
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

        std::array<std::size_t, primary_targets + 1> queue{};
        std::size_t queued = 0;
        for (std::size_t i = 0; i < order_count && i < primary_targets; ++i)
            queue[queued++] = i;
        if (order_count > primary_targets)
            queue[queued++] = primary_targets + g_rotation++ % (order_count - primary_targets);

        candidate best{};
        int point_total = 0;
        start_budget();
        for (std::size_t q = 0; q < queued && !over_budget(point_total); ++q)
        {
            const std::size_t i = queue[q];
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
        const bool seed = cfg.seed_check && !nospread && spread::available();
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
        bool pass = false;
        math::vector3 point = best.point;
        if ((ctx.can_fire || g_doubletap.charged(ctx)) && !scoping)
        {
            if (cfg.hitchance <= 0 || nospread)
            {
                pass = true;
                debug.hitchance = percent;
            }
            else
            {
                const hitchance::result result = hitchance::evaluate(request, static_cast<float>(cfg.hitchance) / percent);
                pass = result.pass;
                if (result.point.is_valid())
                    point = result.point;
                debug.hitchance = result.chance * percent;
            }
        }

        const math::qangle desired = math::helpers::sanitized(math::helpers::calc_angle(ctx.eye, point));
        const math::qangle aim = math::helpers::sanitized(desired - recoil);
        if (pass && seed)
        {
            debug.seed = hitchance::seed_hit(request, aim, recoil, ctx.tick_base);
            if (debug.seed == 0)
                pass = false;
        }

        const systems::prediction::state& pre = systems::g_prediction.pre();
        const std::uint32_t flags = pre.valid ? pre.flags : local.flags;
        const std::uint32_t stop_flags = cfg.autostop_flags;
        const bool between = (stop_flags & settings::combat::as_between_shots) != 0;
        const math::vector3 stop_velocity = pre.valid ? pre.networked_velocity : local.velocity;
        const int needed_ticks = stop_ticks(ctx, stop_velocity, pre.valid ? pre.surface_friction : 1.f);
        const bool ready_soon = ctx.clip > 0 && !ctx.reloading && (between || ctx.ticks_to_fire <= needed_ticks + 1);
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

        const bool user_attack = frame.held(cstypes::command_buttons::in_attack);
        if (pass)
        {
            if (cfg.autofire || user_attack)
                m_firing = systems::g_view.fire();
            if (m_firing && best.tick > 0)
                systems::g_view.set_render_tick(best.tick + 1);
        }
        else if (user_attack && !cfg.autofire && !revolver)
            systems::g_view.suppress_fire();

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
            g_state.verify = seed;
            g_state.seed_tick = ctx.tick_base;
            g_state.seed_request = request;
            g_state.seed_request.target = &shot.player;
            g_state.seed_request.boxes = &shot.boxes;
        }
        if (revolver && cfg.autofire && !m_firing)
            cock_revolver(ctx);
        debug.fired = m_firing;
    }

    void rage::on_create_move_post(systems::input::usercmd& cmd)
    {
        if (!g_state.fired || settings::g_rage.tick_source == server_ticks)
            return;
        g_state.fired = false;
        const bool nospread = g_state.nospread;
        g_state.nospread = false;
        if (!cmd)
            return;
        int index = -1;
        const int tick = shot_tick(cmd, index);
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
            {
                debug.nospread = 2;
                g_state.cancel = true;
                return;
            }
        }
        finish_shot(view, tick);
    }

    void rage::on_create_move_late(systems::input::usercmd& cmd)
    {
        if (g_state.fired && settings::g_rage.tick_source == server_ticks)
        {
            g_state.fired = false;
            const bool nospread = g_state.nospread;
            g_state.nospread = false;
            int index = -1;
            const int tick = cmd ? shot_tick(cmd, index) : 0;
            if (tick > 0)
            {
                math::qangle view = g_state.aim;
                bool ready = true;
                if (nospread)
                {
                    math::qangle angle{};
                    if (spread::compensate(g_shared.ctx(), g_state.desired, g_state.recoil, tick, angle))
                    {
                        const int count = cmd.history_size();
                        for (int i = index >= 0 ? index : 0; i < count; ++i)
                            cmd.set_history_angles(i, angle);
                        if (index < 0)
                            cmd.set_base_angles(angle);
                        debug.nospread = 1;
                        view = angle;
                    }
                    else
                    {
                        debug.nospread = 2;
                        g_state.cancel = true;
                        ready = false;
                    }
                }
                if (ready)
                    finish_shot(view, tick);
            }
        }

        if (cmd && g_state.cancel)
            cancel_shot(cmd);
        else if (cmd && g_state.verify)
        {
            int index = -1;
            const int tick = shot_tick(cmd, index);
            math::qangle view{};
            const bool angles = index >= 0 ? cmd.history_angles(index, view) : cmd.base_angles(view);
            if (tick > 0 && angles && view.is_valid())
            {
                debug.seed_delta = tick - g_state.seed_tick;
                view.z = 0.f;
                const bool moved = std::fabs(view.x - g_state.aim.x) > angle_epsilon || std::fabs(math::helpers::normalized_angle(view.y - g_state.aim.y)) > angle_epsilon;
                if ((tick != g_state.seed_tick || moved) && hitchance::seed_hit(g_state.seed_request, view, g_state.recoil, tick) == 0)
                {
                    debug.seed = 0;
                    cancel_shot(cmd);
                }
            }
        }
        g_state.cancel = false;
        g_state.verify = false;
    }

    void rage::cancel_shot(systems::input::usercmd& cmd)
    {
        systems::g_view.cancel(cmd);
        g_state.confirm = {};
        m_firing = false;
        debug.fired = false;
    }

    void rage::finish_shot(const math::qangle& view, int tick)
    {
        g_state.shot.nospread = debug.nospread == 1;
        g_state.shot.view = view;
        g_state.shot.tick = tick;
        const weapon_context& ctx = g_shared.ctx();
        unconfirmed& c = g_state.confirm;
        if (!c.active && ctx.valid && ctx.clip > 0)
        {
            c.active = true;
            c.clip = ctx.clip;
            c.ticks = 0;
            c.weapon = ctx.weapon;
            c.shot = g_state.shot;
        }
    }

    void rage::reset()
    {
        m_firing = false;
        m_stop = false;
        debug = {};
        g_state = {};
        backtrack::reset();
        shots::reset();
        hitchance::reset();
    }
}
