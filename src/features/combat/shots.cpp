#include "shots.h"
#include "combat_detail.h"
#include "spread.h"
#include "../../core/cstypes.h"
#include "../../core/log.h"
#include "../../core/schema.h"
#include "../../core/settings.h"
#include "../../systems/game_reads.h"
#include "../../systems/entities.h"
#include "../../systems/game_events.h"
#include "../../systems/local.h"
#include "../../systems/tracing.h"
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <string>

namespace
{
    using namespace features::combat;
    using shots::entry;
    using shots::fired;

    constexpr int max_pending = 16;
    constexpr int slots = 65;
    constexpr std::uint64_t resolve_ms = 700;
    constexpr float damage_match = 2.f;
    constexpr int confirm_ticks = 16;
    constexpr int server_ticks = 1;
    constexpr int server_shoot_window = 4;
    constexpr float candidate_cos = 0.95f;
    constexpr float miss_angle = 0.07f;
    constexpr float target_height = 36.f;
    constexpr float impact_cos = 0.96f;
    constexpr int max_impacts = 32;
    constexpr float impact_align = 0.9995f;
    constexpr float impact_slack = 8.f;

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
        bool has_direction = false;
        math::vector3 direction{};
        int predicted_box = -1;
        math::vector3 predicted_point{};
        math::vector3 predicted_local{};
        int weapon_def = 0;
        float inaccuracy = 0.f;
        float spread = 0.f;
        math::vector3 local_velocity{};
        bool has_impact = false;
        math::vector3 impact{};
        math::vector3 impact_direction{};
        float deviation = -1.f;
        int impact_box = -1;
        bool impact_blocked = false;
        math::vector3 impact_local{};
    };

    struct manual_state
    {
        bool active = false;
        int clip = 0;
        int ticks = 0;
        std::uintptr_t weapon = 0;
        fired shot{};
    };

    manual_state g_manual{};
    hitbox::set g_search{};

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

    std::wstring log_directory()
    {
        static const std::wstring value = [] {
            wchar_t buffer[MAX_PATH]{};
            const DWORD length = GetEnvironmentVariableW(L"APPDATA", buffer, MAX_PATH);
            std::wstring dir = length > 0 && length < MAX_PATH ? std::wstring(buffer, length) : std::wstring(L".");
            dir += L"\\Internal-cs2";
            CreateDirectoryW(dir.c_str(), nullptr);
            dir += L"\\shots";
            CreateDirectoryW(dir.c_str(), nullptr);
            return dir;
        }();
        return value;
    }

    void append_vector(std::string& out, const math::vector3& v)
    {
        char buffer[96];
        std::snprintf(buffer, sizeof(buffer), "[%.3f,%.3f,%.3f]", v.x, v.y, v.z);
        out += buffer;
    }

    void append_angle(std::string& out, const math::qangle& v)
    {
        char buffer[96];
        std::snprintf(buffer, sizeof(buffer), "[%.4f,%.4f,%.4f]", v.x, v.y, v.z);
        out += buffer;
    }

    void append_string(std::string& out, const char* text)
    {
        out += '"';
        for (const char* c = text; c && *c; ++c)
        {
            const unsigned char ch = static_cast<unsigned char>(*c);
            if (ch == '"' || ch == '\\')
            {
                out += '\\';
                out += static_cast<char>(ch);
            }
            else if (ch >= 0x20)
                out += static_cast<char>(ch);
        }
        out += '"';
    }

    void append_number(std::string& out, const char* key, double value)
    {
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), ",\"%s\":%.4f", key, value);
        out += buffer;
    }

    void append_int(std::string& out, const char* key, long long value)
    {
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), ",\"%s\":%lld", key, value);
        out += buffer;
    }

    int server_group(const pending& p)
    {
        const std::uint32_t offset = SCHEMA("C_CSPlayerPawn", "m_LastHitGroup"_hash);
        if (!offset || !p.shot.player.pawn)
            return -1;
        const int group = systems::reads::field<int>(p.shot.player.pawn, offset, -1);
        return group > 0 && group < 16 ? group : -1;
    }

    void match_impact(pending& p)
    {
        if (p.has_impact || !p.shot.eye.is_valid())
            return;
        systems::game_events::impact impacts[max_impacts]{};
        const int count = systems::game_events::impacts_since(p.time, impacts, max_impacts);
        math::vector3 reference{};
        if (p.has_direction)
            reference = p.direction;
        else
            math::helpers::angle_vectors(math::helpers::sanitized(p.shot.view + p.shot.recoil), reference);
        float best = impact_cos;
        for (int i = 0; i < count; ++i)
        {
            const math::vector3 to = impacts[i].point - p.shot.eye;
            const float length = to.length();
            if (!(length > 1.f))
                continue;
            const math::vector3 dir = to * (1.f / length);
            const float dot = dir.dot(reference);
            if (dot <= best)
                continue;
            best = dot;
            p.has_impact = true;
            p.impact = impacts[i].point;
            p.impact_direction = dir;
        }
        if (!p.has_impact)
            return;
        if (p.has_direction)
            p.deviation = math::rad2deg(std::acos(std::clamp(p.impact_direction.dot(p.direction), -1.f, 1.f)));
        float reach = 0.f;
        for (int i = 0; i < count; ++i)
        {
            const math::vector3 to = impacts[i].point - p.shot.eye;
            const float length = to.length();
            if (length > 1.f && (to * (1.f / length)).dot(p.impact_direction) > impact_align)
                reach = std::max(reach, length);
        }
        float distance = 0.f;
        p.impact_box = hitbox::nearest(p.shot.boxes, p.shot.eye, p.impact_direction, g_shared.ctx().range, distance);
        p.impact_blocked = p.impact_box >= 0 && distance > reach + impact_slack;
        if (p.impact_box >= 0)
            p.impact_local = hitbox::describe(p.shot.boxes.boxes[p.impact_box], hitbox::core(p.shot.boxes.boxes[p.impact_box], p.shot.eye, p.impact_direction, distance));
    }

    void impact_text(const pending& p, char* out, std::size_t size)
    {
        out[0] = '\0';
        if (!p.has_impact)
            return;
        const char* where = p.impact_blocked ? "wall" : p.impact_box >= 0 ? group_name(p.shot.boxes.boxes[p.impact_box].group) : "none";
        if (p.deviation >= 0.f)
            std::snprintf(out, size, " | real %s, dev %.2f", where, p.deviation);
        else
            std::snprintf(out, size, " | real %s", where);
    }

    void write_record(const pending& p, bool hit, const char* why, int dealt, int remaining, int group, int server)
    {
        if (!settings::g_misc.shot_file)
            return;
        std::string line;
        line.reserve(8192);
        line += "{\"result\":";
        append_string(line, hit ? "hit" : "miss");
        line += ",\"reason\":";
        append_string(line, why);
        append_int(line, "time", static_cast<long long>(p.time));
        line += ",\"manual\":";
        line += p.shot.manual ? "true" : "false";
        line += ",\"nospread\":";
        line += p.shot.nospread ? "true" : "false";
        append_int(line, "weapon", p.weapon_def);
        append_int(line, "tick", p.shot.tick);
        append_int(line, "backtrack", p.shot.backtrack_tick);
        append_number(line, "inaccuracy", p.inaccuracy);
        append_number(line, "spread", p.spread);
        line += ",\"eye\":";
        append_vector(line, p.shot.eye);
        line += ",\"view\":";
        append_angle(line, p.shot.view);
        line += ",\"recoil\":";
        append_angle(line, p.shot.recoil);
        line += ",\"direction\":";
        append_vector(line, p.has_direction ? p.direction : math::vector3{});
        line += ",\"local_velocity\":";
        append_vector(line, p.local_velocity);
        line += ",\"target\":{\"name\":";
        append_string(line, p.shot.player.name);
        append_int(line, "health", p.shot.player.health);
        append_int(line, "armor", p.shot.player.armor);
        line += ",\"origin\":";
        append_vector(line, p.shot.player.origin);
        line += ",\"velocity\":";
        append_vector(line, p.shot.player.velocity);
        line += "}";
        append_int(line, "aimed_group", p.shot.group);
        append_number(line, "aimed_damage", p.shot.damage);
        append_number(line, "hitchance", p.shot.hitchance);
        append_int(line, "predicted_box", p.predicted_box);
        append_number(line, "predicted_distance", p.distance);
        line += ",\"predicted_point\":";
        append_vector(line, p.predicted_point);
        line += ",\"predicted_core\":";
        append_vector(line, p.predicted_local);
        append_int(line, "dealt", dealt);
        append_int(line, "remaining", remaining);
        append_int(line, "group", group);
        append_int(line, "server_group", server);
        line += ",\"impact\":";
        append_vector(line, p.has_impact ? p.impact : math::vector3{});
        line += ",\"impact_direction\":";
        append_vector(line, p.has_impact ? p.impact_direction : math::vector3{});
        append_number(line, "impact_deviation", p.deviation);
        append_int(line, "impact_box", p.impact_box);
        line += ",\"impact_core\":";
        append_vector(line, p.impact_local);
        line += ",\"boxes\":[";
        for (int i = 0; i < p.shot.boxes.count; ++i)
        {
            const hitbox::box& b = p.shot.boxes.boxes[i];
            if (i)
                line += ',';
            char head[96];
            std::snprintf(head, sizeof(head), "{\"index\":%d,\"group\":%d,\"capsule\":%d,\"radius\":%.3f,\"a\":", b.index, b.group, b.capsule ? 1 : 0, b.radius);
            line += head;
            append_vector(line, b.a);
            line += ",\"b\":";
            append_vector(line, b.b);
            line += ",\"origin\":";
            append_vector(line, b.origin);
            line += ",\"axis\":[";
            append_vector(line, b.axis[0]);
            line += ',';
            append_vector(line, b.axis[1]);
            line += ',';
            append_vector(line, b.axis[2]);
            line += "],\"mins\":";
            append_vector(line, b.mins);
            line += ",\"maxs\":";
            append_vector(line, b.maxs);
            line += '}';
        }
        line += "]}\n";

        SYSTEMTIME st{};
        GetLocalTime(&st);
        wchar_t name[64];
        swprintf_s(name, L"\\shots_%04u%02u%02u.jsonl", st.wYear, st.wMonth, st.wDay);
        const std::wstring path = log_directory() + name;
        FILE* file = nullptr;
        if (_wfopen_s(&file, path.c_str(), L"ab") || !file)
            return;
        std::fwrite(line.data(), 1, line.size(), file);
        std::fclose(file);
    }

    void part_text(const pending& p, char* out, std::size_t size)
    {
        out[0] = '\0';
        if (p.predicted_box < 0 || p.predicted_box >= p.shot.boxes.count)
            return;
        const hitbox::box& b = p.shot.boxes.boxes[p.predicted_box];
        if (b.capsule)
            std::snprintf(out, size, " | %s r%.2f t%.2f", group_name(b.group), p.predicted_local.y, p.predicted_local.x);
        else
            std::snprintf(out, size, " | %s %.2f %.2f %.2f", group_name(b.group), p.predicted_local.x, p.predicted_local.y, p.predicted_local.z);
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
        char part[96];
        char real[64];
        match_impact(p);
        part_text(p, part, sizeof(part));
        impact_text(p, real, sizeof(real));
        const int server = server_group(p);
        const int group = server >= 0 ? server : infer_group(p, static_cast<float>(dealt));
        std::snprintf(text, sizeof(text), "Hit %s in %s for %d (%d left) | %s %s %.0f, hc %.0f%%%s%s%s", p.shot.player.name, group_name(group), dealt, std::max(0, remaining),
            p.shot.manual ? "manual" : "aimed", group_name(p.shot.group), p.shot.damage, p.shot.hitchance, p.shot.backtrack_tick > 0 ? ", backtrack" : "", part, real);
        push(true, text);
        write_record(p, true, "hit", dealt, remaining, group, server);
        p.active = false;
    }

    void resolve_miss(pending& p, reason r)
    {
        char text[shots::text_size];
        char part[96];
        char real[64];
        match_impact(p);
        if (p.has_impact && r != reason::local_died && r != reason::target_died)
            r = p.impact_box < 0 ? reason::spread : p.impact_blocked ? reason::occlusion : reason::animation;
        part_text(p, part, sizeof(part));
        impact_text(p, real, sizeof(real));
        std::snprintf(text, sizeof(text), "Missed %s due to %s | %s %s %.0f, hc %.0f%%%s%s%s%s", p.shot.player.name, reason_name(r), p.shot.manual ? "manual" : "aimed", group_name(p.shot.group), p.shot.damage,
            p.shot.hitchance, p.shot.backtrack_tick > 0 ? ", backtrack" : "", p.shot.nospread ? ", nospread" : "", part, real);
        push(false, text);
        write_record(p, false, reason_name(r), 0, p.shot.player.health, -1, -1);
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
        next.weapon_def = ctx.def;
        next.inaccuracy = ctx.inaccuracy;
        next.spread = ctx.spread;
        next.local_velocity = systems::g_local.get().velocity;
        math::vector3 direction{};
        if (spread::bullet(ctx, shot.view, shot.recoil, shot.tick, direction))
        {
            next.has_direction = true;
            next.direction = direction;
            float distance = 0.f;
            const int index = hitbox::nearest(shot.boxes, shot.eye, direction, ctx.range, distance);
            next.distance = distance;
            if (index < 0)
                next.predicted = reason::spread;
            else
            {
                const math::vector3 end = shot.eye + direction * distance;
                next.predicted_box = index;
                next.predicted_point = end;
                next.predicted_local = hitbox::describe(shot.boxes.boxes[index], hitbox::core(shot.boxes.boxes[index], shot.eye, direction, distance));
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

    int command_tick(systems::input::usercmd& cmd, int tick_base, int& index)
    {
        const int count = cmd.history_size();
        index = -1;
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

    bool find_target(const weapon_context& ctx, std::uintptr_t local_pawn, const math::vector3& direction, fired& out)
    {
        const auto players = systems::g_entities.players();
        bool found = false;
        float best_distance = ctx.range;
        float best_angle = miss_angle;
        for (const systems::entities::player& player : players)
        {
            if (!detail::valid_target(player, local_pawn, false))
                continue;
            const math::vector3 to = (math::vector3{ player.origin.x, player.origin.y, player.origin.z + target_height } - ctx.eye).normalized();
            if (to.dot(direction) < candidate_cos)
                continue;
            if (!hitbox::collect(player.pawn, g_search))
                continue;
            float distance = 0.f;
            const int index = hitbox::nearest(g_search, ctx.eye, direction, ctx.range, distance);
            if (index >= 0)
            {
                if (distance >= best_distance)
                    continue;
                best_distance = distance;
                best_angle = -1.f;
                out.player = player;
                out.boxes = g_search;
                out.group = g_search.boxes[index].group;
                out.damage = detail::damage(ctx, player, out.group, distance);
                found = true;
                continue;
            }
            if (best_angle < 0.f)
                continue;
            for (int i = 0; i < g_search.count; ++i)
            {
                const hitbox::box& b = g_search.boxes[i];
                const math::vector3 dir = (b.center - ctx.eye).normalized();
                const float angle = std::acos(std::clamp(dir.dot(direction), -1.f, 1.f));
                if (angle < best_angle)
                {
                    best_angle = angle;
                    out.player = player;
                    out.boxes = g_search;
                    out.group = b.group;
                    out.damage = 0.f;
                    found = true;
                }
            }
        }
        if (found)
            out.slot = out.player.index;
        return found;
    }

    void on_command(systems::input::usercmd& cmd, bool aimbot)
    {
        const weapon_context& ctx = g_shared.ctx();
        const std::uintptr_t local_pawn = systems::g_local.get().pawn;
        if (g_manual.active)
        {
            if (!ctx.valid || ctx.weapon != g_manual.weapon)
                g_manual = {};
            else if (ctx.clip < g_manual.clip)
            {
                on_fire(g_manual.shot, ctx, local_pawn);
                g_manual = {};
            }
            else if (++g_manual.ticks > confirm_ticks)
                g_manual = {};
        }
        if (aimbot || g_manual.active || !settings::g_misc.shot_file || !cmd || !local_pawn)
            return;
        if (!ctx.valid || !ctx.gun || !ctx.can_fire || ctx.clip <= 0 || (cmd.buttons() & cstypes::command_buttons::in_attack) == 0)
            return;
        int index = -1;
        const int tick = command_tick(cmd, ctx.tick_base, index);
        math::qangle view{};
        if (tick <= 0 || !(index >= 0 ? cmd.history_angles(index, view) : cmd.base_angles(view)) || !view.is_valid())
            return;
        view.z = 0.f;
        fired shot{};
        shot.manual = true;
        shot.view = view;
        shot.tick = tick;
        shot.eye = ctx.eye;
        shot.recoil = detail::recoil(ctx);
        math::vector3 direction{};
        if (!spread::bullet(ctx, view, shot.recoil, tick, direction) || !find_target(ctx, local_pawn, direction, shot))
            return;
        g_manual.active = true;
        g_manual.clip = ctx.clip;
        g_manual.ticks = 0;
        g_manual.weapon = ctx.weapon;
        g_manual.shot = shot;
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
        g_manual = {};
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
