#pragma once
#include <cstdint>

struct ImDrawList;

namespace features::misc
{
    class thirdperson
    {
    public:
        void on_create_move();
        void on_override_view(std::uintptr_t client_mode, std::uintptr_t view_setup);
        bool on_camera_think(std::uintptr_t input, int slot);
        void after_camera_think(std::uintptr_t input, int slot);
        void restore();

    private:
        std::uintptr_t m_pawn = 0;
        bool m_applied = false;
        bool m_cheats_saved = false;
        std::uint8_t m_cheats = 0;
        float m_distance = 0.f;
    };

    class overlay
    {
    public:
        void on_present(ImDrawList* draw);
        bool wants_frame() const;
    };

    inline thirdperson g_thirdperson{};
    inline overlay g_overlay{};
}
