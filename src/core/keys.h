#pragma once
#include <atomic>
#include <cstdint>

namespace keys
{
    enum class mode : int
    {
        toggle,
        hold,
        always,
        off
    };

    struct bind
    {
        int key = 0;
        mode type = mode::hold;
        bool toggled = false;
        const char* name = nullptr;
    };

    inline bind* capturing = nullptr;

    void register_bind(bind* b, const char* name);
    bool active(const bind& b);
    bool active(bool enabled, const bind& b);
    bool down(int vk);
    bool on_key(int vk, bool down);
    const char* key_name(int vk);
    int count();
    bind* at(int index);
}
