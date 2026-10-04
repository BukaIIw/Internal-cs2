#pragma once
#include <cstdint>

namespace systems
{
    class globals_system
    {
    public:
        std::uintptr_t get() const;
        int tick_count() const;
        float interval() const;
        bool in_game() const;
        bool connected() const;
    };

    inline globals_system g_globals{};
}
