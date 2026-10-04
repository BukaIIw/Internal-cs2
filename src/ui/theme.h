#pragma once
#include "imgui.h"

namespace ui::theme
{
    enum class token : int
    {
        background,
        rail,
        surface,
        border,
        control,
        control_hover,
        text,
        text_dim,
        text_faint,
        danger,
        good,
        warn,
        accent,
        on_accent,
        team_t,
        team_ct,
        stattrak,
        shadow,
        white,
        count
    };

    constexpr int accent_preset_count = 6;
    extern const float accent_presets[accent_preset_count][3];

    ImU32 raw(token t);
    ImU32 color(token t, float alpha = 1.f);
    ImU32 accent(float alpha = 1.f);
    ImU32 mix(ImU32 a, ImU32 b, float t);
    ImU32 mul_alpha(ImU32 c, float alpha);
    ImU32 with_alpha(ImU32 c, float alpha);
    ImU32 faded(ImU32 c);
    ImVec4 vec(ImU32 c);

    float dpi_scale();
    void refresh(bool force = false);
}
