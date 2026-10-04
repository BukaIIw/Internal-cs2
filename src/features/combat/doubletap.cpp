#include "combat.h"
#include "../../core/cstypes.h"
#include "../../core/hash.h"
#include "../../core/keys.h"
#include "../../core/schema.h"
#include "../../core/settings.h"
#include "../../systems/game_reads.h"
#include "../../systems/local.h"
#include "../../systems/view.h"
#include <cmath>

namespace
{
    namespace reads = systems::reads;

    enum mode : int
    {
        instant,
        alternate,
        split
    };

    constexpr std::uint64_t attack = cstypes::command_buttons::in_attack;

    struct weapon_ticks
    {
        int next_attack = 0;
        float ratio = 0.f;
        float wat_offset = 0.f;
    };

    weapon_ticks read_ticks(std::uintptr_t weapon)
    {
        weapon_ticks out{};
        out.next_attack = reads::field<int>(weapon, SCHEMA("C_BasePlayerWeapon", "m_nNextPrimaryAttackTick"_hash));
        const float ratio = reads::field<float>(weapon, SCHEMA("C_BasePlayerWeapon", "m_flNextPrimaryAttackTickRatio"_hash));
        const float wat = reads::field<float>(weapon, SCHEMA("C_CSWeaponBase", "m_flWatTickOffset"_hash));
        out.ratio = std::isfinite(ratio) ? ratio : 0.f;
        out.wat_offset = std::isfinite(wat) ? wat : 0.f;
        return out;
    }

    int alternate_tick(const weapon_ticks& t)
    {
        double whole = 0.0;
        const double frac = static_cast<double>(t.ratio) + std::modf(static_cast<double>(t.wat_offset), &whole);
        int tick = t.next_attack + static_cast<int>(whole);
        if (frac >= 1.0)
            ++tick;
        else if (frac < 0.0)
            --tick;
        return tick;
    }

    void write_all(systems::input::usercmd& cmd, int count, int tick, int from = 0)
    {
        for (int i = from; i < count; ++i)
            cmd.set_history_player_tick(i, tick, 0.f);
    }
}

namespace features::combat
{
    void doubletap::on_create_move_post(systems::input::usercmd& cmd)
    {
        const settings::combat::rage& cfg = settings::g_rage;
        if (!cmd || !cfg.enabled || !keys::active(cfg.doubletap, cfg.doubletap_key))
        {
            m_release = false;
            m_shots = 0;
            return;
        }
        const weapon_context& ctx = g_shared.ctx();
        const systems::local_player::data local = systems::g_local.get();
        if (!ctx.valid || !ctx.gun || ctx.def == cstypes::weapon_id::revolver || !local.weapon)
            return;
        const int count = cmd.history_size();
        if (count <= 0)
            return;

        if (m_release)
        {
            m_release = false;
            if (!systems::g_view.firing() && !systems::g_view.user_attack())
            {
                cmd.buttons() &= ~attack;
                cmd.buttons_changed() |= attack;
                return;
            }
        }
        if (!systems::g_view.firing())
            return;

        const weapon_ticks ticks = read_ticks(local.weapon);
        if (ticks.next_attack <= 0)
            return;

        switch (cfg.doubletap_mode)
        {
        case alternate:
        {
            const bool shift = (m_shots % 2) != 0;
            ++m_shots;
            write_all(cmd, count, shift ? alternate_tick(ticks) - 1 : 0);
            cmd.set_attack1_index(-1);
            break;
        }
        case split:
            if (count >= 2)
            {
                write_all(cmd, count, ticks.next_attack, 1);
                cmd.set_attack1_index(0);
                break;
            }
            [[fallthrough]];
        default:
            write_all(cmd, count, ticks.next_attack);
            cmd.set_attack1_index(-1);
            break;
        }
        m_release = true;
    }

    void doubletap::reset()
    {
        m_release = false;
        m_shots = 0;
    }
}
