#pragma once

namespace visuals
{
    inline bool esp = false;
    inline bool box = true;
    inline bool name = true;
    inline bool health = true;
    inline bool weapon = true;
    inline bool distance = false;
    inline bool skeleton = false;
    inline bool snaplines = false;
    inline bool teammates = false;
    inline float visibleColor[4]{ 1.f, 0.35f, 0.35f, 1.f };
    inline float hiddenColor[4]{ 0.55f, 0.55f, 1.f, 1.f };
    inline float teamColor[4]{ 0.35f, 1.f, 0.45f, 1.f };

    inline bool fovCircle = false;

    bool Any();
    void Render();
    void Cleanup();
}
