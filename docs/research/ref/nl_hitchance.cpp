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
    run_hitchance_traces(context, aim_point, spread_scale, &data);

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
