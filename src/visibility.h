#pragma once
#include "game.h"

namespace visibility
{
    void OnFrameStage(int stage);
    bool Visible(void* pawn);
    bool Point(void* pawn, const game::Vec3& eye, const game::Vec3& point);
    void Cleanup();
}
