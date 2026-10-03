#pragma once

namespace hands
{
    inline bool arms = false;
    inline bool weapon = false;
    inline float armsColor[4]{ 0.25f, 0.72f, 1.f, 1.f };
    inline float weaponColor[4]{ 1.f, 1.f, 1.f, 1.f };

    struct Debug
    {
        bool hooked;
        bool viewmodel;
        bool attachment;
        int ownerOffset;
        int ownerKind;
        int candidate;
        unsigned tinted;
        unsigned calls;
        int source;
    };
    inline Debug debug{ false, false, false, -1, 0, -1, 0, 0, 0 };

    void Install();
    void OnFrameStage(int stage);
    void Cleanup();
}
