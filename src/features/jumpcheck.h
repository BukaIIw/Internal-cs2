#pragma once
#include "../input.h"
#include <cstdint>

class JumpCheck
{
public:
    struct Info
    {
        bool valid = false;
        bool ground = false;
        bool precision = false;
        float groundZ = 0.f;
        float fraction = -1.f;
        int landedTick = 0;
        float landedFrac = 0.f;
        int pressTick = 0;
        float pressFrac = 0.f;
    };

    inline static bool enabled = false;
    inline static bool traceGround = true;
    inline static float lead = 0.f;

    static bool Init();
    static bool Ready();
    static void Register();

    static float LandingFraction(void* pawn, Info& info);
    static bool Run(subtick::Input* in, bool held);

    inline static Info last{};
};
