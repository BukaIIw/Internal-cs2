#pragma once
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace hooks
{
    enum class state : std::uint8_t
    {
        missing,
        created,
        enabled,
        failed
    };

    struct info
    {
        std::string name;
        std::uintptr_t target = 0;
        state status = state::missing;
        bool required = false;
    };

    inline std::atomic<bool> unloading{ false };
    inline std::atomic<bool> cleaned{ false };

    bool initialize();
    void shutdown();
    std::vector<info> snapshot();
    void* device();
    void* window();
}
