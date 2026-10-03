#pragma once
#include "imgui.h"

namespace misc
{
    struct Bind;
}

namespace ui
{
    inline float accent[4]{ 0.25f, 0.72f, 1.f, 1.f };
    inline bool reduceMotion = false;

    struct Fonts
    {
        ImFont* regular = nullptr;
        ImFont* semibold = nullptr;
    };
    inline Fonts fonts{};

    enum Tone
    {
        Background,
        Rail,
        Surface,
        Border,
        Control,
        ControlHover,
        Text,
        TextDim,
        TextFaint,
        Danger,
        Good,
        Warn
    };

    enum ButtonKind
    {
        Normal,
        Primary,
        Destructive
    };

    void LoadFonts();
    void ApplyStyle();

    ImU32 Color(Tone tone, float alpha = 1.f);
    ImU32 Accent(float alpha = 1.f);
    ImU32 Mix(ImU32 a, ImU32 b, float t);
    float Animate(const char* key, float target, float speed = 14.f);
    float Ease(float t);

    bool BeginCard(const char* title, const char* iconPath = nullptr);
    void EndCard();
    bool BeginColumns(const char* id);
    void NextColumn();
    void EndColumns();

    bool Feature(const char* label, bool* value, misc::Bind* bind = nullptr, void (*options)() = nullptr);
    bool Switch(const char* label, bool* value);
    bool Slider(const char* label, float* value, float min, float max, const char* format);
    bool SliderInt(const char* label, int* value, int min, int max, const char* format);
    bool Combo(const char* label, int* value, const char* items);
    bool ColorEdit(const char* label, float* rgba);
    bool Segmented(const char* id, int* value, const char* const* labels, int count, float width);
    bool Button(const char* label, ImVec2 size, const char* iconPath = nullptr, int kind = Normal);
    void Heading(ImDrawList* dl, ImVec2 pos, float size, ImU32 color, const char* text);
    ImVec2 Snap(ImVec2 p);
    void Print(ImDrawList* dl, ImVec2 pos, ImU32 color, const char* text);
    void Print(ImDrawList* dl, ImFont* font, float size, ImVec2 pos, ImU32 color, const char* text);
    void RadialGlow(ImDrawList* dl, ImVec2 center, float radius, ImU32 color, float alpha);
    void SoftShadow(ImDrawList* dl, ImVec2 min, ImVec2 max, float rounding, float spread, ImU32 color, float alpha);
    void Chrome(ImVec2 pos, ImVec2 size);
    void KeybindEditor(misc::Bind* bind);
    void Value(const char* label, const char* fmt, ...);
    void Note(const char* text);

    void Toasts();
    bool ToastsActive();
}
