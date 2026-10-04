#include "misc.h"
#include "../../core/keys.h"
#include "../../core/settings.h"
#include "../combat/shots.h"
#include "../../ui/menu.h"
#include "../../ui/render.h"
#include "imgui.h"
#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <ctime>
#include <Windows.h>

namespace
{
    constexpr float margin = 10.f;
    constexpr float padding = 8.f;
    constexpr float rounding = 8.f;
    constexpr float accent_height = 2.f;
    constexpr float min_list_width = 190.f;
    constexpr float column_gap = 16.f;
    constexpr int max_rows = 32;
    constexpr int shot_rows = 6;
    constexpr std::uint64_t shot_visible_ms = 6000;
    constexpr std::uint64_t shot_fade_ms = 1000;

    bool g_dragging = false;
    ImVec2 g_drag_offset{};

    struct bind_row
    {
        const char* name;
        char key[48];
    };

    bool feature_enabled(const keys::bind* b)
    {
        if (b == &settings::g_rage.key || b == &settings::g_rage.damage_override_key)
            return settings::g_rage.enabled;
        if (b == &settings::g_legit.key)
            return settings::g_legit.enabled;
        if (b == &settings::g_trigger.key)
            return settings::g_trigger.enabled;
        if (b == &settings::g_movement.bhop_key)
            return settings::g_movement.bhop;
        if (b == &settings::g_movement.airstrafe_key)
            return settings::g_movement.airstrafe;
        if (b == &settings::g_movement.jumpbug_key)
            return settings::g_movement.jumpbug;
        if (b == &settings::g_misc.thirdperson_key)
            return settings::g_misc.thirdperson;
        return true;
    }

    const char* mode_name(keys::mode m)
    {
        switch (m)
        {
        case keys::mode::hold:
            return "hold";
        case keys::mode::toggle:
            return "toggle";
        default:
            return "always";
        }
    }

    float text_width(ImFont* font, float size, const char* text)
    {
        return font->CalcTextSizeA(size, FLT_MAX, 0.f, text).x;
    }

    void panel(ImDrawList* draw, ImVec2 a, ImVec2 b)
    {
        render::Rect(draw, a, b, IM_COL32(14, 16, 22, 240), rounding);
        render::Border(draw, a, b, IM_COL32(255, 255, 255, 28), rounding);
        render::Rect(draw, ImVec2(a.x + padding, a.y), ImVec2(b.x - padding, a.y + accent_height), static_cast<ImU32>(settings::g_ui.accent.abgr()), 1.f);
    }

    float draw_watermark(ImDrawList* draw, ImFont* font, float size, const ImVec2& screen, float y)
    {
        char line[128];
        const std::time_t now = std::time(nullptr);
        std::tm local{};
        localtime_s(&local, &now);
        std::snprintf(line, sizeof(line), "internal-cs2 | %d fps | %02d:%02d:%02d", static_cast<int>(ImGui::GetIO().Framerate), local.tm_hour, local.tm_min, local.tm_sec);
        const float width = text_width(font, size, line);
        const ImVec2 a(screen.x - margin - width - padding * 2.f, y), b(screen.x - margin, y + size + padding * 1.25f);
        panel(draw, a, b);
        render::Text(draw, font, size, ImVec2(a.x + padding, a.y + padding * 0.625f), IM_COL32(235, 238, 245, 255), line);
        return b.y + padding;
    }

    void draw_keybinds(ImDrawList* draw, ImFont* font, float size, const ImVec2& screen, float y)
    {
        bind_row rows[max_rows];
        int count = 0;
        const int total = keys::count();
        for (int i = 0; i < total && count < max_rows; ++i)
        {
            const keys::bind* b = keys::at(i);
            if (!b || !b->name || b->type == keys::mode::off)
                continue;
            if (!b->key && b->type != keys::mode::always)
                continue;
            if (!feature_enabled(b) || !keys::active(*b))
                continue;
            auto& row = rows[count++];
            row.name = b->name;
            if (!b->key || b->type == keys::mode::always)
                std::snprintf(row.key, sizeof(row.key), "[always]");
            else
                std::snprintf(row.key, sizeof(row.key), "[%s] %s", keys::key_name(b->key), mode_name(b->type));
        }
        if (!count && !menu::open)
            return;

        float width = min_list_width;
        for (int i = 0; i < count; ++i)
            width = std::max(width, text_width(font, size, rows[i].name) + text_width(font, size, rows[i].key) + column_gap + padding * 2.f);

        const float line = size + 4.f;
        const float height = line * static_cast<float>(count + 1) + padding * 1.25f;
        auto& cfg = settings::g_misc;
        ImVec2 a(screen.x - margin - width, y);
        if (cfg.keybinds_x >= 0.f && cfg.keybinds_y >= 0.f)
            a = ImVec2(cfg.keybinds_x, cfg.keybinds_y);

        const ImGuiIO& io = ImGui::GetIO();
        if (menu::open)
        {
            const ImVec2 mouse = io.MousePos;
            const bool hovered = mouse.x >= a.x && mouse.x <= a.x + width && mouse.y >= a.y && mouse.y <= a.y + line + padding;
            if (!g_dragging && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow))
            {
                g_dragging = true;
                g_drag_offset = ImVec2(mouse.x - a.x, mouse.y - a.y);
            }
            if (g_dragging)
            {
                if (io.MouseDown[ImGuiMouseButton_Left])
                {
                    a = ImVec2(std::clamp(mouse.x - g_drag_offset.x, 0.f, std::max(0.f, screen.x - width)), std::clamp(mouse.y - g_drag_offset.y, 0.f, std::max(0.f, screen.y - height)));
                    cfg.keybinds_x = a.x;
                    cfg.keybinds_y = a.y;
                }
                else
                    g_dragging = false;
            }
        }
        else
            g_dragging = false;

        a.x = std::clamp(a.x, 0.f, std::max(0.f, screen.x - width));
        a.y = std::clamp(a.y, 0.f, std::max(0.f, screen.y - height));
        const ImVec2 b(a.x + width, a.y + height);
        panel(draw, a, b);

        const char* title = "keybinds";
        render::Text(draw, font, size, ImVec2(a.x + (width - text_width(font, size, title)) * 0.5f, a.y + padding * 0.625f), IM_COL32(235, 238, 245, 255), title);
        float row_y = a.y + padding * 0.625f + line;
        for (int i = 0; i < count; ++i)
        {
            render::Text(draw, font, size, ImVec2(a.x + padding, row_y), IM_COL32(235, 238, 245, 255), rows[i].name);
            render::Text(draw, font, size, ImVec2(b.x - padding - text_width(font, size, rows[i].key), row_y), IM_COL32(150, 156, 170, 255), rows[i].key);
            row_y += line;
        }
    }

    void draw_shots(ImDrawList* draw, ImFont* font, float size)
    {
        features::combat::shots::entry entries[shot_rows];
        const int count = features::combat::shots::snapshot(entries, shot_rows);
        const std::uint64_t now = GetTickCount64();
        float y = margin;
        for (int i = count - 1; i >= 0; --i)
        {
            const auto& e = entries[i];
            const std::uint64_t age = now - e.time;
            if (age >= shot_visible_ms)
                continue;
            float alpha = 1.f;
            if (age > shot_visible_ms - shot_fade_ms)
                alpha = static_cast<float>(shot_visible_ms - age) / static_cast<float>(shot_fade_ms);
            const int a = static_cast<int>(alpha * 255.f);
            const ImU32 tag = e.hit ? IM_COL32(120, 220, 140, a) : IM_COL32(240, 120, 110, a);
            const char* label = e.hit ? "[hit] " : "[miss] ";
            render::Text(draw, font, size, ImVec2(margin, y), tag, label);
            render::Text(draw, font, size, ImVec2(margin + text_width(font, size, label), y), IM_COL32(235, 238, 245, a), e.text);
            y += size + 4.f;
        }
    }
}

namespace features::misc
{
    void overlay::on_present(ImDrawList* draw)
    {
        const auto& cfg = settings::g_misc;
        if (!draw || (!cfg.watermark && !cfg.keybinds && !cfg.shot_logs))
            return;

        ImFont* font = ImGui::GetFont();
        const float size = ImGui::GetFontSize();
        const ImVec2 screen = ImGui::GetIO().DisplaySize;
        if (!font || size <= 0.f || screen.x < 1.f || screen.y < 1.f)
            return;

        float y = margin;
        if (cfg.watermark)
            y = draw_watermark(draw, font, size, screen, y);
        if (cfg.keybinds)
            draw_keybinds(draw, font, size, screen, y);
        if (cfg.shot_logs)
            draw_shots(draw, font, size);
    }

    bool overlay::wants_frame() const
    {
        return settings::g_misc.watermark || settings::g_misc.keybinds || settings::g_misc.shot_logs;
    }
}
