#pragma once
#include "../core/math.h"
#include <cstdint>
#include <mutex>

namespace systems
{
    namespace local_player
    {
        struct data
        {
            std::uintptr_t controller = 0;
            std::uintptr_t pawn = 0;
            std::uint32_t pawn_handle = 0xFFFFFFFF;
            int index = 0;
            int team = 0;
            int health = 0;
            bool is_alive = false;
            std::uint8_t move_type = 0;
            std::uint32_t flags = 0;
            math::vector3 origin{};
            math::vector3 eye{};
            math::vector3 velocity{};
            std::uintptr_t weapon = 0;
            std::uint32_t weapon_handle = 0xFFFFFFFF;
            std::uint16_t weapon_def = 0;
            std::uintptr_t weapon_vdata = 0;
            int weapon_type = -1;
            int tick_base = 0;
        };
    }

    class local_system
    {
    public:
        local_player::data get() const;
        void update();
        bool is_in_cinematic() const;
        int index() const;

    private:
        mutable std::mutex m_mutex;
        local_player::data m_data{};
    };

    inline local_system g_local{};
}
