#include "combat.h"
#include "combat_detail.h"
#include "spread.h"
#include "../../core/addresses.h"
#include "../../core/convar.h"
#include "../../core/cstypes.h"
#include "../../core/hash.h"
#include "../../core/memory.h"
#include "../../core/patterns.h"
#include "../../core/schema.h"
#include "../../core/settings.h"
#include "../../systems/game_reads.h"
#include "../../systems/local.h"
#include "../../systems/prediction.h"
#include <Windows.h>
#include <algorithm>
#include <cmath>

namespace
{
    namespace reads = systems::reads;

    constexpr float default_recoil_scale = 2.f;
    constexpr float max_recoil_scale = 10.f;
    constexpr float max_punch = 90.f;
    constexpr int weapon_mode_count = 2;
    constexpr float default_range = 8192.f;
    constexpr float default_headshot = 4.f;
    constexpr float default_max_speed = 250.f;
    constexpr float max_damage = 1000.f;
    constexpr float max_recoil_index = 1000.f;
    constexpr float coarse_center_height = 36.f;
    constexpr float coarse_radius = 48.f;
    constexpr float full_fov = 180.f;
    constexpr float range_distance_unit = 500.f;
    constexpr float stomach_scale = 1.25f;
    constexpr float leg_scale = 0.75f;
    constexpr float armor_bonus = 0.5f;
    constexpr float max_convar_scale = 10.f;

    float sane_float(float value, float low, float high, float fallback)
    {
        return std::isfinite(value) && value >= low && value <= high ? value : fallback;
    }

    float mode_float(std::uintptr_t vdata, std::uint32_t offset, int mode, float fallback)
    {
        if (!vdata || !offset)
            return fallback;
        return reads::value<float>(vdata + offset + static_cast<std::uint32_t>(mode) * sizeof(float), fallback);
    }

    bool call_inaccuracy(std::uintptr_t function, std::uintptr_t weapon, float& out)
    {
        __try
        {
            out = memory::call<float>(function, weapon, std::uintptr_t{ 0 }, std::uintptr_t{ 0 });
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool call_spread(std::uintptr_t function, std::uintptr_t weapon, float& out)
    {
        __try
        {
            out = memory::call<float>(function, weapon);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool call_aim_punch(std::uintptr_t function, std::uintptr_t services, float* out)
    {
        __try
        {
            memory::call<std::uintptr_t>(function, services, out, true);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    float convar_scale(const convars::convar* cvar)
    {
        const float value = cvar->get<float>();
        return cvar->value && std::isfinite(value) && value > 0.f && value <= max_convar_scale ? value : 1.f;
    }

    bool sane_punch(const math::qangle& punch)
    {
        return punch.is_valid() && std::fabs(punch.x) < max_punch && std::fabs(punch.y) < max_punch;
    }

    float read_inaccuracy(std::uintptr_t weapon)
    {
        float value = 0.f;
        if (const std::uintptr_t function = PATTERN(patterns::get_inaccuracy))
        {
            if (call_inaccuracy(function, weapon, value) && std::isfinite(value) && value >= 0.f && value <= 1.f)
                return value;
        }
        return sane_float(reads::field<float>(weapon, SCHEMA("C_CSWeaponBase", "m_fAccuracyPenalty"_hash)), 0.f, 1.f, 0.f);
    }

    float read_spread(std::uintptr_t weapon, std::uintptr_t vdata, int mode)
    {
        float value = 0.f;
        if (const std::uintptr_t function = PATTERN(patterns::get_spread))
        {
            if (call_spread(function, weapon, value) && std::isfinite(value) && value >= 0.f && value <= 1.f)
                return value;
        }
        return sane_float(mode_float(vdata, SCHEMA("CCSWeaponBaseVData", "m_flSpread"_hash), mode, 0.f), 0.f, 1.f, 0.f);
    }

    math::qangle read_punch(std::uintptr_t pawn)
    {
        const std::uintptr_t services = reads::field_pointer(pawn, SCHEMA("C_CSPlayerPawn", "m_pAimPunchServices"_hash));
        std::uintptr_t function = PATTERN(patterns::get_aim_punch_fn);
        if (!function)
            function = PATTERN(patterns::get_aim_punch);
        if (services && function)
        {
            float values[3]{};
            if (call_aim_punch(function, services, values))
            {
                const math::qangle punch{ values[0], values[1], 0.f };
                if (sane_punch(punch))
                    return punch;
            }
        }
        math::qangle punch = reads::field<math::qangle>(pawn, SCHEMA("C_CSPlayerPawn", "m_aimPunchAngle"_hash));
        punch.z = 0.f;
        return sane_punch(punch) ? punch : math::qangle{};
    }
}

namespace features::combat
{
    void shared::update()
    {
        weapon_context ctx{};
        const systems::local_player::data local = systems::g_local.get();
        const systems::prediction::state& pre = systems::g_prediction.pre();
        ctx.eye = pre.valid && pre.eye.is_valid() ? pre.eye : local.eye;
        if (!local.is_alive || !local.pawn || !local.weapon || !local.weapon_vdata)
        {
            m_ctx = ctx;
            return;
        }

        const std::uintptr_t weapon = local.weapon;
        const std::uintptr_t vdata = local.weapon_vdata;
        ctx.weapon = weapon;
        ctx.vdata = vdata;
        ctx.def = local.weapon_def;
        ctx.group = settings::combat::weapon_group_of(ctx.def);

        const int type = reads::field<int>(vdata, SCHEMA("CCSWeaponBaseVData", "m_WeaponType"_hash), local.weapon_type);
        ctx.type = type >= cstypes::weapon_type::knife && type <= cstypes::weapon_type::equipment ? type : local.weapon_type;
        ctx.gun = ctx.type >= cstypes::weapon_type::pistol && ctx.type <= cstypes::weapon_type::machinegun;
        ctx.full_auto = reads::field<std::uint8_t>(vdata, SCHEMA("CCSWeaponBaseVData", "m_bIsFullAuto"_hash)) != 0;
        ctx.needs_scope = ctx.type == cstypes::weapon_type::sniper;
        ctx.scoped = reads::field<std::uint8_t>(local.pawn, SCHEMA("C_CSPlayerPawn", "m_bIsScoped"_hash)) != 0;

        ctx.mode = std::clamp(reads::field<int>(weapon, SCHEMA("C_CSWeaponBase", "m_weaponMode"_hash)), 0, weapon_mode_count - 1);
        ctx.recoil_index = sane_float(reads::field<float>(weapon, SCHEMA("C_CSWeaponBase", "m_flRecoilIndex"_hash)), 0.f, max_recoil_index, 0.f);
        ctx.bullets = std::clamp(reads::field<int>(vdata, SCHEMA("CCSWeaponBaseVData", "m_nNumBullets"_hash), 1), 1, spread::max_bullets);
        ctx.damage = sane_float(static_cast<float>(reads::field<int>(vdata, SCHEMA("CCSWeaponBaseVData", "m_nDamage"_hash))), 0.f, max_damage, 0.f);
        ctx.range = sane_float(reads::field<float>(vdata, SCHEMA("CCSWeaponBaseVData", "m_flRange"_hash), default_range), 1.f, reads::world_limit, default_range);
        ctx.range_modifier = sane_float(reads::field<float>(vdata, SCHEMA("CCSWeaponBaseVData", "m_flRangeModifier"_hash), 1.f), 0.01f, 1.f, 1.f);
        ctx.penetration = sane_float(reads::field<float>(vdata, SCHEMA("CCSWeaponBaseVData", "m_flPenetration"_hash), 1.f), 0.f, 10.f, 1.f);
        ctx.armor_ratio = sane_float(reads::field<float>(vdata, SCHEMA("CCSWeaponBaseVData", "m_flArmorRatio"_hash), 1.f), 0.f, 2.f, 1.f);
        ctx.headshot_multiplier = sane_float(reads::field<float>(vdata, SCHEMA("CCSWeaponBaseVData", "m_flHeadshotMultiplier"_hash), default_headshot), 0.f, 10.f, default_headshot);
        ctx.max_speed = sane_float(mode_float(vdata, SCHEMA("CCSWeaponBaseVData", "m_flMaxSpeed"_hash), ctx.mode, default_max_speed), 1.f, max_damage, default_max_speed);

        ctx.clip = reads::field<int>(weapon, SCHEMA("C_BasePlayerWeapon", "m_iClip1"_hash));
        const int next_attack = reads::field<int>(weapon, SCHEMA("C_BasePlayerWeapon", "m_nNextPrimaryAttackTick"_hash));
        ctx.reloading = reads::field<std::uint8_t>(weapon, SCHEMA("C_CSWeaponBase", "m_bInReload"_hash)) != 0;
        const int tick_base = pre.valid && pre.tick_base > 0 ? pre.tick_base : local.tick_base;
        ctx.ticks_to_fire = std::max(0, next_attack - tick_base);
        ctx.can_fire = ctx.gun && ctx.clip > 0 && next_attack <= tick_base && !ctx.reloading;
        ctx.shots_fired = std::max(0, reads::field<int>(local.pawn, SCHEMA("C_CSPlayerPawn", "m_iShotsFired"_hash)));

        const convars::convar* nospread = CONVAR("weapon_accuracy_nospread");
        if (nospread->value && nospread->get<bool>())
        {
            ctx.inaccuracy = 0.f;
            ctx.spread = 0.f;
        }
        else
        {
            ctx.inaccuracy = read_inaccuracy(weapon);
            ctx.spread = read_spread(weapon, vdata, ctx.mode);
        }
        ctx.punch = read_punch(local.pawn);
        ctx.valid = true;
        m_ctx = ctx;
    }
}

namespace features::combat::detail
{
    bool valid_target(const systems::entities::player& player, std::uintptr_t local_pawn, bool teammates)
    {
        if (!player.valid || !player.alive || player.dormant || !player.pawn || player.pawn == local_pawn || player.health <= 0)
            return false;
        if (!teammates && !player.enemy)
            return false;
        return reads::field<std::uint8_t>(player.pawn, SCHEMA("C_CSPlayerPawn", "m_bGunGameImmunity"_hash)) == 0;
    }

    bool in_fov_range(const math::qangle& reference, const math::vector3& eye, const math::vector3& origin, float fov)
    {
        if (fov >= full_fov)
            return true;
        const math::vector3 center{ origin.x, origin.y, origin.z + coarse_center_height };
        const float distance = std::max(eye.distance(center), 1.f);
        const float allowance = math::rad2deg(std::atan2(coarse_radius, distance));
        return math::helpers::angle_fov(reference, math::helpers::calc_angle(eye, center)) - allowance <= fov;
    }

    float recoil_scale()
    {
        const float value = CONVAR("weapon_recoil_scale")->get<float>();
        return std::isfinite(value) && value > 0.f && value <= max_recoil_scale ? value : default_recoil_scale;
    }

    math::qangle recoil(const weapon_context& ctx)
    {
        return ctx.punch * recoil_scale();
    }

    float damage(const weapon_context& ctx, const systems::entities::player& target, int hitgroup, float distance)
    {
        if (!ctx.valid || !(ctx.damage > 0.f) || !std::isfinite(distance) || distance < 0.f || distance > ctx.range)
            return 0.f;
        float value = ctx.damage * std::pow(ctx.range_modifier, distance / range_distance_unit);
        const bool ct = target.team == cstypes::team::ct;
        const bool head = hitgroup == cstypes::hitgroup::head;
        const bool leg = hitgroup == cstypes::hitgroup::left_leg || hitgroup == cstypes::hitgroup::right_leg;
        if (head)
            value *= ctx.headshot_multiplier * convar_scale(ct ? CONVAR("mp_damage_scale_ct_head") : CONVAR("mp_damage_scale_t_head"));
        else
            value *= convar_scale(ct ? CONVAR("mp_damage_scale_ct_body") : CONVAR("mp_damage_scale_t_body"));
        if (hitgroup == cstypes::hitgroup::stomach)
            value *= stomach_scale;
        else if (leg)
            value *= leg_scale;

        const bool armored = target.armor > 0 && (head ? target.helmet : !leg);
        if (armored)
        {
            float reduced = value * ctx.armor_ratio * armor_bonus;
            if ((value - reduced) * armor_bonus > static_cast<float>(target.armor))
                reduced = value - static_cast<float>(target.armor) / armor_bonus;
            value = reduced;
        }
        return std::isfinite(value) && value > 0.f ? value : 0.f;
    }
}
