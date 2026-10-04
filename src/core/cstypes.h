#pragma once
#include <cstdint>

namespace cstypes
{
    constexpr float tick_interval = 1.f / 64.f;
    constexpr int max_players = 64;

    namespace command_buttons
    {
        constexpr std::uint64_t in_attack = 1ull << 0;
        constexpr std::uint64_t in_jump = 1ull << 1;
        constexpr std::uint64_t in_duck = 1ull << 2;
        constexpr std::uint64_t in_forward = 1ull << 3;
        constexpr std::uint64_t in_back = 1ull << 4;
        constexpr std::uint64_t in_use = 1ull << 5;
        constexpr std::uint64_t in_turnleft = 1ull << 7;
        constexpr std::uint64_t in_turnright = 1ull << 8;
        constexpr std::uint64_t in_moveleft = 1ull << 9;
        constexpr std::uint64_t in_moveright = 1ull << 10;
        constexpr std::uint64_t in_attack2 = 1ull << 11;
        constexpr std::uint64_t in_reload = 1ull << 13;
        constexpr std::uint64_t in_sprint = 1ull << 16;
        constexpr std::uint64_t in_speed = in_sprint;
        constexpr std::uint64_t in_move = in_forward | in_back | in_moveleft | in_moveright;
    }

    namespace entity_flags
    {
        constexpr std::uint32_t on_ground = 1u << 0;
        constexpr std::uint32_t ducking = 1u << 1;
    }

    namespace move_type
    {
        constexpr std::uint8_t none = 0;
        constexpr std::uint8_t walk = 2;
        constexpr std::uint8_t fly = 3;
        constexpr std::uint8_t noclip = 7;
        constexpr std::uint8_t observer = 8;
        constexpr std::uint8_t ladder = 9;
    }

    namespace frame_stage
    {
        constexpr int start = 0;
        constexpr int net_update_start = 1;
        constexpr int net_update_postdataupdate_start = 2;
        constexpr int net_update_postdataupdate_end = 3;
        constexpr int net_update_end = 4;
        constexpr int render_start = 5;
        constexpr int render_end = 6;
        constexpr int update = 7;
    }

    namespace hitgroup
    {
        constexpr int generic = 0;
        constexpr int head = 1;
        constexpr int chest = 2;
        constexpr int stomach = 3;
        constexpr int left_arm = 4;
        constexpr int right_arm = 5;
        constexpr int left_leg = 6;
        constexpr int right_leg = 7;
        constexpr int neck = 8;
        constexpr int gear = 10;
    }

    namespace team
    {
        constexpr int spectator = 1;
        constexpr int t = 2;
        constexpr int ct = 3;
    }

    namespace weapon_type
    {
        constexpr int knife = 0;
        constexpr int pistol = 1;
        constexpr int smg = 2;
        constexpr int rifle = 3;
        constexpr int shotgun = 4;
        constexpr int sniper = 5;
        constexpr int machinegun = 6;
        constexpr int c4 = 7;
        constexpr int taser = 8;
        constexpr int grenade = 9;
        constexpr int equipment = 10;
    }

    namespace weapon_id
    {
        constexpr int deagle = 1;
        constexpr int awp = 9;
        constexpr int g3sg1 = 11;
        constexpr int negev = 28;
        constexpr int taser = 31;
        constexpr int ssg08 = 40;
        constexpr int knife_ct = 42;
        constexpr int knife_t = 59;
        constexpr int scar20 = 38;
        constexpr int revolver = 64;
    }

    namespace masks
    {
        constexpr std::uint64_t shot = 0x1C300B;
        constexpr std::uint64_t wallbang = 0x1C100B;
        constexpr std::uint64_t world = 0x1001;
    }
}
