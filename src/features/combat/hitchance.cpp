#include "hitchance.h"
#include "combat.h"
#include "combat_detail.h"
#include "spread.h"
#include "../../systems/tracing.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace features::combat::hitchance
{
    namespace
    {
        using vec2_t = math::vector2;
        using vec3_t = math::vector3;

        static_assert(spread::seed_count == 64);

        struct hitchance_data_t
        {
            float damage[64];
            std::uint32_t hit_count;
            std::uint32_t lethal_hit_mask[2];
            std::uint32_t player_hit_mask[2];
            vec3_t forward;
            vec3_t right;
            vec3_t up;
        };

        struct scan_settings_t
        {
            float minimum_damage = 0.f;
            float target_health = 0.f;
        };

        struct aim_point_t
        {
            vec3_t position{};
            const vec3_t* shoot_position = nullptr;
            const scan_settings_t* scan_settings = nullptr;
        };

        struct hitchance_context_t
        {
            vec2_t spread_seeds[64]{};
            int hit_mask_mode = 1;
            const request* input = nullptr;
        };
    }

    static std::uint32_t count_set_bits(std::uint32_t value)
    {
        return static_cast<std::uint32_t>(std::popcount(value));
    }

    static vec3_t normalize(const vec3_t& value)
    {
        return value.normalized();
    }

    static bool should_use_player_hit_mask(const aim_point_t* aim_point)
    {
        return aim_point->scan_settings->minimum_damage >= aim_point->scan_settings->target_health;
    }

    struct seed_geometry
    {
        int index = -1;
        float distance = 0.f;
        vec3_t direction{};
    };

    static int project_seeds(hitchance_context_t* context, aim_point_t* aim_point, float spread_scale, hitchance_data_t* data, seed_geometry (&out)[64])
    {
        const request& in = *context->input;
        const vec3_t& shoot = *aim_point->shoot_position;
        math::helpers::angle_vectors(math::helpers::calc_angle(shoot, aim_point->position), data->forward, data->right, data->up);
        int hits = 0;
        for (std::size_t seed = 0; seed < 64; ++seed)
        {
            const vec2_t offset = context->spread_seeds[seed] * spread_scale;
            seed_geometry& g = out[seed];
            g.direction = normalize(data->forward + data->right * offset.x + data->up * offset.y);
            g.index = hitbox::nearest(*in.boxes, shoot, g.direction, in.range, g.distance);
            if (g.index >= 0)
                ++hits;
        }
        return hits;
    }

    static float seed_damage(const request& in, const vec3_t& shoot, const seed_geometry& g, bool traced)
    {
        if (g.index < 0)
            return 0.f;
        const weapon_context& weapon = g_shared.ctx();
        if (!traced)
            return detail::damage(weapon, *in.target, in.boxes->boxes[g.index].group, g.distance);
        const vec3_t end = shoot + g.direction * g.distance;
        if (in.penetration)
        {
            const systems::tracing::bullet_result bullet = systems::g_tracing.fire_bullet(in.local, in.target->pawn, shoot, end, true);
            return bullet.ok && bullet.hit_target ? bullet.damage : 0.f;
        }
        if (!systems::g_tracing.is_visible(in.local, in.target->pawn, shoot, end))
            return 0.f;
        return detail::damage(weapon, *in.target, in.boxes->boxes[g.index].group, g.distance);
    }

    static void record_seed(hitchance_data_t* data, aim_point_t* aim_point, std::size_t seed, float damage)
    {
        if (!(damage > 0.f))
            return;
        const std::uint32_t bit = 1u << (seed & 31);
        data->damage[seed] = damage;
        data->player_hit_mask[seed >> 5] |= bit;
        if (damage >= aim_point->scan_settings->target_health)
            data->lethal_hit_mask[seed >> 5] |= bit;
        else
            ++data->hit_count;
    }

    static void run_hitchance_traces(hitchance_context_t* context, aim_point_t* aim_point, float spread_scale, hitchance_data_t* data, int needed = -1, bool full_only = false)
    {
        const request& in = *context->input;
        const vec3_t& shoot = *aim_point->shoot_position;
        seed_geometry geometry[64]{};
        const int reachable = project_seeds(context, aim_point, spread_scale, data, geometry);
        bool traced = !(needed >= 0 && reachable < needed) && !(full_only && reachable < 64);
        int hits = 0;
        int misses = 0;
        for (std::size_t seed = 0; seed < 64; ++seed)
        {
            if (traced && needed >= 0 && (hits >= needed || misses > 64 - needed))
                break;
            const float damage = seed_damage(in, shoot, geometry[seed], traced);
            if (damage >= aim_point->scan_settings->minimum_damage)
                ++hits;
            else
            {
                ++misses;
                if (full_only)
                    traced = false;
            }
            record_seed(data, aim_point, seed, damage);
        }
    }

static float calculate_hitchance(
    const hitchance_data_t& data,
    float minimum_damage) {
    std::uint32_t valid_seed_count = 0;

    for (std::size_t seed = 0; seed < 64; ++seed) {
        if (data.damage[seed] >= minimum_damage)
            ++valid_seed_count;
    }

    return static_cast<float>(valid_seed_count) / 64.0f;
}

static bool hit_chance(
    hitchance_context_t* context,
    aim_point_t* aim_point,
    float spread_scale) {
    hitchance_data_t data{};
    run_hitchance_traces(context, aim_point, spread_scale, &data, -1, true);

    data.hit_count +=
        count_set_bits(data.lethal_hit_mask[0]) +
        count_set_bits(data.lethal_hit_mask[1]);

    if (data.hit_count == 0)
        return false;

    const float hitchance = calculate_hitchance(
        data,
        aim_point->scan_settings->minimum_damage);

    if (hitchance >= 1.0f)
        return true;

    const std::uint32_t* hit_mask = data.lethal_hit_mask;

    if (context->hit_mask_mode == 1 && should_use_player_hit_mask(aim_point))
        hit_mask = data.player_hit_mask;

    const float minimum_damage = aim_point->scan_settings->minimum_damage;
    const float target_health = aim_point->scan_settings->target_health;
    const float damage_ratio = minimum_damage / target_health;
    const float damage_limit =
        minimum_damage * (1.0f + std::max(0.0f, damage_ratio - 1.0f) * 2.5f);

    vec3_t direction_sum{};
    float damage_sum = 0.0f;

    for (std::size_t seed = 0; seed < 64; ++seed) {
        if ((hit_mask[seed >> 5] & (1u << (seed & 31))) == 0)
            continue;

        const vec2_t spread = context->spread_seeds[seed];
        const vec3_t direction = normalize({
            data.forward.x + spread.x * data.right.x + spread.y * data.up.x,
            data.forward.y + spread.x * data.right.y + spread.y * data.up.y,
            data.forward.z + spread.x * data.right.z + spread.y * data.up.z,
        });

        const float seed_damage = std::min(damage_limit, data.damage[seed]);

        direction_sum.x += direction.x * seed_damage;
        direction_sum.y += direction.y * seed_damage;
        direction_sum.z += direction.z * seed_damage;
        damage_sum += seed_damage;
    }

    if (damage_sum <= 0.0f)
        return false;

    const vec3_t average_direction{
        direction_sum.x / damage_sum,
        direction_sum.y / damage_sum,
        direction_sum.z / damage_sum,
    };

    const vec3_t aim_delta{
        aim_point->position.x - aim_point->shoot_position->x,
        aim_point->position.y - aim_point->shoot_position->y,
        aim_point->position.z - aim_point->shoot_position->z,
    };

    const float distance_along_ray =
        aim_delta.x * average_direction.x +
        aim_delta.y * average_direction.y +
        aim_delta.z * average_direction.z;

    aim_point->position = {
        aim_point->shoot_position->x + average_direction.x * distance_along_ray,
        aim_point->shoot_position->y + average_direction.y * distance_along_ray,
        aim_point->shoot_position->z + average_direction.z * distance_along_ray,
    };

    return false;
}

static bool force_shot(
    hitchance_context_t* context,
    aim_point_t* aim_point,
    int iteration_count,
    float minimum_spread_scale) {
    iteration_count = std::clamp(iteration_count, 1, 3);

    if (iteration_count == 1)
        return hit_chance(context, aim_point, 1.0f);

    const float divisor = static_cast<float>(iteration_count - 1);
    const float spread_scale_range = 1.0f - minimum_spread_scale;

    for (int iteration = 0; iteration < iteration_count; ++iteration) {
        const float spread_scale =
            1.0f - spread_scale_range * static_cast<float>(iteration) / divisor;

        if (hit_chance(context, aim_point, spread_scale))
            return true;
    }

    return false;
}

    namespace
    {
        constexpr int memo_slots = 4;
        constexpr int memo_ticks = 16;
        constexpr float memo_distance = 0.05f;
        constexpr float memo_spread = 1e-5f;

        struct memo_entry
        {
            bool valid = false;
            int tick = 0;
            std::uintptr_t pawn = 0;
            int box_count = 0;
            vec3_t box_center{};
            vec3_t shoot{};
            vec3_t point{};
            float inaccuracy = 0.f;
            float spread = 0.f;
            float minimum_damage = 0.f;
            float target_health = 0.f;
            float threshold = 0.f;
            bool penetration = false;
            bool force_shot = false;
            int force_shot_iterations = 0;
            float force_shot_min_spread = 0.f;
            result out{};
        };

        memo_entry g_memo[memo_slots]{};
        int g_memo_next = 0;

        memo_entry make_key(const request& in, float threshold)
        {
            const weapon_context& weapon = g_shared.ctx();
            memo_entry key{};
            key.valid = true;
            key.tick = weapon.tick_base;
            key.pawn = in.target->pawn;
            key.box_count = in.boxes->count;
            key.box_center = in.boxes->boxes[0].center;
            key.shoot = in.shoot;
            key.point = in.point;
            key.inaccuracy = weapon.inaccuracy;
            key.spread = weapon.spread;
            key.minimum_damage = in.minimum_damage;
            key.target_health = in.target_health;
            key.threshold = threshold;
            key.penetration = in.penetration;
            key.force_shot = in.force_shot;
            key.force_shot_iterations = in.force_shot_iterations;
            key.force_shot_min_spread = in.force_shot_min_spread;
            return key;
        }

        bool same(const memo_entry& a, const memo_entry& b)
        {
            return a.valid && b.valid && a.pawn == b.pawn && a.box_count == b.box_count
                && b.tick >= a.tick && b.tick - a.tick <= memo_ticks
                && a.box_center.distance(b.box_center) < memo_distance && a.shoot.distance(b.shoot) < memo_distance && a.point.distance(b.point) < memo_distance
                && std::fabs(a.inaccuracy - b.inaccuracy) < memo_spread && std::fabs(a.spread - b.spread) < memo_spread
                && a.minimum_damage == b.minimum_damage && a.target_health == b.target_health && a.threshold == b.threshold
                && a.penetration == b.penetration && a.force_shot == b.force_shot && a.force_shot_iterations == b.force_shot_iterations && a.force_shot_min_spread == b.force_shot_min_spread;
        }

        result compute(const request& in, float threshold)
        {
            result out{};
            out.point = in.point;
            hitchance_context_t context{};
            spread::table(g_shared.ctx(), context.spread_seeds);
            context.hit_mask_mode = 1;
            context.input = &in;
            const scan_settings_t settings{ std::max(1.f, in.minimum_damage), in.target_health };
            aim_point_t aim_point{ in.point, &in.shoot, &settings };

            if (in.force_shot && force_shot(&context, &aim_point, in.force_shot_iterations, in.force_shot_min_spread))
            {
                out.pass = true;
                out.chance = 1.f;
                out.point = aim_point.position;
                return out;
            }

            const int needed = std::clamp(static_cast<int>(std::ceil(threshold * 64.f - 0.001f)), 0, 64);
            hitchance_data_t data{};
            run_hitchance_traces(&context, &aim_point, 1.0f, &data, needed);
            out.chance = calculate_hitchance(data, settings.minimum_damage);
            out.pass = out.chance >= threshold;
            out.point = aim_point.position;
            return out;
        }
    }

    result evaluate(const request& in, float threshold)
    {
        result out{};
        out.point = in.point;
        if (!in.boxes || in.boxes->count <= 0 || !in.local || !in.target || !in.target->pawn || !(in.target_health > 0.f) || !in.point.is_valid() || !in.shoot.is_valid())
            return out;

        memo_entry key = make_key(in, threshold);
        for (const memo_entry& entry : g_memo)
        {
            if (same(entry, key))
                return entry.out;
        }
        key.out = compute(in, threshold);
        g_memo[g_memo_next] = key;
        g_memo_next = (g_memo_next + 1) % memo_slots;
        return key.out;
    }

    int seed_hit(const request& in, const math::qangle& view, const math::qangle& recoil, int tick)
    {
        if (!in.boxes || in.boxes->count <= 0 || !in.local || !in.target || !in.target->pawn || !in.shoot.is_valid())
            return -1;
        math::vector3 direction{};
        if (!spread::bullet(g_shared.ctx(), view, recoil, tick, direction))
            return -1;
        seed_geometry geometry{};
        geometry.direction = direction;
        geometry.index = hitbox::nearest(*in.boxes, in.shoot, direction, in.range, geometry.distance);
        return seed_damage(in, in.shoot, geometry, true) >= std::max(1.f, in.minimum_damage) ? 1 : 0;
    }

    void reset()
    {
        for (memo_entry& entry : g_memo)
            entry = {};
    }
}
