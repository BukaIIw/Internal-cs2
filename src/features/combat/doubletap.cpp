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
    constexpr int charge_window = 16;

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

    int shoot_tick(const weapon_ticks& t)
    {
        double whole = 0.0;
        const double frac = static_cast<double>(t.ratio) + std::modf(static_cast<double>(t.wat_offset), &whole);
        int tick = t.next_attack + static_cast<int>(whole);
        if (frac >= 0.0)
        {
            if (frac >= 1.0)
                ++tick;
        }
        else
            --tick;
        return tick;
    }

    void write_all(systems::input::usercmd& cmd, int count, int tick, int from = 0)
    {
        for (int i = from; i < count; ++i)
            cmd.set_history_player_tick(i, tick, 0.f);
    }

    bool active()
    {
        const settings::combat::rage& cfg = settings::g_rage;
        return cfg.enabled && keys::active(cfg.doubletap, cfg.doubletap_key);
    }
}

namespace features::combat
{
    bool doubletap::charged(const weapon_context& ctx) const
    {
        if (!active() || !ctx.valid || !ctx.gun || ctx.def == cstypes::weapon_id::revolver || ctx.clip <= 0 || ctx.reloading)
            return false;
        return m_second > 0;
    }

    void doubletap::on_create_move_post(systems::input::usercmd& cmd)
    {
        const settings::combat::rage& cfg = settings::g_rage;
        if (!cmd || !active())
        {
            reset();
            return;
        }
        const weapon_context& ctx = g_shared.ctx();
        const systems::local_player::data local = systems::g_local.get();
        if (!ctx.valid || !ctx.gun || ctx.def == cstypes::weapon_id::revolver || !local.weapon)
            return;
        const int count = cmd.history_size();
        if (count <= 0)
            return;
        if (m_second > 0)
            --m_second;

        const bool firing = systems::g_view.firing();
        if (m_release)
        {
            m_release = false;
            if (!firing && !systems::g_view.user_attack())
            {
                cmd.buttons() &= ~attack;
                cmd.buttons_changed() |= attack;
            }
        }

        const weapon_ticks ticks = read_ticks(local.weapon);
        if (ticks.next_attack <= 0)
            return;

        if (cfg.doubletap_mode == alternate)
        {
            const bool shift = (m_shots % 2) != 0;
            if (cmd.attack1_index() > -1)
            {
                ++m_shots;
                m_second = (m_shots % 2) != 0 ? charge_window : 0;
                m_release = true;
            }
            write_all(cmd, count, shift ? shoot_tick(ticks) - 1 : 0);
            cmd.set_attack1_index(-1);
            return;
        }

        if (!firing)
            return;
        const bool second = m_second > 0;
        if (cfg.doubletap_mode == split && count >= 2)
        {
            write_all(cmd, count, ticks.next_attack, 1);
            cmd.set_attack1_index(0);
        }
        else
        {
            write_all(cmd, count, ticks.next_attack);
            cmd.set_attack1_index(-1);
        }
        m_second = second ? 0 : charge_window;
        m_release = true;
    }

    void doubletap::reset()
    {
        m_release = false;
        m_shots = 0;
        m_second = 0;
    }
}
