#include "shots.h"
#include "combat_detail.h"
#include "spread.h"
#include "../../core/cstypes.h"
#include "../../core/log.h"
#include "../../systems/entities.h"
#include "../../systems/local.h"
#include "../../systems/tracing.h"
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <mutex>

namespace
{
    using namespace features::combat;
    using shots::entry;
    using shots::fired;

    constexpr int max_pending = 16;
    constexpr int slots = 65;
    constexpr std::uint64_t resolve_ms = 700;
    constexpr float damage_match = 2.f;

    enum class reason
    {
        none,
        unknown,
        spread,
        occlusion,
        backtrack,
        pitch,
        animation,
        target_died,
        local_died
    };

    struct pending
    {
        bool active = false;
        std::uint64_t time = 0;
        fired shot{};
        bool expect_hit = false;
        int expect_group = -1;
        float expect_damage = 0.f;
        float distance = 0.f;
        reason predicted = reason::none;
    };

    pending g_pending[max_pending]{};
    int g_health[slots]{};
    entry g_entries[shots::max_entries]{};
    int g_entry_head = 0;
    std::mutex g_mutex;

    const char* group_name(int group)
    {
        switch (group)
        {
        case cstypes::hitgroup::head:
            return "head";
        case cstypes::hitgroup::neck:
            return "neck";
        case cstypes::hitgroup::chest:
            return "chest";
        case cstypes::hitgroup::stomach:
            return "stomach";
        case cstypes::hitgroup::left_arm:
            return "left arm";
        case cstypes::hitgroup::right_arm:
            return "right arm";
        case cstypes::hitgroup::left_leg:
            return "left leg";
        case cstypes::hitgroup::right_leg:
            return "right leg";
        case cstypes::hitgroup::generic:
            return "body";
        default:
            return "unknown";
        }
    }

    const char* reason_name(reason r)
    {
        switch (r)
        {
        case reason::spread:
            return "spread";
        case reason::occlusion:
            return "occlusion";
        case reason::backtrack:
            return "backtrack rejected";
        case reason::pitch:
            return "pitch desync";
        case reason::animation:
            return "animation desync";
        case reason::target_died:
            return "target died";
        case reason::local_died:
            return "local died";
        default:
            return "unknown";
        }
    }

    void push(bool hit, const char* text)
    {
        {
            std::lock_guard lock(g_mutex);
            entry& e = g_entries[g_entry_head];
            e.time = GetTickCount64();
            e.hit = hit;
            std::snprintf(e.text, sizeof(e.text), "%s", text);
            g_entry_head = (g_entry_head + 1) % shots::max_entries;
        }
        logs::Add(hit ? logs::Success : logs::Warning, "%s", text);
    }

    int infer_group(const pending& p, float dealt)
    {
        const weapon_context& ctx = g_shared.ctx();
        if (p.expect_hit && std::fabs(std::min(p.expect_damage, static_cast<float>(p.shot.player.health)) - dealt) <= damage_match)
            return p.expect_group;
        static constexpr int groups[] = {
            cstypes::hitgroup::head, cstypes::hitgroup::chest, cstypes::hitgroup::stomach,
            cstypes::hitgroup::left_arm, cstypes::hitgroup::left_leg, cstypes::hitgroup::neck,
        };
        int best = -1;
        float best_error = damage_match;
        for (int group : groups)
        {
            const float predicted = std::min(detail::damage(ctx, p.shot.player, group, p.distance), static_cast<float>(p.shot.player.health));
            const float error = std::fabs(predicted - dealt);
            if (error <= best_error)
            {
                best_error = error;
                best = group;
            }
        }
        return best >= 0 ? best : p.expect_group;
    }

    void resolve_hit(pending& p, int dealt, int remaining)
    {
        char text[shots::text_size];
        const int group = infer_group(p, static_cast<float>(dealt));
        std::snprintf(text, sizeof(text), "Hit %s in %s for %d (%d left) | aimed %s %.0f, hc %.0f%%%s", p.shot.player.name, group_name(group), dealt, std::max(0, remaining),
            group_name(p.shot.group), p.shot.damage, p.shot.hitchance, p.shot.backtrack_tick > 0 ? ", backtrack" : "");
        push(true, text);
        p.active = false;
    }

    void resolve_miss(pending& p, reason r)
    {
        char text[shots::text_size];
        std::snprintf(text, sizeof(text), "Missed %s due to %s | aimed %s %.0f, hc %.0f%%%s%s", p.shot.player.name, reason_name(r), group_name(p.shot.group), p.shot.damage,
            p.shot.hitchance, p.shot.backtrack_tick > 0 ? ", backtrack" : "", p.shot.nospread ? ", nospread" : "");
        push(false, text);
        p.active = false;
    }

    reason miss_reason(const pending& p)
    {
        if (p.predicted != reason::none)
            return p.predicted;
        if (p.shot.backtrack_tick > 0)
            return reason::backtrack;
        if (p.shot.pitch_broken)
            return reason::pitch;
        return reason::animation;
    }

    bool slot_pending(int slot, const pending* skip)
    {
        for (const pending& p : g_pending)
        {
            if (p.active && &p != skip && p.shot.slot == slot)
                return true;
        }
        return false;
    }

    pending* oldest(int slot)
    {
        pending* out = nullptr;
        for (pending& p : g_pending)
        {
            if (p.active && p.shot.slot == slot && (!out || p.time < out->time))
                out = &p;
        }
        return out;
    }
}

namespace features::combat::shots
{
    void on_fire(const fired& shot, const weapon_context& ctx, std::uintptr_t local_pawn)
    {
        if (shot.slot < 0 || shot.slot >= slots || !shot.player.pawn)
            return;
        pending* slot = nullptr;
        for (pending& p : g_pending)
        {
            if (!p.active)
            {
                slot = &p;
                break;
            }
        }
        if (!slot)
        {
            slot = &g_pending[0];
            for (pending& p : g_pending)
            {
                if (p.time < slot->time)
                    slot = &p;
            }
        }
        if (!slot_pending(shot.slot, slot))
            g_health[shot.slot] = shot.player.health;

        pending next{};
        next.active = true;
        next.time = GetTickCount64();
        next.shot = shot;
        next.predicted = reason::unknown;
        math::vector3 direction{};
        if (spread::bullet(ctx, shot.view, shot.recoil, shot.tick, direction))
        {
            float distance = 0.f;
            const int index = hitbox::nearest(shot.boxes, shot.eye, direction, ctx.range, distance);
            next.distance = distance;
            if (index < 0)
                next.predicted = reason::spread;
            else
            {
                const math::vector3 end = shot.eye + direction * distance;
                const hitbox::box& box = shot.boxes.boxes[index];
                bool reached = false;
                float damage = 0.f;
                int group = box.group;
                if (shot.penetrated && systems::g_tracing.bullets_ready())
                {
                    const systems::tracing::bullet_result bullet = systems::g_tracing.fire_bullet(local_pawn, shot.player.pawn, shot.eye, end, true);
                    reached = bullet.ok && bullet.hit_target && bullet.damage > 0.f;
                    damage = bullet.damage;
                    if (bullet.hitgroup >= 0)
                        group = bullet.hitgroup;
                }
                else if (systems::g_tracing.is_visible(local_pawn, shot.player.pawn, shot.eye, end))
                {
                    reached = true;
                    damage = detail::damage(ctx, shot.player, group, distance);
                }
                if (!reached)
                    next.predicted = reason::occlusion;
                else
                {
                    next.predicted = reason::none;
                    next.expect_hit = true;
                    next.expect_group = group;
                    next.expect_damage = damage;
                }
            }
        }
        if (!(next.distance > 0.f))
            next.distance = shot.eye.distance(shot.player.origin);
        *slot = next;
    }

    void update()
    {
        bool any = false;
        for (const pending& p : g_pending)
            any = any || p.active;
        if (!any)
            return;

        const std::uint64_t now = GetTickCount64();
        const systems::local_player::data local = systems::g_local.get();
        const auto players = systems::g_entities.players();
        for (int slot = 0; slot < slots; ++slot)
        {
            pending* first = oldest(slot);
            if (!first)
                continue;
            const systems::entities::player& player = players[static_cast<std::size_t>(slot)];
            if (player.pawn_handle != first->shot.player.pawn_handle)
            {
                resolve_miss(*first, reason::target_died);
                for (pending& p : g_pending)
                {
                    if (p.active && p.shot.slot == slot)
                        p.active = false;
                }
                continue;
            }
            const int health = player.alive ? player.health : 0;
            if (health < g_health[slot])
            {
                const int dealt = g_health[slot] - health;
                g_health[slot] = health;
                resolve_hit(*first, dealt, health);
                if (health <= 0)
                {
                    for (pending& p : g_pending)
                    {
                        if (p.active && p.shot.slot == slot)
                            p.active = false;
                    }
                }
                continue;
            }
            g_health[slot] = health;
        }

        for (pending& p : g_pending)
        {
            if (!p.active || now - p.time < resolve_ms)
                continue;
            if (!local.is_alive)
                resolve_miss(p, reason::local_died);
            else
                resolve_miss(p, miss_reason(p));
        }
    }

    void reset()
    {
        for (pending& p : g_pending)
            p = {};
    }

    int snapshot(entry* out, int max)
    {
        if (!out || max <= 0)
            return 0;
        std::lock_guard lock(g_mutex);
        int written = 0;
        for (int i = 1; i <= shots::max_entries && written < max; ++i)
        {
            const entry& e = g_entries[(g_entry_head + shots::max_entries - i) % shots::max_entries];
            if (e.time == 0)
                continue;
            out[written++] = e;
        }
        return written;
    }
}
