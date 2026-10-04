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
    constexpr int compensation_iterations = 4;
    constexpr float max_offset = 1.f;

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
        return (PATTERN(patterns::get_tick_view_angles) || PATTERN(patterns::spread_seed)) && (PATTERN(patterns::weapon_calculate_spread) || PATTERN(patterns::calc_spread));
    }

    bool compensate(const weapon_context& ctx, const math::qangle& desired, const math::qangle& recoil, int tick, math::qangle& out)
    {
        std::uintptr_t seed_function = PATTERN(patterns::get_tick_view_angles);
        if (!seed_function)
            seed_function = PATTERN(patterns::spread_seed);
        std::uintptr_t calc_function = PATTERN(patterns::weapon_calculate_spread);
        if (!calc_function)
            calc_function = PATTERN(patterns::calc_spread);
        if (!ctx.valid || !seed_function || !calc_function || tick <= 0 || !desired.is_valid() || !recoil.is_valid())
            return false;

        const math::qangle target = math::helpers::sanitized(desired);
        math::vector3 forward{};
        math::vector3 right{};
        math::vector3 up{};
        math::helpers::angle_vectors(target, forward, right, up);
        const int bullets = std::clamp(ctx.bullets, 1, max_bullets);
        math::qangle current = math::helpers::sanitized(target - recoil);

        for (int i = 0; i < compensation_iterations; ++i)
        {
            std::uint32_t seed = 0;
            if (!call_seed(seed_function, current, tick, seed))
                return false;
            float x[max_bullets]{};
            float y[max_bullets]{};
            if (!call_calc_spread(calc_function, ctx.def, bullets, ctx.mode, seed + 1, ctx.inaccuracy, ctx.spread, ctx.recoil_index, x, y))
                return false;
            if (!std::isfinite(x[0]) || !std::isfinite(y[0]) || std::fabs(x[0]) > max_offset || std::fabs(y[0]) > max_offset)
                return false;
            const math::vector3 anti = (forward - right * x[0] - up * y[0]).normalized();
            if (anti.is_zero())
                return false;
            const math::qangle next = math::helpers::sanitized(math::helpers::vector_angles(anti) - recoil);
            std::uint32_t check = 0;
            if (!call_seed(seed_function, next, tick, check))
                return false;
            if (check == seed)
            {
                out = next;
                return true;
            }
            current = next;
        }
        return false;
    }
}
