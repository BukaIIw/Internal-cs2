#pragma once
#include "misc.h"
#include "input.h"
#include "core/usercmd.h"

namespace ragebot
{
    inline bool enabled = false;
    inline misc::Bind key{};
    inline bool silent = true;
    inline bool autofire = true;
    inline float fov = 180.f;
    inline bool prediction = true;
    inline int extraTicks = 0;
    inline int lockTicks = 12;
    inline float pointScale = 0.8f;
    inline int density = 2;
    inline int minDamage = 1;

    struct Debug
    {
        bool locked = false;
        int group = -1;
        int points = 0;
        int traces = 0;
        float speed = 0.f;
        float lead = 0.f;
        float damage = 0.f;
        bool fired = false;
    };
    inline Debug debug{};

    void Register();
    void OnFrameStage(int stage);
    void OnInput(void* input, subtick::Input* in, const float* punch);
    bool OnCmd(usercmd::Cmd* cmd);
    bool Active();
}
