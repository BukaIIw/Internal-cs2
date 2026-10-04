#include "theme.h"
#include "fonts.h"
#include "metrics.h"
#include "render.h"
#include "../core/hooks.h"
#include "../core/settings.h"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <cmath>

namespace
{
    using ui::theme::token;

    constexpr float min_scale = 0.75f;
    constexpr float max_scale = 2.f;

    const ImVec4 k_tones[] = {
        { 0.058f, 0.062f, 0.078f, 1.f },
        { 0.043f, 0.047f, 0.059f, 1.f },
        { 0.083f, 0.090f, 0.113f, 1.f },
        { 0.135f, 0.147f, 0.180f, 1.f },
        { 0.113f, 0.125f, 0.157f, 1.f },
        { 0.150f, 0.166f, 0.212f, 1.f },
        { 0.910f, 0.920f, 0.940f, 1.f },
        { 0.570f, 0.600f, 0.660f, 1.f },
        { 0.370f, 0.390f, 0.450f, 1.f },
        { 0.940f, 0.330f, 0.360f, 1.f },
        { 0.290f, 0.850f, 0.550f, 1.f },
        { 0.980f, 0.740f, 0.270f, 1.f },
    };

    std::array<ImU32, static_cast<int>(token::count)> g_tokens{};
    float g_applied_scale = -1.f;
    std::array<float, 3> g_applied_accent{ -1.f, -1.f, -1.f };

    ImVec4 tone(token t, float alpha = 1.f)
    {
        ImVec4 v = ImGui::ColorConvertU32ToFloat4(g_tokens[static_cast<int>(t)]);
        v.w *= alpha;
        return v;
    }

    void rebuild_tokens(const settings::color& accent)
    {
        for (int i = 0; i < static_cast<int>(std::size(k_tones)); ++i)
            g_tokens[i] = ImGui::ColorConvertFloat4ToU32(k_tones[i]);
        g_tokens[static_cast<int>(token::accent)] = ImGui::ColorConvertFloat4ToU32(ImVec4(accent.r, accent.g, accent.b, 1.f));
        g_tokens[static_cast<int>(token::on_accent)] = IM_COL32(10, 14, 20, 255);
        g_tokens[static_cast<int>(token::team_t)] = IM_COL32(234, 182, 92, 255);
        g_tokens[static_cast<int>(token::team_ct)] = IM_COL32(110, 160, 255, 255);
        g_tokens[static_cast<int>(token::stattrak)] = IM_COL32(255, 140, 70, 255);
        g_tokens[static_cast<int>(token::shadow)] = IM_COL32(0, 0, 0, 255);
        g_tokens[static_cast<int>(token::white)] = IM_COL32(255, 255, 255, 255);
    }

    void apply_colors(ImGuiStyle& s)
    {
        ImVec4* c = s.Colors;
        const ImVec4 clear(0.f, 0.f, 0.f, 0.f);
        c[ImGuiCol_Text] = tone(token::text);
        c[ImGuiCol_TextDisabled] = tone(token::text_dim);
        c[ImGuiCol_WindowBg] = tone(token::background);
        c[ImGuiCol_ChildBg] = clear;
        c[ImGuiCol_PopupBg] = tone(token::surface, 0.99f);
        c[ImGuiCol_Border] = tone(token::border);
        c[ImGuiCol_BorderShadow] = clear;
        c[ImGuiCol_FrameBg] = tone(token::control);
        c[ImGuiCol_FrameBgHovered] = tone(token::control_hover);
        c[ImGuiCol_FrameBgActive] = tone(token::control_hover);
        c[ImGuiCol_TitleBg] = tone(token::rail);
        c[ImGuiCol_TitleBgActive] = tone(token::rail);
        c[ImGuiCol_TitleBgCollapsed] = tone(token::rail);
        c[ImGuiCol_ScrollbarBg] = clear;
        c[ImGuiCol_ScrollbarGrab] = tone(token::border);
        c[ImGuiCol_ScrollbarGrabHovered] = tone(token::text_faint);
        c[ImGuiCol_ScrollbarGrabActive] = tone(token::accent, 0.8f);
        c[ImGuiCol_CheckMark] = tone(token::accent);
        c[ImGuiCol_SliderGrab] = tone(token::accent);
        c[ImGuiCol_SliderGrabActive] = tone(token::accent);
        c[ImGuiCol_Button] = tone(token::control);
        c[ImGuiCol_ButtonHovered] = tone(token::control_hover);
        c[ImGuiCol_ButtonActive] = tone(token::accent, 0.45f);
        c[ImGuiCol_Header] = tone(token::accent, 0.16f);
        c[ImGuiCol_HeaderHovered] = tone(token::control_hover);
        c[ImGuiCol_HeaderActive] = tone(token::accent, 0.28f);
        c[ImGuiCol_Separator] = tone(token::border);
        c[ImGuiCol_SeparatorHovered] = tone(token::border);
        c[ImGuiCol_SeparatorActive] = tone(token::accent);
        c[ImGuiCol_ResizeGrip] = clear;
        c[ImGuiCol_ResizeGripHovered] = clear;
        c[ImGuiCol_ResizeGripActive] = clear;
        c[ImGuiCol_Tab] = tone(token::control);
        c[ImGuiCol_TabHovered] = tone(token::control_hover);
        c[ImGuiCol_TabSelected] = tone(token::accent, 0.3f);
        c[ImGuiCol_TableHeaderBg] = clear;
        c[ImGuiCol_TableBorderLight] = clear;
        c[ImGuiCol_TableBorderStrong] = clear;
        c[ImGuiCol_TableRowBg] = clear;
        c[ImGuiCol_TableRowBgAlt] = clear;
        c[ImGuiCol_TextSelectedBg] = tone(token::accent, 0.3f);
        c[ImGuiCol_NavCursor] = tone(token::accent);
        c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.f, 0.f, 0.f, 0.45f);
    }

    void apply_style(bool initial)
    {
        const ui::metrics& mt = ui::m();
        ImGuiStyle& current = ImGui::GetStyle();
        ImGuiStyle s;
        s.ScaleAllSizes(mt.scale);
        s.FontScaleMain = 1.f;
        s.FontScaleDpi = 1.f;
        s.WindowRounding = mt.window_radius;
        s.ChildRounding = mt.radius_card;
        s.FrameRounding = mt.radius_frame;
        s.PopupRounding = mt.radius_card;
        s.GrabRounding = mt.radius_sm;
        s.TabRounding = mt.radius_sm;
        s.ScrollbarRounding = mt.radius_sm;
        s.SelectableRounding = mt.radius_sm;
        s.ScrollbarSize = mt.scrollbar;
        s.GrabMinSize = mt.grab_min;
        s.WindowBorderSize = mt.hairline;
        s.ChildBorderSize = mt.hairline;
        s.PopupBorderSize = mt.hairline;
        s.FrameBorderSize = 0.f;
        s.ItemSpacing = ImVec2(mt.item_gap, mt.row_gap);
        s.ItemInnerSpacing = ImVec2(mt.inner_gap_x, mt.inner_gap_y);
        s.FramePadding = ImVec2(mt.popup_pad, std::fmax(0.f, std::floor((mt.control - mt.font_body) * 0.5f)));
        s.WindowPadding = ImVec2(mt.popover_pad_x, mt.popover_pad_x);
        s.CellPadding = ImVec2(mt.cell_pad_x, 0.f);
        s.SelectableTextAlign = ImVec2(0.f, 0.5f);
        s.AntiAliasedLines = true;
        s.AntiAliasedFill = true;
        apply_colors(s);
        if (initial)
            s.FontSizeBase = mt.font_body;
        else
        {
            s.FontSizeBase = current.FontSizeBase;
            if (current.FontSizeBase != mt.font_body)
                s._NextFrameFontSizeBase = mt.font_body;
        }
        current = s;
    }
}

const float ui::theme::accent_presets[accent_preset_count][3] = {
    { 0.25f, 0.72f, 1.f },
    { 0.45f, 0.86f, 0.62f },
    { 1.f, 0.62f, 0.32f },
    { 0.96f, 0.42f, 0.55f },
    { 0.68f, 0.56f, 1.f },
    { 0.92f, 0.92f, 0.95f },
};

ImU32 ui::theme::raw(token t)
{
    return g_tokens[static_cast<int>(t)];
}

ImU32 ui::theme::color(token t, float alpha)
{
    return render::Fade(g_tokens[static_cast<int>(t)], alpha * ImGui::GetStyle().Alpha);
}

ImU32 ui::theme::accent(float alpha)
{
    return color(token::accent, alpha);
}

ImU32 ui::theme::mix(ImU32 a, ImU32 b, float t)
{
    return render::Lerp(a, b, t);
}

ImU32 ui::theme::mul_alpha(ImU32 c, float alpha)
{
    return render::Fade(c, alpha);
}

ImU32 ui::theme::with_alpha(ImU32 c, float alpha)
{
    const float a = std::clamp(alpha, 0.f, 1.f);
    return (c & 0x00FFFFFF) | (static_cast<ImU32>(a * 255.f + 0.5f) << 24);
}

ImU32 ui::theme::faded(ImU32 c)
{
    return render::Fade(c, ImGui::GetStyle().Alpha);
}

ImVec4 ui::theme::vec(ImU32 c)
{
    return ImGui::ColorConvertU32ToFloat4(c);
}

float ui::theme::dpi_scale()
{
    using dpi_for_window = UINT(WINAPI*)(HWND);
    using dpi_for_system = UINT(WINAPI*)();
    static const HMODULE user32 = GetModuleHandleA("user32.dll");
    static const auto for_window = user32 ? reinterpret_cast<dpi_for_window>(GetProcAddress(user32, "GetDpiForWindow")) : nullptr;
    static const auto for_system = user32 ? reinterpret_cast<dpi_for_system>(GetProcAddress(user32, "GetDpiForSystem")) : nullptr;
    UINT dpi = 0;
    if (HWND window = static_cast<HWND>(hooks::window()); window && for_window)
        dpi = for_window(window);
    if (!dpi && for_system)
        dpi = for_system();
    return dpi ? static_cast<float>(dpi) / 96.f : 1.f;
}

void ui::theme::refresh(bool force)
{
    const settings::color& accent_color = settings::g_ui.accent;
    const float user = std::isfinite(settings::g_ui.scale) ? std::clamp(settings::g_ui.scale, min_scale, max_scale) : 1.f;
    const float scale = user * dpi_scale();
    const bool scale_changed = std::fabs(scale - g_applied_scale) > 0.0001f;
    const bool accent_changed = accent_color.r != g_applied_accent[0] || accent_color.g != g_applied_accent[1] || accent_color.b != g_applied_accent[2];
    if (!force && !scale_changed && !accent_changed)
        return;
    if (force || scale_changed)
    {
        g_metrics = g_base_metrics.scaled(scale);
        g_applied_scale = scale;
    }
    g_applied_accent = { accent_color.r, accent_color.g, accent_color.b };
    rebuild_tokens(accent_color);
    apply_style(force);
}

void ui::style()
{
    theme::refresh(true);
}
