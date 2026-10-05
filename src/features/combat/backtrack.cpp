#include "backtrack.h"
#include "combat_detail.h"
#include "../../core/convar.h"
#include "../../core/cstypes.h"
#include "../../core/hash.h"
#include "../../core/schema.h"
#include "../../core/settings.h"
#include "../../systems/entities.h"
#include "../../systems/game_reads.h"
#include "../../systems/local.h"
#include <algorithm>
#include <cmath>

namespace
{
    namespace reads = systems::reads;
    using features::combat::backtrack::max_records;
    using features::combat::backtrack::record;

    constexpr int slots = 65;
    constexpr float default_unlag = 0.2f;
    constexpr float max_unlag = 0.2f;
    constexpr int safety_ticks = 2;
    constexpr float pitch_limit = 89.5f;
    constexpr float realign_interval = 1.32f;
    constexpr float teleport_distance_sqr = 64.f * 64.f;

    struct track
    {
        std::uint32_t handle = 0;
        int head = 0;
        int count = 0;
        float last_realign = 0.f;
        record records[max_records]{};
    };

    track g_tracks[slots]{};

    int unlag_ticks()
    {
        const auto* cvar = CONVAR("sv_maxunlag");
        float value = cvar->value ? cvar->get<float>() : default_unlag;
        if (!std::isfinite(value) || value <= 0.f)
            value = default_unlag;
        value = std::min(value, max_unlag);
        const auto* player = CONVAR("sv_maxunlag_player");
        if (player->value)
        {
            const float limit = player->get<float>();
            if (std::isfinite(limit) && limit > 0.f)
                value = std::min(value, limit);
        }
        return static_cast<int>(value / cstypes::tick_interval);
    }

    float eye_pitch(std::uintptr_t pawn)
    {
        std::uint32_t offset = SCHEMA("C_CSPlayerPawn", "m_angEyeAngles"_hash);
        if (!offset)
            offset = SCHEMA("C_CSPlayerPawnBase", "m_angEyeAngles"_hash);
        if (!offset)
            return 0.f;
        return reads::field<math::qangle>(pawn, offset).x;
    }

    int server_window()
    {
        const auto* cvar = CONVAR("cl_interp");
        float lerp = cvar->value ? cvar->get<float>() : cstypes::tick_interval;
        if (!std::isfinite(lerp) || lerp <= 0.f)
            lerp = cstypes::tick_interval;
        const int ticks = static_cast<int>(std::ceil(lerp / cstypes::tick_interval - 0.01f));
        return std::clamp(ticks, 1, 8) * 2 + 1;
    }

    const record* newest(const track& t)
    {
        if (t.count <= 0)
            return nullptr;
        return &t.records[(t.head + max_records - 1) % max_records];
    }
}

namespace features::combat::backtrack
{
    void update(int tick_base)
    {
        const std::uint32_t sim_offset = SCHEMA("C_BaseEntity", "m_flSimulationTime"_hash);
        if (!sim_offset || tick_base <= 0)
            return;
        const systems::local_player::data local = systems::g_local.get();
        const auto players = systems::g_entities.players();
        for (int slot = 0; slot < slots; ++slot)
        {
            const systems::entities::player& player = players[slot];
            track& t = g_tracks[slot];
            if (!detail::valid_target(player, local.pawn, true))
            {
                t = {};
                continue;
            }
            if (t.handle != player.pawn_handle)
            {
                t = {};
                t.handle = player.pawn_handle;
            }

            const float sim = reads::field<float>(player.pawn, sim_offset);
            if (!std::isfinite(sim) || sim <= 0.f)
                continue;
            const record* last = newest(t);
            if (last && sim <= last->simulation_time)
                continue;

            record& next = t.records[t.head];
            next = {};
            if (!hitbox::collect(player.pawn, next.boxes))
                continue;
            next.simulation_time = sim;
            next.tick = static_cast<int>(std::lround(sim / cstypes::tick_interval));
            next.origin = player.origin;

            const float pitch = eye_pitch(player.pawn);
            bool broken = !std::isfinite(pitch) || std::fabs(pitch) > pitch_limit;
            if (last)
            {
                if (sim > last->simulation_time + realign_interval)
                    t.last_realign = sim;
                if ((next.origin - last->origin).length_sqr() > teleport_distance_sqr)
                {
                    for (record& old : t.records)
                        old.valid = false;
                    t.count = 0;
                }
            }
            if (t.last_realign > 0.f && sim - t.last_realign <= realign_interval)
                broken = true;
            next.pitch_broken = broken;
            next.valid = true;
            t.head = (t.head + 1) % max_records;
            t.count = std::min(t.count + 1, max_records);
        }
    }

    void reset()
    {
        for (track& t : g_tracks)
            t = {};
    }

    bool tick_valid(int tick, int tick_base)
    {
        return tick > 0 && tick_base > 0 && tick_base - tick >= 0 && tick_base - tick <= unlag_ticks() - safety_ticks;
    }

    int collect(int slot, int tick_base, const record** out, int max)
    {
        if (slot < 0 || slot >= slots || !out || max <= 0)
            return 0;
        const track& t = g_tracks[slot];
        const record* latest = newest(t);
        const int floor = settings::g_rage.tick_source == 1 && latest ? latest->tick - server_window() : 0;
        int written = 0;
        for (int i = 1; i <= t.count && written < max; ++i)
        {
            const record& r = t.records[(t.head + max_records - i) % max_records];
            if (!r.valid || !tick_valid(r.tick, tick_base) || r.tick < floor)
                continue;
            out[written++] = &r;
        }
        return written;
    }

    int current_tick(std::uintptr_t pawn)
    {
        if (!pawn)
            return 0;
        const float sim = reads::field<float>(pawn, SCHEMA("C_BaseEntity", "m_flSimulationTime"_hash));
        if (!std::isfinite(sim) || sim <= 0.f)
            return 0;
        return static_cast<int>(std::lround(sim / cstypes::tick_interval));
    }

    bool pitch_broken(int slot)
    {
        if (slot < 0 || slot >= slots)
            return false;
        const record* r = newest(g_tracks[slot]);
        return r && r->pitch_broken;
    }
}
