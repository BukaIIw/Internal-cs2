#include "menu_pages.h"
#include "ui.h"
#include "catalog.h"
#include "svg.h"
#include "icons_svg.h"
#include "../items.h"
#include "../core/addresses.h"
#include "../core/events.h"
#include "../core/hooks.h"
#include "../core/log.h"
#include "../core/schema.h"
#include "../core/settings.h"
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <string>
#include <vector>

namespace
{
    using ui::font_role;
    using ui::theme::token;
    namespace theme = ui::theme;

    constexpr float min_scale = 0.75f;
    constexpr float max_scale = 2.f;

    float g_pending_scale = 1.f;
    bool g_scale_active = false;
    char g_module[64] = "client.dll";
    char g_rva[24] = "";

    float text_y(float top, float height, font_role role)
    {
        return top + (height - ui::font_size(role)) * 0.5f;
    }

    void clipped_text(ImDrawList* dl, font_role role, ImVec2 pos, float right, float height, ImU32 col, const char* str)
    {
        dl->PushClipRect(ImVec2(pos.x, pos.y), ImVec2(std::fmax(pos.x, right), pos.y + height), true);
        ui::text(dl, role, ImVec2(pos.x, text_y(pos.y, height, role)), col, str);
        dl->PopClipRect();
    }

    const char* hook_state_name(hooks::state s)
    {
        switch (s)
        {
        case hooks::state::created:
            return "created";
        case hooks::state::enabled:
            return "enabled";
        case hooks::state::failed:
            return "failed";
        default:
            return "missing";
        }
    }

    token hook_state_tone(const hooks::info& h)
    {
        switch (h.status)
        {
        case hooks::state::enabled:
            return token::good;
        case hooks::state::created:
            return token::warn;
        default:
            return h.required ? token::danger : token::warn;
        }
    }

    token pattern_tone(addresses::status s)
    {
        switch (s)
        {
        case addresses::status::missing:
            return token::danger;
        case addresses::status::ambiguous:
            return token::warn;
        case addresses::status::unique:
            return token::good;
        default:
            return token::text_dim;
        }
    }

    token log_tone(int level)
    {
        switch (level)
        {
        case logs::Error:
            return token::danger;
        case logs::Warning:
            return token::warn;
        case logs::Success:
            return token::good;
        default:
            return token::text_dim;
        }
    }

    void general_tab()
    {
        const ui::metrics& mt = ui::m();
        if (!ui::begin_columns("##general"))
            return;
        ui::next_column();
        ui::begin_card("Config", icon::Save);
        if (ui::button("Save##config", ImVec2(0.f, mt.button_height), icon::Save))
        {
            if (settings::save())
                logs::Add(logs::Success, "Config saved");
        }
        if (ui::button("Load##config", ImVec2(0.f, mt.button_height), icon::Folder))
            settings::load();
        ui::note(settings::path());
        ui::end_card();

        ui::begin_card("Menu", icon::Keyboard);
        ui::key_button("Menu key", &settings::g_ui.menu_key);
        ui::end_card();

        ui::next_column();
        ui::begin_card("Session", icon::Power);
        int enabled = 0, total = 0, failed_required = 0;
        for (const hooks::info& h : hooks::snapshot())
        {
            ++total;
            if (h.status == hooks::state::enabled)
                ++enabled;
            else if (h.required)
                ++failed_required;
        }
        ui::value("Hooks", "%d / %d", enabled, total);
        ui::value("Required missing", "%d", failed_required);
        ui::value("Schema", "%s", schema::ready() ? "ready" : "unavailable");
        const int items_state = items::state.load();
        ui::value("Item files", "%s", items_state == 1 ? "loaded" : items_state < 0 ? "error" : "loading");
        ui::value("Catalog", "%s", ui::catalog::status_text());
        ImGui::Dummy(ImVec2(0.f, mt.space_xs));
        if (ui::button("Unload##session", ImVec2(0.f, mt.button_height), icon::Power, ui::button_kind::destructive))
            hooks::unloading.store(true);
        ui::end_card();
        ui::end_columns();
    }

    void preset_swatches()
    {
        const ui::metrics& mt = ui::m();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float radius = mt.preset_swatch * 0.5f;
        float* accent = settings::g_ui.accent.data();
        for (int i = 0; i < theme::accent_preset_count; ++i)
        {
            const float* preset = theme::accent_presets[i];
            const ImVec2 c(p.x + radius + static_cast<float>(i) * mt.preset_step, p.y + radius);
            ImGui::SetCursorScreenPos(ImVec2(c.x - radius, c.y - radius));
            ImGui::PushID(i);
            if (ImGui::InvisibleButton("##preset", ImVec2(mt.preset_swatch, mt.preset_swatch)))
                std::copy(preset, preset + 3, accent);
            const bool hovered = ImGui::IsItemHovered();
            ImGui::PopID();
            const bool current = std::fabs(accent[0] - preset[0]) < 0.002f && std::fabs(accent[1] - preset[1]) < 0.002f && std::fabs(accent[2] - preset[2]) < 0.002f;
            const ImU32 col = ImGui::GetColorU32(ImVec4(preset[0], preset[1], preset[2], 1.f));
            const float r = radius - mt.preset_hover + (hovered ? mt.preset_hover : 0.f);
            dl->AddCircleFilled(c, r, col, 32);
            if (current)
                dl->AddCircle(c, radius + mt.space_xxs, theme::color(token::text), 32, mt.stroke);
        }
        ImGui::SetCursorScreenPos(p);
        ImGui::Dummy(ImVec2(static_cast<float>(theme::accent_preset_count) * mt.preset_step, mt.preset_swatch + mt.space_xs));
    }

    void theme_tab()
    {
        if (!ui::begin_columns("##theme"))
            return;
        ui::next_column();
        ui::begin_card("Appearance", icon::Palette);
        ui::color_edit("Accent", settings::g_ui.accent.data());
        preset_swatches();
        ui::toggle("Reduce animations", &settings::g_ui.reduce_motion);
        ui::end_card();

        ui::next_column();
        ui::begin_card("Scale", icon::Sliders);
        if (!g_scale_active)
            g_pending_scale = std::isfinite(settings::g_ui.scale) ? std::clamp(settings::g_ui.scale, min_scale, max_scale) : 1.f;
        ui::slider("Interface scale", &g_pending_scale, min_scale, max_scale, "%.2fx");
        g_scale_active = ImGui::IsItemActive();
        if (!g_scale_active)
            settings::g_ui.scale = std::clamp(g_pending_scale, min_scale, max_scale);
        ui::value("Display DPI", "%.0f%%", theme::dpi_scale() * 100.f);
        ui::note("Scale is applied when the slider is released.");
        ui::end_card();
        ui::end_columns();
    }

    void events_tab()
    {
        const ui::metrics& mt = ui::m();
        ui::begin_card("Event bus", icon::List);
        if (ui::button("Reset stats##events", ImVec2(mt.button_wide, mt.button_small), icon::Trash))
            events::reset_stats();
        ImGui::Dummy(ImVec2(0.f, mt.space_xs));
        const float w = std::fmax(1.f, ImGui::GetContentRegionAvail().x);
        constexpr int column_count = static_cast<int>(std::size(ui::ratio::events_columns));
        float cols[column_count + 1];
        for (int i = 0; i < column_count; ++i)
            cols[i] = std::floor(w * ui::ratio::events_columns[i]);
        cols[column_count] = w;
        const char* const heads[column_count] = { "Event", "Subscriber", "Calls", "Avg us", "Peak us", "State" };
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        for (int i = 0; i < column_count; ++i)
            clipped_text(dl, font_role::caption, ImVec2(p.x + cols[i], p.y), p.x + cols[i + 1] - mt.cell_pad_x, mt.table_header, theme::color(token::text_faint), heads[i]);
        ImGui::Dummy(ImVec2(w, mt.table_header));
        ImGui::BeginChild("##events", ImVec2(0.f, mt.table_height));
        dl = ImGui::GetWindowDrawList();
        const float h = mt.table_row;
        int last_type = -1;
        for (const events::info& e : events::snapshot())
        {
            p = ImGui::GetCursorScreenPos();
            const float cw = std::fmax(1.f, ImGui::GetContentRegionAvail().x);
            if (e.type != last_type && last_type >= 0)
                dl->AddLine(p, ImVec2(p.x + cw, p.y), theme::color(token::border, 0.6f), mt.hairline);
            const auto right = [&](int i) { return p.x + cols[i + 1] - mt.cell_pad_x; };
            if (e.type != last_type)
                clipped_text(dl, font_role::body, ImVec2(p.x + cols[0], p.y), right(0), h, theme::accent(), events::name(e.type));
            last_type = e.type;
            char buf[32];
            clipped_text(dl, font_role::body, ImVec2(p.x + cols[1], p.y), right(1), h, theme::color(token::text), e.name ? e.name : "?");
            std::snprintf(buf, sizeof(buf), "%llu", static_cast<unsigned long long>(e.calls));
            clipped_text(dl, font_role::body, ImVec2(p.x + cols[2], p.y), right(2), h, theme::color(token::text_dim), buf);
            std::snprintf(buf, sizeof(buf), "%.1f", e.avg_us);
            clipped_text(dl, font_role::body, ImVec2(p.x + cols[3], p.y), right(3), h, theme::color(token::text_dim), buf);
            std::snprintf(buf, sizeof(buf), "%.1f", e.max_us);
            clipped_text(dl, font_role::body, ImVec2(p.x + cols[4], p.y), right(4), h, theme::color(e.max_us > 1000.0 ? token::warn : token::text_dim), buf);
            if (e.faulted)
            {
                ImGui::SetCursorScreenPos(ImVec2(p.x + cols[5], p.y + mt.space_xxs));
                ImGui::PushID(e.type * 256 + e.index);
                if (ui::button("Enable", ImVec2(std::fmax(1.f, cw - cols[5]), h - mt.space_xxs * 2.f), nullptr, ui::button_kind::destructive))
                    events::enable(e.type, e.index);
                ImGui::PopID();
            }
            else
            {
                ui::status_dot(dl, ImVec2(p.x + cols[5] + mt.space_xs, p.y + h * 0.5f), theme::color(token::good));
                clipped_text(dl, font_role::body, ImVec2(p.x + cols[5] + mt.status_text, p.y), p.x + cw, h, theme::color(token::text_dim), "ok");
            }
            ImGui::SetCursorScreenPos(p);
            ImGui::Dummy(ImVec2(cw, h));
        }
        ImGui::EndChild();
        ui::end_card();
    }

    void runtime_tab()
    {
        const ui::metrics& mt = ui::m();
        ui::begin_card("Hooks", icon::Bug);
        const float w = std::fmax(1.f, ImGui::GetContentRegionAvail().x);
        constexpr int column_count = static_cast<int>(std::size(ui::ratio::hooks_columns));
        float cols[column_count + 1];
        for (int i = 0; i < column_count; ++i)
            cols[i] = std::floor(w * ui::ratio::hooks_columns[i]);
        cols[column_count] = w;
        const char* const heads[column_count] = { "Hook", "Target", "State", "Required" };
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        for (int i = 0; i < column_count; ++i)
            clipped_text(dl, font_role::caption, ImVec2(p.x + cols[i], p.y), p.x + cols[i + 1] - mt.cell_pad_x, mt.table_header, theme::color(token::text_faint), heads[i]);
        ImGui::Dummy(ImVec2(w, mt.table_header));
        const float h = mt.table_row;
        for (const hooks::info& hk : hooks::snapshot())
        {
            p = ImGui::GetCursorScreenPos();
            const auto right = [&](int i) { return p.x + cols[i + 1] - mt.cell_pad_x; };
            clipped_text(dl, font_role::body, ImVec2(p.x + cols[0], p.y), right(0), h, theme::color(token::text), hk.name.c_str());
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%llX", static_cast<unsigned long long>(hk.target));
            clipped_text(dl, font_role::mono, ImVec2(p.x + cols[1], p.y), right(1), h, theme::color(token::text_dim), hk.target ? buf : "-");
            ui::status_dot(dl, ImVec2(p.x + cols[2] + mt.space_xs, p.y + h * 0.5f), theme::color(hook_state_tone(hk)));
            clipped_text(dl, font_role::body, ImVec2(p.x + cols[2] + mt.status_text, p.y), right(2), h, theme::color(token::text_dim), hook_state_name(hk.status));
            clipped_text(dl, font_role::body, ImVec2(p.x + cols[3], p.y), right(3), h, theme::color(token::text_dim), hk.required ? "yes" : "no");
            ImGui::Dummy(ImVec2(w, h));
        }
        ui::end_card();

        ui::begin_card("Schema", icon::List);
        ui::value("State", "%s", schema::ready() ? "ready" : "unavailable");
        const std::vector<schema::missing_field> missing = schema::missing();
        ui::value("Missing fields", "%d", static_cast<int>(missing.size()));
        for (const schema::missing_field& f : missing)
        {
            const std::string line = f.class_name + "::" + f.field;
            ui::text(ImGui::GetWindowDrawList(), font_role::mono, ImGui::GetCursorScreenPos(), theme::color(token::warn), line.c_str());
            ImGui::Dummy(ImVec2(0.f, ui::font_size(font_role::mono) + mt.space_xxs));
        }
        ui::end_card();
    }

    void pattern_list()
    {
        const ui::metrics& mt = ui::m();
        const bool busy = addresses::busy();
        ImGui::BeginChild("##patterns", ImVec2(0.f, mt.pattern_list));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float w = std::fmax(1.f, ImGui::GetContentRegionAvail().x);
        const float bw = mt.pattern_button;
        const float row_w = std::fmax(1.f, w - bw - mt.space_sm);
        const float pad = mt.space_xs;
        const float line = ui::font_size(font_role::body) + mt.space_xxs;
        const float mono = ui::font_size(font_role::mono) + mt.space_xxs;
        const std::vector<addresses::record> list = addresses::snapshot();
        for (int i = 0; i < static_cast<int>(list.size()); ++i)
        {
            const addresses::record& e = list[static_cast<std::size_t>(i)];
            const bool has_generated = !e.generated.empty();
            const float h = has_generated ? mt.pattern_row_generated : mt.pattern_row;
            const ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::PushID(i);
            const bool clicked = ImGui::InvisibleButton("##row", ImVec2(row_w, h));
            const bool hovered = ImGui::IsItemHovered();
            ImGui::SetCursorScreenPos(ImVec2(p.x + w - bw, p.y + pad));
            if (ui::button(busy ? "...##gen" : "Gen##gen", ImVec2(bw, mt.pattern_button_height)) && !busy)
                addresses::generate(i);
            ImGui::PopID();
            if (hovered)
                dl->AddRectFilled(p, ImVec2(p.x + row_w, p.y + h - mt.space_xxs), theme::color(token::control_hover, 0.6f), mt.radius_sm);
            const float tx = p.x + mt.status_text + mt.space_xs;
            ui::status_dot(dl, ImVec2(p.x + mt.space_sm, p.y + pad + line * 0.5f), theme::color(pattern_tone(e.state)));
            char meta[96];
            if (e.state == addresses::status::missing)
                std::snprintf(meta, sizeof(meta), "%s  not found", e.module.c_str());
            else if (e.state == addresses::status::pending)
                std::snprintf(meta, sizeof(meta), "%s  pending", e.module.c_str());
            else if (e.matches > 1)
                std::snprintf(meta, sizeof(meta), "%s+0x%X  %d+ matches", e.module.c_str(), e.rva, e.matches);
            else
                std::snprintf(meta, sizeof(meta), "%s+0x%X", e.module.c_str(), e.rva);
            const float meta_w = ui::text_size(font_role::compact, meta).x;
            const float meta_x = std::fmax(tx, p.x + row_w - pad - meta_w);
            ui::text(dl, font_role::compact, ImVec2(meta_x, p.y + pad), theme::color(token::text_faint), meta);
            dl->PushClipRect(p, ImVec2(std::fmax(p.x, meta_x - mt.space_sm), p.y + h), true);
            ui::text(dl, font_role::body, ImVec2(tx, p.y + pad), theme::color(token::text), e.name.c_str());
            dl->PopClipRect();
            dl->PushClipRect(p, ImVec2(p.x + row_w - pad, p.y + h), true);
            ui::text(dl, font_role::mono, ImVec2(tx, p.y + pad + line), theme::color(token::text_dim), e.spec.c_str());
            if (has_generated)
                ui::text(dl, font_role::mono, ImVec2(tx, p.y + pad + line + mono), theme::accent(), e.generated.c_str());
            dl->PopClipRect();
            if (clicked)
            {
                const bool use_generated = has_generated && e.generated != "-";
                ImGui::SetClipboardText(use_generated ? e.generated.c_str() : e.spec.c_str());
                logs::Add(logs::Info, "Copied %s", e.name.c_str());
            }
            ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h));
        }
        ImGui::Dummy(ImVec2(0.f, 0.f));
        ImGui::EndChild();
    }

    void generator()
    {
        const ui::metrics& mt = ui::m();
        ui::begin_card("Generator", icon::Plus);
        const float avail = std::fmax(1.f, ImGui::GetContentRegionAvail().x);
        const float field = std::floor(avail * ui::ratio::generator_field);
        ImGui::SetNextItemWidth(field);
        ImGui::InputTextWithHint("##module", "module", g_module, sizeof(g_module));
        ImGui::SameLine(0.f, mt.space_sm);
        ImGui::SetNextItemWidth(field);
        ImGui::InputTextWithHint("##rva", "RVA, e.g. 37C8E0", g_rva, sizeof(g_rva), ImGuiInputTextFlags_CharsHexadecimal);
        ImGui::SameLine(0.f, mt.space_sm);
        bool pending = false;
        const std::string result = addresses::generated_custom(&pending);
        if (ui::button(pending ? "...##custom" : "Generate##custom", ImVec2(std::fmax(1.f, ImGui::GetContentRegionAvail().x), ImGui::GetFrameHeight()), nullptr, ui::button_kind::primary) && g_rva[0] && !pending)
            addresses::generate_at(g_module, static_cast<std::uint32_t>(std::strtoul(g_rva, nullptr, 16)));
        if (!result.empty())
        {
            ImGui::Dummy(ImVec2(0.f, mt.space_xs));
            {
                ui::font_scope scope(font_role::mono);
                ImGui::PushTextWrapPos(0.f);
                ImGui::PushStyleColor(ImGuiCol_Text, theme::vec(theme::accent()));
                ImGui::TextUnformatted(result.c_str());
                ImGui::PopStyleColor();
                ImGui::PopTextWrapPos();
            }
            if (ui::button("Copy##custom", ImVec2(mt.button_wide, mt.button_small)))
                ImGui::SetClipboardText(result.c_str());
        }
        ui::end_card();
    }

    void patterns_tab()
    {
        const ui::metrics& mt = ui::m();
        ui::begin_card("Signatures", icon::Bug);
        const bool busy = addresses::busy();
        const float bw = std::fmax(1.f, std::floor((ImGui::GetContentRegionAvail().x - mt.space_sm * 2.f) / 3.f));
        if (ui::button(busy ? "Working...##verify" : "Verify##verify", ImVec2(bw, mt.button_small), icon::Check) && !busy)
            addresses::verify();
        ImGui::SameLine(0.f, mt.space_sm);
        if (ui::button("Generate all##all", ImVec2(bw, mt.button_small), icon::Plus, ui::button_kind::primary) && !busy)
        {
            const int count = static_cast<int>(addresses::snapshot().size());
            for (int i = 0; i < count; ++i)
                addresses::generate(i);
        }
        ImGui::SameLine(0.f, mt.space_sm);
        if (ui::button("Export##export", ImVec2(bw, mt.button_small), icon::Save))
        {
            if (!addresses::export_list())
                logs::Add(logs::Error, "Patterns: export failed");
        }
        ui::note("Click a row to copy. Generated patterns come from the clean module file on disk.");
        ImGui::Dummy(ImVec2(0.f, mt.space_xs));
        pattern_list();
        ui::end_card();
        generator();
    }

    void log_tab()
    {
        const ui::metrics& mt = ui::m();
        ui::begin_card("Log", icon::List);
        if (ui::button("Clear##log", ImVec2(mt.button_wide, mt.button_small), icon::Trash))
            logs::Clear();
        ImGui::Dummy(ImVec2(0.f, mt.space_xs));
        ImGui::BeginChild("##lines", ImVec2(0.f, mt.log_list));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const std::uint64_t now = GetTickCount64();
        const float line_h = ui::font_size(font_role::body) + mt.space_xs;
        const std::vector<logs::Line> lines = logs::Snapshot();
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(lines.size()), line_h);
        while (clipper.Step())
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
            {
                const logs::Line& line = lines[static_cast<std::size_t>(i)];
                const ImVec2 p = ImGui::GetCursorScreenPos();
                ui::status_dot(dl, ImVec2(p.x + mt.space_xs + mt.status_dot, p.y + line_h * 0.5f), theme::color(log_tone(line.level)));
                char age[24];
                std::snprintf(age, sizeof(age), "%llus", static_cast<unsigned long long>(now >= line.time ? (now - line.time) / 1000 : 0));
                ui::text(dl, font_role::compact, ImVec2(p.x + mt.status_text, text_y(p.y, line_h, font_role::compact)), theme::color(token::text_faint), age);
                ui::text(dl, font_role::body, ImVec2(p.x + mt.status_text + mt.log_age, text_y(p.y, line_h, font_role::body)), theme::color(token::text), line.text);
                ImGui::Dummy(ImVec2(0.f, line_h));
            }
        ImGui::EndChild();
        ui::end_card();
    }
}

void menu::pages::settings(int tab)
{
    switch (tab)
    {
    case 0:
        general_tab();
        break;
    case 1:
        theme_tab();
        break;
    case 2:
        events_tab();
        break;
    case 3:
        runtime_tab();
        break;
    case 4:
        patterns_tab();
        break;
    default:
        log_tab();
        break;
    }
}
