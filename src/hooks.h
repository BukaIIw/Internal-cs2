#pragma once
#include <atomic>

namespace hooks
{
    inline std::atomic<bool> unload{ false };
    inline std::atomic<bool> cleaned{ false };
    bool Init();
    void Shutdown();
}
