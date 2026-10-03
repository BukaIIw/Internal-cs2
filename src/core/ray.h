#pragma once
#include "../game.h"
#include <cstdint>

namespace ray
{
    constexpr uint64_t kMaskShot = 0x1C300B;
    constexpr uint64_t kMaskWorld = 0x1001;

    struct Hit
    {
        void* entity = nullptr;
        float fraction = 1.f;
        game::Vec3 end{};
    };

    struct Stats
    {
        uint64_t traces = 0;
        uint64_t faults = 0;
    };

    bool Init();
    bool Ready();
    bool Trace(const game::Vec3& from, const game::Vec3& to, uint32_t skipHandle, uint64_t mask, Hit& out);
    bool Clear(const game::Vec3& from, const game::Vec3& to, uint32_t skipHandle, void* target);
    game::Vec3 Forward(float pitch, float yaw, float length);
    Stats Snapshot();
}
