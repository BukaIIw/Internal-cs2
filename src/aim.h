#pragma once
#include "misc.h"
#include "input.h"

namespace aim
{
    inline bool legit = false;
    inline misc::Bind legitKey{ 1, false, misc::Hold };
    inline float legitFov = 4.f;
    inline float legitSmooth = 6.f;
    inline int legitHitbox = 0;
    inline bool legitRcs = true;

    inline bool trigger = false;
    inline misc::Bind triggerKey{ 6, false, misc::Hold };
    inline int triggerDelay = 0;
    inline bool triggerHead = true;
    inline bool triggerNeck = true;
    inline bool triggerChest = true;
    inline bool triggerStomach = true;
    inline bool triggerArms = true;
    inline bool triggerLegs = true;
    inline int triggerMinDamage = 1;

    inline bool teammates = false;

    bool Any();
    bool Held(const misc::Bind& bind);
    void OnInput(void* input, subtick::Input* in, const float* punch);
}
