#include "globals.h"
#include "local.h"
#include "../core/addresses.h"
#include "../core/cstypes.h"
#include "../core/memory.h"

namespace
{
    constexpr std::uintptr_t tick_count_offset = 0x44;
}

namespace systems
{
    std::uintptr_t globals_system::get() const
    {
        return addresses::globals::global_vars();
    }

    int globals_system::tick_count() const
    {
        const std::uintptr_t globals = get();
        if (!globals || !memory::is_readable(globals + tick_count_offset, sizeof(int)))
            return 0;
        const int tick = memory::read<int>(globals + tick_count_offset);
        return tick > 0 ? tick : 0;
    }

    float globals_system::interval() const
    {
        return cstypes::tick_interval;
    }

    bool globals_system::in_game() const
    {
        return g_local.get().controller != 0;
    }

    bool globals_system::connected() const
    {
        return g_local.get().controller != 0;
    }
}
