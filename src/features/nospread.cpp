#include "nospread.h"
#include "vecmath.h"
#include "../core/settings.h"
#include <cmath>

namespace
{
    using namespace vm;

    int Bucket(float a)
    {
        return static_cast<int>(std::floor(a * 2.f));
    }
}

void NoSpread::Register()
{
    settings::Bool("nospread.enabled", &enabled);
    settings::Float("nospread.max_search", &maxSearch);
    settings::Int("nospread.iterations", &iterations);
}

bool NoSpread::Ready()
{
    return Spread::Ready() && Spread::SeedReady();
}

bool NoSpread::Apply(const Spread::Weapon& w, const Spread::Shot& target, float& pitch, float& yaw)
{
    lastFound = false;
    if (!Ready())
        return false;
    const float limit = maxSearch < 1.f ? 1.f : maxSearch > 45.f ? 45.f : maxSearch;
    const int steps = iterations < 1 ? 1 : iterations > 8 ? 8 : iterations;
    float maxDeg = std::atan(w.inaccuracy + w.spread) / kRad + 1.f;
    maxDeg = maxDeg > limit ? limit : maxDeg;
    const int p0 = Bucket(target.pitch - maxDeg), p1 = Bucket(target.pitch + maxDeg);
    const int y0 = Bucket(target.yaw - maxDeg), y1 = Bucket(target.yaw + maxDeg);

    const int tick = Spread::Tick(target.tick, w.attack);
    float bestError = 1e9f;
    for (int i = p0; i <= p1; ++i)
    {
        const float cp = i * 0.5f + 0.25f;
        if (cp < -89.f || cp > 89.f)
            continue;
        for (int j = y0; j <= y1; ++j)
        {
            const float cy = NormalizeYaw(j * 0.5f + 0.25f);
            const uint32_t seed = Spread::Seed(cp, cy, tick);
            float x, y;
            if (Spread::Offsets(w, seed, &x, &y, 1) != 1)
                return false;
            float candP = target.pitch, candY = target.yaw;
            for (int k = 0; k < steps; ++k)
            {
                const float bp = candP + target.punchPitch, by = candY + target.punchYaw;
                float rp, ry;
                Angles(Spread::Direction(bp, by, x, y), rp, ry);
                candP = target.pitch - (rp - bp);
                candY = NormalizeYaw(target.yaw - NormalizeYaw(ry - by));
            }
            if (candP < -89.f || candP > 89.f)
                continue;
            if (Bucket(candP) != Bucket(cp) || Bucket(NormalizeYaw(candY)) != Bucket(cy))
                continue;
            const float dp = candP - target.pitch, dy = NormalizeYaw(candY - target.yaw);
            const float error = dp * dp + dy * dy;
            if (error < bestError)
            {
                bestError = error;
                pitch = candP;
                yaw = candY;
                lastSeed = seed;
                lastFound = true;
            }
        }
    }
    lastError = lastFound ? std::sqrt(bestError) : 0.f;
    return lastFound;
}
