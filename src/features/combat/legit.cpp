#include "combat.h"
#include "combat_detail.h"
#include "hitbox.h"
#include "../../core/keys.h"
#include "../../core/math.h"
#include "../../core/settings.h"
#include "../../systems/entities.h"
#include "../../systems/local.h"
#include "../../systems/tracing.h"
#include "../../systems/view.h"
#include <algorithm>
#include <cstdint>

namespace
{
    using namespace features::combat;

    constexpr int legit_priority = 1;

    struct aim_box
    {
        float fov = 0.f;
        math::vector3 center{};
    };

    hitbox::set g_boxes{};

    bool best_box(const systems::entities::player& player, std::uintptr_t local_pawn, const math::vector3& eye, const math::qangle& reference, aim_box& out)
    {
        const settings::combat::legit& cfg = settings::g_legit;
        if (!hitbox::collect(player.pawn, g_boxes))
            return false;

        aim_box boxes[hitbox::max_boxes]{};
        int count = 0;
        for (int i = 0; i < g_boxes.count && count < hitbox::max_boxes; ++i)
        {
            const hitbox::box& box = g_boxes.boxes[i];
            if ((box.bit & cfg.hitboxes) == 0 || !box.center.is_valid())
                continue;
            const float fov = math::helpers::angle_fov(reference, math::helpers::calc_angle(eye, box.center));
            if (fov > cfg.fov)
                continue;
            boxes[count++] = { fov, box.center };
        }
        if (count == 0)
            return false;

        std::sort(boxes, boxes + count, [](const aim_box& a, const aim_box& b) { return a.fov < b.fov; });
        for (int i = 0; i < count; ++i)
        {
            if (cfg.visible_only && !systems::g_tracing.is_visible(local_pawn, player.pawn, eye, boxes[i].center))
                continue;
            out = boxes[i];
            return true;
        }
        return false;
    }
}

namespace features::combat
{
    void legit::on_create_move(systems::input::frame& frame)
    {
        const settings::combat::legit& cfg = settings::g_legit;
        if (!frame.valid() || !keys::active(cfg.enabled, cfg.key))
            return;

        const weapon_context& ctx = g_shared.ctx();
        if (!ctx.valid || !ctx.gun || !ctx.eye.is_valid())
            return;
        if (cfg.visible_only && !systems::g_tracing.ready())
            return;
        const systems::local_player::data local = systems::g_local.get();
        if (!local.is_alive || !local.pawn)
            return;

        const math::qangle view = systems::g_view.original();
        const math::qangle recoil = cfg.rcs ? detail::recoil(ctx) * cfg.rcs_scale : math::qangle{};
        const math::qangle reference = math::helpers::sanitized(view + recoil);

        bool found = false;
        aim_box best{};
        const auto players = systems::g_entities.players();
        for (const systems::entities::player& player : players)
        {
            if (!detail::valid_target(player, local.pawn, cfg.teammates))
                continue;
            if (!detail::in_fov_range(reference, ctx.eye, player.origin, cfg.fov))
                continue;
            aim_box next{};
            if (!best_box(player, local.pawn, ctx.eye, reference, next))
                continue;
            if (!found || next.fov < best.fov)
            {
                best = next;
                found = true;
            }
        }
        if (!found)
            return;

        const math::qangle target = math::helpers::calc_angle(ctx.eye, best.center) - recoil;
        math::qangle delta = target - view;
        math::helpers::normalize_angles(delta);
        const float smooth = std::max(1.f, cfg.smooth);
        systems::g_view.aim(math::helpers::sanitized(view + delta * (1.f / smooth)), false, legit_priority, "legit");
    }
}
