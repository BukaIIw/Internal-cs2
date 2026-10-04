#include "spread.h"
#include "../../core/addresses.h"
#include "../../core/cstypes.h"
#include "../../core/memory.h"
#include "../../core/patterns.h"
#include <Windows.h>
#include <algorithm>
#include <cmath>

namespace
{
    constexpr int weapon_mode_secondary = 1;
    constexpr float negev_recoil_limit = 3.f;
    constexpr float max_offset = 0.5f;
    constexpr float bucket_size = 0.5f;
    constexpr float min_pitch_cos = 0.05f;
    constexpr int max_bucket_range = 24;
    constexpr int circle_steps = 96;
    constexpr float cell_slack = 0.02f;
    constexpr float min_alignment = 0.9999995f;

    class ran1
    {
    public:
        void seed(std::int32_t value)
        {
            m_idum = value < 0 ? value : -value;
            m_iy = 0;
        }

        float next(float low, float high)
        {
            float value = static_cast<float>(am * generate());
            if (value > rnmx)
                value = static_cast<float>(rnmx);
            return value * (high - low) + low;
        }

    private:
        static constexpr std::int32_t ia = 16807;
        static constexpr std::int32_t im = 2147483647;
        static constexpr std::int32_t iq = 127773;
        static constexpr std::int32_t ir = 2836;
        static constexpr int ntab = 32;
        static constexpr std::int32_t ndiv = 1 + (im - 1) / ntab;
        static constexpr double am = 1.0 / im;
        static constexpr double rnmx = 1.0 - 1.2e-7;

        std::int32_t generate()
        {
            std::int32_t k = 0;
            if (m_idum <= 0 || !m_iy)
            {
                m_idum = -m_idum < 1 ? 1 : -m_idum;
                for (int j = ntab + 7; j >= 0; --j)
                {
                    k = m_idum / iq;
                    m_idum = ia * (m_idum - k * iq) - ir * k;
                    if (m_idum < 0)
                        m_idum += im;
                    if (j < ntab)
                        m_iv[j] = m_idum;
                }
                m_iy = m_iv[0];
            }
            k = m_idum / iq;
            m_idum = ia * (m_idum - k * iq) - ir * k;
            if (m_idum < 0)
                m_idum += im;
            const int j = std::clamp(m_iy / ndiv, 0, ntab - 1);
            m_iy = m_iv[j];
            m_iv[j] = m_idum;
            return m_iy;
        }

        std::int32_t m_idum = 0;
        std::int32_t m_iy = 0;
        std::int32_t m_iv[ntab]{};
    };

    float shape(std::uint16_t def, int mode, float recoil_index, float r)
    {
        if (def == cstypes::weapon_id::revolver && mode == weapon_mode_secondary)
            return 1.f - r * r;
        if (def == cstypes::weapon_id::negev && recoil_index < negev_recoil_limit)
        {
            for (int j = 3; static_cast<float>(j) > recoil_index; --j)
                r *= r;
            return 1.f - r;
        }
        return r;
    }

    bool call_seed(std::uintptr_t function, const math::qangle& angles, int tick, std::uint32_t& out)
    {
        __try
        {
            out = memory::call<std::uint32_t>(function, std::uintptr_t{ 0 }, &angles.x, tick);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool call_calc_spread(std::uintptr_t function, std::uint16_t def, int bullets, int mode, std::uint32_t seed, float inaccuracy, float spread, float recoil_index, float* x, float* y)
    {
        __try
        {
            memory::call<void>(function, def, bullets, mode, seed, inaccuracy, spread, recoil_index, x, y);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }
}

namespace features::combat::spread
{
    math::vector2 offset(std::uint16_t def, int mode, float recoil_index, std::int32_t seed, float inaccuracy, float spread)
    {
        ran1 random{};
        random.seed(seed);
        const float r1 = shape(def, mode, recoil_index, random.next(0.f, 1.f));
        const float a1 = random.next(0.f, 2.f * math::pi);
        const float r2 = shape(def, mode, recoil_index, random.next(0.f, 1.f));
        const float a2 = random.next(0.f, 2.f * math::pi);
        return { std::cos(a1) * r1 * inaccuracy + std::cos(a2) * r2 * spread, std::sin(a1) * r1 * inaccuracy + std::sin(a2) * r2 * spread };
    }

    void table(const weapon_context& ctx, math::vector2 (&out)[seed_count])
    {
        for (int i = 0; i < seed_count; ++i)
            out[i] = offset(ctx.def, ctx.mode, ctx.recoil_index, i + 1, ctx.inaccuracy, ctx.spread);
    }

    bool available()
    {
        return PATTERN(patterns::spread_seed) && (PATTERN(patterns::calc_spread) || PATTERN(patterns::weapon_calculate_spread));
    }

    bool compensate(const weapon_context& ctx, const math::qangle& desired, const math::qangle& recoil, int tick, math::qangle& out)
    {
        std::uintptr_t calc_function = PATTERN(patterns::calc_spread);
        if (!calc_function)
            calc_function = PATTERN(patterns::weapon_calculate_spread);
        const std::uintptr_t seed_function = PATTERN(patterns::spread_seed);
        if (!ctx.valid || !seed_function || !calc_function || tick <= 0 || !desired.is_valid() || !recoil.is_valid())
            return false;

        const math::qangle target = math::helpers::sanitized(desired);
        math::vector3 dir{};
        math::vector3 dir_right{};
        math::vector3 dir_up{};
        math::helpers::angle_vectors(target, dir, dir_right, dir_up);
        const math::qangle base = math::helpers::sanitized(target - recoil);
        const float max_theta = std::atan(std::min(ctx.inaccuracy + ctx.spread, max_offset));
        const float pitch_cos = std::max(std::cos(math::deg2rad(base.x)), min_pitch_cos);
        const int pitch_range = std::min(static_cast<int>(math::rad2deg(max_theta) / bucket_size) + 2, max_bucket_range);
        const int yaw_range = std::min(static_cast<int>(math::rad2deg(max_theta) / (bucket_size * pitch_cos)) + 2, max_bucket_range);
        const float base_pitch = std::round(base.x / bucket_size) * bucket_size;
        const float base_yaw = std::round(base.y / bucket_size) * bucket_size;
        const int bullets = std::clamp(ctx.bullets, 1, max_bullets);

        for (int ring = 0; ring <= std::max(pitch_range, yaw_range); ++ring)
        {
            for (int dp = -ring; dp <= ring; ++dp)
            {
                for (int dy = -ring; dy <= ring; ++dy)
                {
                    if (std::max(std::abs(dp), std::abs(dy)) != ring || std::abs(dp) > pitch_range || std::abs(dy) > yaw_range)
                        continue;
                    const math::qangle cell{ base_pitch + static_cast<float>(dp) * bucket_size, math::helpers::normalized_angle(base_yaw + static_cast<float>(dy) * bucket_size), 0.f };
                    if (cell.x < -89.f || cell.x > 89.f)
                        continue;
                    std::uint32_t seed = 0;
                    if (!call_seed(seed_function, cell, tick, seed))
                        return false;
                    float x[max_bullets]{};
                    float y[max_bullets]{};
                    if (!call_calc_spread(calc_function, ctx.def, bullets, ctx.mode, seed + 1, ctx.inaccuracy, ctx.spread, ctx.recoil_index, x, y))
                        return false;
                    const float magnitude = std::sqrt(x[0] * x[0] + y[0] * y[0]);
                    if (!std::isfinite(magnitude) || magnitude > max_offset)
                        continue;
                    const float theta = std::atan(magnitude);

                    const float cell_dp = (cell.x + recoil.x - target.x);
                    const float cell_dy = math::helpers::normalized_angle(cell.y + recoil.y - target.y) * pitch_cos;
                    const float half = bucket_size * 0.5f;
                    const float near_p = std::max(0.f, std::fabs(cell_dp) - half);
                    const float near_y = std::max(0.f, std::fabs(cell_dy) - half * pitch_cos);
                    const float far_p = std::fabs(cell_dp) + half;
                    const float far_y = std::fabs(cell_dy) + half * pitch_cos;
                    const float theta_deg = math::rad2deg(theta);
                    if (theta_deg < std::sqrt(near_p * near_p + near_y * near_y) - cell_slack || theta_deg > std::sqrt(far_p * far_p + far_y * far_y) + cell_slack)
                        continue;

                    for (int step = 0; step < circle_steps; ++step)
                    {
                        const float psi = static_cast<float>(step) * (2.f * math::pi / static_cast<float>(circle_steps));
                        const float ap = -std::sin(psi) * theta_deg;
                        const float ay = std::cos(psi) * theta_deg;
                        if (std::fabs(ap - cell_dp) > half + cell_slack || std::fabs(-ay - cell_dy) > half * pitch_cos + cell_slack)
                            continue;
                        const math::vector3 forward = (dir * std::cos(theta) + (dir_up * std::sin(psi) + dir_right * std::cos(psi)) * std::sin(theta)).normalized();
                        math::qangle shot = math::helpers::vector_angles(forward);
                        math::qangle view{ shot.x - recoil.x, math::helpers::normalized_angle(shot.y - recoil.y), 0.f };
                        if (view.x < -89.f || view.x > 89.f)
                            continue;
                        std::uint32_t check = 0;
                        if (!call_seed(seed_function, view, tick, check))
                            return false;
                        if (check != seed)
                            continue;

                        math::vector3 f{};
                        math::vector3 r{};
                        math::vector3 u{};
                        math::helpers::angle_vectors(shot, f, r, u);
                        const float along = dir.dot(f);
                        if (!(along > 0.f))
                            continue;
                        const math::vector3 need = dir / along - f;
                        const float roll = math::rad2deg(std::atan2(y[0], x[0]) - std::atan2(need.dot(u), need.dot(r)));
                        shot.z = math::helpers::normalized_angle(roll);
                        math::helpers::angle_vectors(shot, f, r, u);
                        const math::vector3 bullet = (f + r * x[0] + u * y[0]).normalized();
                        if (bullet.dot(dir) < min_alignment)
                            continue;
                        view.z = shot.z;
                        out = view;
                        return true;
                    }
                }
            }
        }
        return false;
    }
}
