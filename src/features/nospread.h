#pragma once
#include "spread.h"

class NoSpread
{
public:
    inline static bool enabled = false;
    inline static float maxSearch = 12.f;
    inline static int iterations = 3;

    static void Register();
    static bool Ready();
    static bool Apply(const Spread::Weapon& w, const Spread::Shot& target, float& pitch, float& yaw);

    inline static uint32_t lastSeed = 0;
    inline static float lastError = 0.f;
    inline static bool lastFound = false;
};
