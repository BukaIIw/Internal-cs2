#pragma once
#include "imgui.h"

namespace render
{
    enum Direction
    {
        Vertical,
        Horizontal
    };

    ImVec2 Snap(ImVec2 p);
    ImU32 Fade(ImU32 color, float alpha);
    ImU32 Lerp(ImU32 a, ImU32 b, float t);

    void Rect(ImDrawList* dl, ImVec2 min, ImVec2 max, ImU32 color, float rounding = 0.f);
    void Gradient(ImDrawList* dl, ImVec2 min, ImVec2 max, ImU32 from, ImU32 to, float rounding = 0.f, Direction direction = Vertical);
    void Border(ImDrawList* dl, ImVec2 min, ImVec2 max, ImU32 color, float rounding = 0.f, float thickness = 1.f);
    void Shadow(ImDrawList* dl, ImVec2 min, ImVec2 max, float rounding, float spread, ImU32 color);
    void Glow(ImDrawList* dl, ImVec2 center, float radius, ImU32 color);
    void Circle(ImDrawList* dl, ImVec2 center, float radius, ImU32 color);
    void Text(ImDrawList* dl, ImFont* font, float size, ImVec2 pos, ImU32 color, const char* text);
}
