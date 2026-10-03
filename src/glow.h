#pragma once

namespace glow
{
    inline bool enabled = false;
    inline bool teammates = false;
    inline bool byVisibility = true;
    inline float enemyColor[4]{ 1.f, 0.2f, 0.2f, 1.f };
    inline float hiddenColor[4]{ 0.6f, 0.3f, 1.f, 1.f };
    inline float teamColor[4]{ 0.2f, 1.f, 0.3f, 1.f };

    void OnFrameStage(int stage);
    void Cleanup();
}
