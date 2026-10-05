#pragma once
#include "../core/math.h"
#include <array>
#include <cstdint>
#include <mutex>

namespace systems
{
    namespace entities
    {
        struct player
        {
            bool valid = false;
            int index = 0;
            std::uintptr_t controller = 0;
            std::uintptr_t pawn = 0;
            std::uint32_t pawn_handle = 0xFFFFFFFF;
            int team = 0;
            int health = 0;
            int armor = 0;
            bool helmet = false;
            bool alive = false;
            bool dormant = true;
            bool enemy = false;
            bool visible = false;
            bool spotted = false;
            math::vector3 origin{};
            math::vector3 velocity{};
            math::vector3 mins{};
            math::vector3 maxs{};
            char name[32]{};
            std::uint16_t weapon_def = 0;
        };
    }

    class entity_system
    {
    public:
        std::uintptr_t system() const;
        std::uintptr_t identity(int index) const;
        std::uintptr_t get(int index) const;
        std::uintptr_t lookup(std::uint32_t handle) const;
        std::uint32_t handle_of(int index) const;
        const char* get_designer_name(std::uintptr_t entity) const;
        const char* get_designer_name(int index) const;
        const char* get_schema_name(std::uintptr_t entity) const;
        int highest_index() const;

        void update(bool visibility, bool teammates);
        std::array<entities::player, 65> players() const;
        entities::player player_by_pawn(std::uintptr_t pawn) const;

    private:
        mutable std::mutex m_mutex;
        std::array<entities::player, 65> m_players{};
    };

    inline entity_system g_entities{};
}
