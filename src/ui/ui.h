#pragma once
#include "imgui.h"
#include "metrics.h"
#include "theme.h"
#include "typography.h"
#include "../core/keys.h"
#include <cstddef>
#include <cstdint>

namespace ui
{
    enum class button_kind
    {
        normal,
        primary,
        destructive
    };

    float frame_dt();
    float ease(float t);
    float animate(const char* key, float target, float speed = 14.f);
    float approach(float current, float target, float speed);

    void text(ImDrawList* dl, font_role role, ImVec2 pos, ImU32 color, const char* text, const char* text_end = nullptr);
    void glow(ImDrawList* dl, ImVec2 center, float radius, ImU32 color, float alpha);
    void soft_shadow(ImDrawList* dl, ImVec2 min, ImVec2 max, float rounding, float spread, float alpha);
    void status_dot(ImDrawList* dl, ImVec2 center, ImU32 color);
    void chrome(ImVec2 pos, ImVec2 size);

    void begin_card(const char* title, const char* icon_path = nullptr);
    void end_card();
    bool begin_columns(const char* id, int count = 2);
    void next_column();
    void end_columns();

    bool feature(const char* label, bool* value, keys::bind* bind = nullptr, void (*options)() = nullptr);
    bool toggle(const char* label, bool* value);
    bool slider(const char* label, float* value, float min, float max, const char* format);
    bool slider(const char* label, int* value, int min, int max, const char* format);
    bool combo(const char* label, int* value, const char* const* items, int count);
    bool begin_combo(const char* id, const char* preview, float width);
    bool combo_item(const char* label, bool selected);
    void end_combo();
    bool color_edit(const char* label, float* rgba);
    bool flags(const char* label, std::uint32_t* mask, const char* const* names, const std::uint32_t* bits, int count);
    bool segmented(const char* id, int* value, const char* const* labels, int count, float width);
    bool button(const char* label, ImVec2 size, const char* icon_path = nullptr, button_kind kind = button_kind::normal);
    bool chip(const char* label, bool active);
    bool search_box(const char* id, char* buffer, std::size_t size, const char* hint, float width);
    bool input_int(const char* label, int* value, int min, int max);
    bool input_text(const char* label, char* buffer, std::size_t size, const char* hint);
    bool key_button(const char* label, keys::bind* bind);
    void bind_editor(keys::bind* bind);
    void value(const char* label, const char* format, ...);
    void note(const char* text);
    void divider();

    void toasts();
    bool toasts_active();
}
