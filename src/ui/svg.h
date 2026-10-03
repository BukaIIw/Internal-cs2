#pragma once
#include "imgui.h"

namespace svg
{
    void Stroke(ImDrawList* dl, const char* path, ImVec2 pos, float size, ImU32 color, float thickness = 1.8f);
    void Fill(ImDrawList* dl, const char* path, ImVec2 pos, float size, ImU32 color);
}
