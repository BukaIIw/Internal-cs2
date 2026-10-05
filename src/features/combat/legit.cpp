#include "combat.h"
#include "combat_detail.h"
#include "hitbox.h"
#include "../../core/cstypes.h"
#include "../../core/keys.h"
#include "../../core/math.h"
#include "../../core/settings.h"
#include "../../systems/entities.h"
#include "../../systems/local.h"
#include "../../systems/tracing.h"
#include "../../systems/view.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace
{
    using namespace features::combat;

    constexpr int legit_priority = 1;
    constexpr float linear_max_step = 10.f;
    constexpr float min_speed = 0.01f;
    constexpr float min_speed_drop = 0.97f;
    constexpr float max_jitter = 0.18f;
    constexpr int random_interval = 6;
    constexpr float visible_scale = 0.5f;

    std::uint32_t g_rng = 0x9E3779B9u;

    float random01()
    {
        g_rng ^= g_rng << 13;
        g_rng ^= g_rng >> 17;
        g_rng ^= g_rng << 5;
        return static_cast<float>(g_rng & 0xFFFFFFu) / static_cast<float>(0xFFFFFFu);
    }

    struct aim_box
    {
        float fov = 0.f;
        math::vector3 center{};
    };

    hitbox::set g_boxes{};

    bool best_box(const settings::combat::legit& cfg, const systems::entities::player& player, std::uintptr_t local_pawn, const math::vector3& eye, const math::qangle& reference, aim_box& out)
    {
        if (!hitbox::collect(player.pawn, g_boxes))
            return false;

        struct entry
        {
            float fov = 0.f;
            int box = -1;
        };
        entry boxes[hitbox::max_boxes]{};
        int count = 0;
        for (int i = 0; i < g_boxes.count && count < hitbox::max_boxes; ++i)
        {
            const hitbox::box& box = g_boxes.boxes[i];
            if ((box.bit & cfg.hitboxes) == 0 || !box.center.is_valid())
                continue;
            const float fov = math::helpers::angle_fov(reference, math::helpers::calc_angle(eye, box.center));
            if (fov > cfg.fov)
                continue;
            boxes[count++] = { fov, i };
        }
        if (count == 0)
            return false;

        std::sort(boxes, boxes + count, [](const entry& a, const entry& b) { return a.fov < b.fov; });
        for (int i = 0; i < count; ++i)
        {
            const hitbox::box& box = g_boxes.boxes[boxes[i].box];
            if (!cfg.visible_only || systems::g_tracing.is_visible(local_pawn, player.pawn, eye, box.center))
            {
                out = { boxes[i].fov, box.center };
                return true;
            }
            math::vector3 points[hitbox::max_points]{};
            const int total = hitbox::points(box, eye, true, visible_scale, visible_scale, points, hitbox::max_points);
            for (int p = 1; p < total; ++p)
            {
                if (!points[p].is_valid() || !systems::g_tracing.is_visible(local_pawn, player.pawn, eye, points[p]))
                    continue;
                out = { math::helpers::angle_fov(reference, math::helpers::calc_angle(eye, points[p])), points[p] };
                return true;
            }
        }
        return false;
    }
}

namespace features::combat
{
    void legit::on_create_move(systems::input::frame& frame)
    {
        const settings::combat::legit& global = settings::g_legit;
        const weapon_context& ctx = g_shared.ctx();
        if (!frame.valid() || !global.enabled || !ctx.valid || !ctx.gun || !ctx.eye.is_valid())
        {
            m_last_recoil = {};
            return;
        }
        const systems::local_player::data local = systems::g_local.get();
        if (!local.is_alive || !local.pawn)
        {
            m_last_recoil = {};
            return;
        }

        const settings::combat::legit& cfg = settings::legit_for(ctx.group);
        const math::qangle recoil_now = detail::recoil(ctx);
        const bool rcs_weapon = ctx.full_auto && (ctx.group == settings::combat::wg_rifle || ctx.group == settings::combat::wg_smg || ctx.group == settings::combat::wg_machinegun);
        const bool spraying = cfg.rcs && rcs_weapon && ctx.shots_fired >= 1 && frame.really_held(cstypes::command_buttons::in_attack);
        const math::qangle recoil_delta = spraying ? recoil_now - m_last_recoil : math::qangle{};
        m_last_recoil = recoil_now;

        const math::qangle view = systems::g_view.original();
        const math::qangle recoil = spraying ? recoil_now * cfg.rcs_scale : math::qangle{};
        const math::qangle reference = math::helpers::sanitized(view + recoil);

        bool found = false;
        aim_box best{};
        if (keys::active(global.key) && (!cfg.visible_only || systems::g_tracing.ready()))
        {
            const auto players = systems::g_entities.players();
            for (const systems::entities::player& player : players)
            {
                if (!detail::valid_target(player, local.pawn, cfg.teammates))
                    continue;
                if (cfg.spotted_only && !player.spotted)
                    continue;
                if (!detail::in_fov_range(reference, ctx.eye, player.origin, cfg.fov))
                    continue;
                aim_box next{};
                if (!best_box(cfg, player, local.pawn, ctx.eye, reference, next))
                    continue;
                if (!found || next.fov < best.fov)
                {
                    best = next;
                    found = true;
                }
            }
        }

        if (!found)
        {
            if (spraying && recoil_delta.is_valid() && (recoil_delta.x != 0.f || recoil_delta.y != 0.f))
                systems::g_view.aim(math::helpers::sanitized(view - recoil_delta * cfg.rcs_scale), false, legit_priority, "rcs");
            return;
        }

        const bool randomize = cfg.randomization > 0 && keys::active(global.random_key) && (global.random_key.key != 0 || global.random_key.type == keys::mode::always);
        const float amount = std::clamp(static_cast<float>(cfg.randomization) / 100.f, 0.f, 1.f);
        if (randomize)
        {
            if (--m_random_ticks <= 0)
            {
                m_random_ticks = random_interval;
                m_speed_scale = 1.f - amount * min_speed_drop * random01();
                m_jitter = { (random01() - 0.5f) * 2.f * max_jitter * amount, (random01() - 0.5f) * 2.f * max_jitter * amount, 0.f };
            }
        }
        else
        {
            m_speed_scale = 1.f;
            m_jitter = {};
            m_random_ticks = 0;
        }

        const math::qangle target = math::helpers::calc_angle(ctx.eye, best.center) - recoil;
        math::qangle delta = target - view;
        math::helpers::normalize_angles(delta);
        delta.z = 0.f;

        const float speed = std::clamp(cfg.speed / 100.f, min_speed, 1.f);
        const float factor = std::clamp(speed * m_speed_scale, min_speed * 0.1f, 1.f);
        math::qangle step = delta;
        if (speed < 1.f || factor < 1.f)
        {
            if (cfg.speed_mode == 0)
            {
                const float length = std::sqrt(delta.x * delta.x + delta.y * delta.y);
                const float limit = factor * linear_max_step;
                if (length > limit && length > 0.f)
                    step = delta * (limit / length);
            }
            else
            {
                step = delta * factor;
            }
        }
        if (randomize)
            step = step + m_jitter;
        systems::g_view.aim(math::helpers::sanitized(view + step), false, legit_priority, "legit");
    }
}
