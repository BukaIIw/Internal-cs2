#pragma once
#include "imgui.h"

namespace ui
{
    enum class font_role : int
    {
        body,
        body_strong,
        compact,
        caption,
        badge,
        title,
        display,
        mono,
        count
    };

    ImFont* font(font_role role);
    float font_size(font_role role);
    ImVec2 text_size(font_role role, const char* text, const char* text_end = nullptr);

    class font_scope
    {
    public:
        explicit font_scope(font_role role);
        ~font_scope();
        font_scope(const font_scope&) = delete;
        font_scope& operator=(const font_scope&) = delete;
    };
}
