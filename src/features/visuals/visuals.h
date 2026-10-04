#pragma once
#include "../../core/math.h"
#include <cstdint>

struct ImDrawList;

namespace features::visuals
{
    class esp
    {
    public:
        void on_frame_stage(int stage);
        void on_present(ImDrawList* draw);
    };

    class glow
    {
    public:
        void on_frame_stage(int stage);
        bool override_glow(std::uintptr_t glow_property, bool original) const;
        bool override_color(std::uintptr_t glow_property, float* rgba) const;
        void restore();
    };

    class chams
    {
    public:
        bool on_draw_object(std::uintptr_t desc, std::uintptr_t meshes, int count) const;
    };

    class grenade_prediction
    {
    public:
        void on_frame_stage(int stage);
        void on_present(ImDrawList* draw);
    };

    bool any();

    inline esp g_esp{};
    inline glow g_glow{};
    inline chams g_chams{};
    inline grenade_prediction g_grenade{};
}
