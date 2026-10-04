#include "ui.h"
#include "svg.h"
#include "render.h"
#include "icons_svg.h"
#include "imgui_internal.h"
#include "../core/log.h"
#include "../core/settings.h"
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <vector>

namespace
{
    using ui::font_role;
    using ui::theme::token;
    namespace theme = ui::theme;

    constexpr const char* search_icon = "M17 10.5a6.5 6.5 0 1 1-13 0a6.5 6.5 0 1 1 13 0ZM15.2 15.2L20 20";
    constexpr std::uint64_t toast_life = 4000;
    constexpr std::uint64_t toast_fade_in = 220;
    constexpr std::uint64_t toast_fade_out = 300;
    constexpr std::uint64_t toast_refresh = 100;
    constexpr int toast_limit = 4;

    struct card_frame
    {
        ImVec2 start;
        float width;
        float work_max;
        float content_max;
        ImGuiID height_id;
        bool deferred;
    };

    std::vector<card_frame> g_cards;

    struct toast_cache
    {
        std::vector<logs::Line> lines;
        std::uint64_t fetched = 0;
        bool valid = false;
    };

    toast_cache g_toasts;

    const char* visible_end(const char* label)
    {
        return ImGui::FindRenderedTextEnd(label);
    }

    ImU32 tone(token t, float alpha = 1.f)
    {
        return theme::color(t, alpha);
    }

    ImU32 white(float alpha = 1.f)
    {
        return theme::color(token::white, alpha);
    }

    float centered_y(float top, float height, font_role role)
    {
        return top + (height - ui::font_size(role)) * 0.5f;
    }

    bool add_row(ImGuiID id, float height, ImRect& bb, ImGuiItemFlags flags = 0)
    {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        const ImVec2 pos = window->DC.CursorPos;
        const float width = std::fmax(1.f, ImGui::GetContentRegionAvail().x);
        bb = ImRect(pos, ImVec2(pos.x + width, pos.y + height));
        ImGui::ItemSize(bb.GetSize());
        return ImGui::ItemAdd(bb, id, nullptr, flags);
    }

    void draw_card_background(ImDrawList* dl, ImVec2 a, ImVec2 b)
    {
        const ui::metrics& mt = ui::m();
        render::Gradient(dl, a, b, theme::mix(tone(token::surface), tone(token::control_hover), 0.35f), tone(token::surface), mt.radius_card);
        render::Border(dl, a, b, tone(token::border, 0.8f), mt.radius_card, mt.hairline);
        render::Gradient(dl, ImVec2(a.x + mt.radius_card, a.y), ImVec2(b.x - mt.radius_card, a.y + mt.hairline), white(0.f), white(14.f / 255.f), 0.f, render::Horizontal);
    }

    void draw_chevron(ImDrawList* dl, const ImRect& bb, ImU32 col)
    {
        const ui::metrics& mt = ui::m();
        const float size = mt.icon_sm;
        svg::Stroke(dl, icon::Chevron, ImVec2(bb.Max.x - mt.popup_pad - size, bb.Min.y + (bb.GetHeight() - size) * 0.5f), size, col, mt.stroke);
    }

    void popover(const char* title, keys::bind* bind, void (*options)(), ImVec2 anchor)
    {
        const ui::metrics& mt = ui::m();
        const bool open = ImGui::IsPopupOpen("##opts");
        const float t = ui::animate("##popanim", open ? 1.f : 0.f, 18.f);
        if (!open)
        {
            if (bind && keys::capturing == bind)
                keys::capturing = nullptr;
            return;
        }
        ImGui::SetNextWindowPos(anchor, ImGuiCond_Appearing, ImVec2(1.f, 0.f));
        ImGui::SetNextWindowSize(ImVec2(mt.popover_width, 0.f));
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * ui::ease(t));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(mt.popover_pad_x, mt.popover_pad_y));
        if (ImGui::BeginPopup("##opts"))
        {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            ui::text(ImGui::GetWindowDrawList(), font_role::caption, p, tone(token::text_dim), title);
            ImGui::Dummy(ImVec2(0.f, ui::font_size(font_role::caption)));
            if (options)
                options();
            if (bind)
            {
                if (options)
                    ui::divider();
                ui::bind_editor(bind);
            }
            ImGui::EndPopup();
        }
        else if (bind && keys::capturing == bind)
            keys::capturing = nullptr;
        ImGui::PopStyleVar(2);
    }

    bool slider_impl(const char* label, ImGuiDataType type, void* data, const void* min_value, const void* max_value, const char* format)
    {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (window->SkipItems)
            return false;
        ImGuiContext& g = *GImGui;
        const ui::metrics& mt = ui::m();
        ImGui::PushID(label);
        const ImGuiID id = window->GetID("##slider");
        const ImVec2 pos = window->DC.CursorPos;
        const float width = std::fmax(1.f, ImGui::GetContentRegionAvail().x);
        const ImRect bb(pos, ImVec2(pos.x + width, pos.y + mt.slider_label + mt.slider_hit));
        const ImRect track(ImVec2(pos.x, pos.y + mt.slider_label), bb.Max);
        ImGui::ItemSize(bb.GetSize());
        if (!ImGui::ItemAdd(bb, id, &track, ImGuiItemFlags_Inputable))
        {
            ImGui::PopID();
            return false;
        }
        const bool hovered = ImGui::ItemHoverable(track, id, g.LastItemData.ItemFlags);
        bool temp_input = ImGui::TempInputIsActive(id);
        if (!temp_input)
        {
            const bool clicked = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left, ImGuiInputFlags_None, id);
            const bool nav = g.NavActivateId == id;
            if (clicked)
                ImGui::SetKeyOwner(ImGuiKey_MouseLeft, id);
            if ((clicked && g.IO.KeyCtrl) || (nav && (g.NavActivateFlags & ImGuiActivateFlags_PreferInput)))
                temp_input = true;
            else if (clicked || nav)
            {
                ImGui::SetActiveID(id, window);
                ImGui::SetFocusID(id, window);
                ImGui::FocusWindow(window);
            }
        }
        if (temp_input)
        {
            const float cy = (bb.Min.y + bb.Max.y) * 0.5f;
            const ImRect input_bb(ImVec2(pos.x, cy - mt.control * 0.5f), ImVec2(pos.x + width, cy + mt.control * 0.5f));
            const bool edited = ImGui::TempInputScalar(input_bb, id, "##slider", type, data, format, min_value, max_value);
            ImGui::PopID();
            return edited;
        }

        const bool is_float = type == ImGuiDataType_Float;
        const double lo = is_float ? *static_cast<const float*>(min_value) : *static_cast<const int*>(min_value);
        const double hi = is_float ? *static_cast<const float*>(max_value) : *static_cast<const int*>(max_value);
        const float inset = mt.slider_knob;
        const float span = std::fmax(1.f, track.GetWidth() - inset * 2.f);
        bool changed = false;
        if (g.ActiveId == id)
        {
            if (!g.IO.MouseDown[ImGuiMouseButton_Left])
                ImGui::ClearActiveID();
            else if (hi != lo)
            {
                const double f = std::clamp(static_cast<double>((g.IO.MousePos.x - track.Min.x - inset) / span), 0.0, 1.0);
                const double v = lo + f * (hi - lo);
                if (is_float)
                {
                    float* p = static_cast<float*>(data);
                    const float next = static_cast<float>(v);
                    if (next != *p)
                    {
                        *p = next;
                        changed = true;
                    }
                }
                else
                {
                    int* p = static_cast<int*>(data);
                    const int next = static_cast<int>(std::lround(v));
                    if (next != *p)
                    {
                        *p = next;
                        changed = true;
                    }
                }
            }
        }
        if (changed)
            ImGui::MarkItemEdited(id);

        const double current = is_float ? *static_cast<const float*>(data) : *static_cast<const int*>(data);
        const float fraction = hi != lo ? static_cast<float>(std::clamp((current - lo) / (hi - lo), 0.0, 1.0)) : 0.f;
        const bool active = g.ActiveId == id;

        ImDrawList* dl = window->DrawList;
        char value_text[64];
        ImGui::DataTypeFormatString(value_text, IM_COUNTOF(value_text), type, data, format);
        const ImVec2 value_size = ui::text_size(font_role::body, value_text);
        const float label_y = centered_y(pos.y, mt.slider_label, font_role::body);
        dl->PushClipRect(pos, ImVec2(std::fmax(pos.x, pos.x + width - value_size.x - mt.space_sm), bb.Max.y), true);
        ui::text(dl, font_role::body, ImVec2(pos.x, label_y), tone(token::text_dim), label, visible_end(label));
        dl->PopClipRect();
        ui::text(dl, font_role::body, ImVec2(pos.x + width - value_size.x, label_y), tone(token::text), value_text);

        const float cy = (track.Min.y + track.Max.y) * 0.5f;
        const float half = mt.slider_line * 0.5f;
        const float kx = track.Min.x + inset + fraction * span;
        dl->AddRectFilled(ImVec2(track.Min.x, cy - half), ImVec2(track.Max.x, cy + half), tone(token::control), half);
        render::Gradient(dl, ImVec2(track.Min.x, cy - half), ImVec2(std::fmax(track.Min.x + mt.slider_line, kx), cy + half), theme::accent(0.55f), theme::accent(), half, render::Horizontal);
        const float grow = ui::animate("##grow", active ? 1.f : hovered ? 0.5f : 0.f, 20.f);
        if (grow > 0.01f)
            ui::glow(dl, ImVec2(kx, cy), mt.slider_glow + mt.slider_glow_grow * grow, theme::accent(), 0.35f * grow);
        dl->AddCircleFilled(ImVec2(kx, cy), mt.slider_knob, white(), 32);
        dl->AddCircle(ImVec2(kx, cy), mt.slider_knob, theme::accent(), 32, mt.stroke);
        ImGui::PopID();
        return changed;
    }

    const std::vector<logs::Line>& recent_lines()
    {
        const std::uint64_t now = GetTickCount64();
        if (!g_toasts.valid || now - g_toasts.fetched >= toast_refresh)
        {
            g_toasts.lines = logs::Snapshot();
            g_toasts.fetched = now;
            g_toasts.valid = true;
        }
        return g_toasts.lines;
    }
}

float ui::frame_dt()
{
    const float dt = ImGui::GetIO().DeltaTime;
    return std::isfinite(dt) ? std::clamp(dt, 0.f, 1.f / 30.f) : 0.f;
}

float ui::ease(float t)
{
    t = std::clamp(t, 0.f, 1.f);
    const float inv = 1.f - t;
    return 1.f - inv * inv * inv;
}

float ui::animate(const char* key, float target, float speed)
{
    ImGuiStorage* storage = ImGui::GetStateStorage();
    const ImGuiID id = ImGui::GetID(key);
    float v = storage->GetFloat(id, target);
    if (settings::g_ui.reduce_motion)
        v = target;
    else
    {
        const float k = std::fmin(1.f, frame_dt() * speed);
        v += (target - v) * k;
        if (std::fabs(target - v) < 0.001f)
            v = target;
    }
    storage->SetFloat(id, v);
    return v;
}

float ui::approach(float current, float target, float speed)
{
    if (settings::g_ui.reduce_motion)
        return target;
    const float step = frame_dt() * speed;
    if (current < target)
        return std::fmin(target, current + step);
    return std::fmax(target, current - step);
}

void ui::text(ImDrawList* dl, font_role role, ImVec2 pos, ImU32 col, const char* str, const char* str_end)
{
    if (!str || str == str_end || !*str)
        return;
    dl->AddText(font(role), font_size(role), render::Snap(pos), col, str, str_end);
}

void ui::glow(ImDrawList* dl, ImVec2 center, float radius, ImU32 col, float alpha)
{
    render::Glow(dl, center, radius, render::Fade(col | IM_COL32_A_MASK, alpha));
}

void ui::soft_shadow(ImDrawList* dl, ImVec2 min, ImVec2 max, float rounding, float spread, float alpha)
{
    render::Shadow(dl, min, max, rounding, spread, render::Fade(theme::raw(token::shadow) | IM_COL32_A_MASK, alpha));
}

void ui::status_dot(ImDrawList* dl, ImVec2 center, ImU32 col)
{
    dl->AddCircleFilled(center, m().status_dot, col, 16);
}

void ui::chrome(ImVec2 pos, ImVec2 size)
{
    const metrics& mt = m();
    const float alpha = ImGui::GetStyle().Alpha;
    soft_shadow(ImGui::GetBackgroundDrawList(), ImVec2(pos.x + mt.window_shadow_inset, pos.y + mt.window_shadow_drop), ImVec2(pos.x + size.x - mt.window_shadow_inset, pos.y + size.y + mt.window_shadow_tail), mt.radius_xl, mt.window_shadow, 0.55f * alpha);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float inset = mt.window_radius;
    const float w = size.x - inset * 2.f;
    const float mid = pos.x + inset + w * 0.5f;
    render::Gradient(dl, ImVec2(pos.x + inset, pos.y), ImVec2(mid, pos.y + mt.hairline), theme::accent(0.f), theme::accent(0.9f), 0.f, render::Horizontal);
    render::Gradient(dl, ImVec2(mid, pos.y), ImVec2(pos.x + inset + w, pos.y + mt.hairline), theme::accent(0.9f), theme::accent(0.f), 0.f, render::Horizontal);
}

void ui::begin_card(const char* title, const char* icon_path)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const metrics& mt = m();
    ImGui::PushID(title);
    card_frame frame{};
    frame.start = window->DC.CursorPos;
    frame.width = std::fmax(1.f, ImGui::GetContentRegionAvail().x);
    frame.height_id = window->GetID("##card_height");
    frame.deferred = (window->Flags & ImGuiWindowFlags_ChildWindow) && !(window->Flags & ImGuiWindowFlags_Popup) && window->ParentWindow;
    if (!frame.deferred)
    {
        const float height = window->StateStorage.GetFloat(frame.height_id, 0.f);
        if (height > 0.f)
            draw_card_background(window->DrawList, frame.start, ImVec2(frame.start.x + frame.width, frame.start.y + height));
    }

    ImGui::BeginGroup();
    ImGui::Dummy(ImVec2(frame.width, mt.card_pad_y + mt.card_header));
    ImDrawList* dl = window->DrawList;
    float x = frame.start.x + mt.card_pad_x;
    const float header_top = frame.start.y + mt.card_pad_y;
    if (icon_path)
    {
        svg::Stroke(dl, icon_path, ImVec2(x, header_top + (mt.card_header - mt.icon_md) * 0.5f), mt.icon_md, theme::accent(), mt.stroke);
        x += mt.icon_md + mt.space_sm;
    }
    dl->PushClipRect(frame.start, ImVec2(frame.start.x + frame.width - mt.card_pad_x, header_top + mt.card_header), true);
    text(dl, font_role::body_strong, ImVec2(x, centered_y(header_top, mt.card_header, font_role::body_strong)), tone(token::text), title, visible_end(title));
    dl->PopClipRect();

    ImGui::Indent(mt.card_pad_x);
    frame.work_max = window->WorkRect.Max.x;
    frame.content_max = window->ContentRegionRect.Max.x;
    window->WorkRect.Max.x -= mt.card_pad_x;
    window->ContentRegionRect.Max.x -= mt.card_pad_x;
    g_cards.push_back(frame);
}

void ui::end_card()
{
    if (g_cards.empty())
        return;
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const metrics& mt = m();
    const card_frame frame = g_cards.back();
    g_cards.pop_back();

    ImGui::Dummy(ImVec2(0.f, std::fmax(0.f, mt.card_pad_y - mt.row_gap)));
    window->WorkRect.Max.x = frame.work_max;
    window->ContentRegionRect.Max.x = frame.content_max;
    ImGui::Unindent(mt.card_pad_x);
    ImGui::EndGroup();

    const float bottom = ImGui::GetItemRectMax().y;
    const ImVec2 a = frame.start;
    const ImVec2 b(frame.start.x + frame.width, bottom);
    if (frame.deferred)
    {
        ImDrawList* parent = window->ParentWindow->DrawList;
        parent->PushClipRect(window->InnerClipRect.Min, window->InnerClipRect.Max, false);
        draw_card_background(parent, a, b);
        parent->PopClipRect();
    }
    else
        window->StateStorage.SetFloat(frame.height_id, bottom - frame.start.y);

    ImGui::Dummy(ImVec2(0.f, std::fmax(0.f, mt.card_gap - mt.row_gap * 2.f)));
    ImGui::PopID();
}

bool ui::begin_columns(const char* id, int count)
{
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(m().card_gap * 0.5f, 0.f));
    const bool open = ImGui::BeginTable(id, std::max(1, count), ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoPadOuterX);
    ImGui::PopStyleVar();
    return open;
}

void ui::next_column()
{
    ImGui::TableNextColumn();
}

void ui::end_columns()
{
    ImGui::EndTable();
}

bool ui::feature(const char* label, bool* value, keys::bind* bind, void (*options)())
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return false;
    ImGuiContext& g = *GImGui;
    const metrics& mt = m();
    ImGui::PushID(label);
    const ImGuiID id = window->GetID("##row");
    ImRect bb;
    const bool visible = add_row(id, mt.row, bb);
    const bool has_gear = bind || options;
    const float w = bb.GetWidth();
    const ImVec2 pos = bb.Min;
    const ImVec2 gear_pos(pos.x + w - mt.switch_width - mt.gear_gap - mt.gear, pos.y + (mt.row - mt.gear) * 0.5f);
    bool changed = false;

    if (visible)
    {
        bool hovered = false, held = false;
        const bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        const bool gear_hovered = has_gear && hovered && ImGui::IsMouseHoveringRect(ImVec2(gear_pos.x - mt.space_xs, pos.y), ImVec2(gear_pos.x + mt.gear + mt.space_xs, pos.y + mt.row));
        if (pressed)
        {
            const bool right = g.IO.MouseReleased[ImGuiMouseButton_Right];
            if (has_gear && (right || gear_hovered))
                ImGui::OpenPopup("##opts");
            else if (!right)
            {
                *value = !*value;
                changed = true;
                ImGui::MarkItemEdited(id);
            }
        }

        ImDrawList* dl = window->DrawList;
        const float on = animate("##on", *value ? 1.f : 0.f);
        const float hover = animate("##hover", hovered ? 1.f : 0.f, 20.f);
        const float label_right = has_gear ? gear_pos.x - mt.space_sm : pos.x + w - mt.switch_width - mt.space_sm;
        float right = gear_pos.x - mt.space_sm;
        if (has_gear)
            svg::Stroke(dl, icon::Gear, gear_pos, mt.gear, gear_hovered ? tone(token::text) : tone(token::text_faint), mt.stroke_nav);
        float text_limit = label_right;
        if (bind && bind->key && (bind->type == keys::mode::toggle || bind->type == keys::mode::hold))
        {
            const char* key = keys::key_name(bind->key);
            const ImVec2 ts = text_size(font_role::badge, key);
            const ImVec2 a(right - ts.x - mt.key_badge_pad * 2.f, pos.y + (mt.row - mt.key_badge_height) * 0.5f);
            const ImVec2 b(right, a.y + mt.key_badge_height);
            dl->AddRectFilled(a, b, tone(token::control), mt.radius_xs);
            text(dl, font_role::badge, ImVec2(a.x + mt.key_badge_pad, centered_y(a.y, mt.key_badge_height, font_role::badge)), tone(token::text_dim), key);
            text_limit = a.x - mt.space_sm;
        }
        dl->PushClipRect(pos, ImVec2(std::fmax(pos.x, text_limit), bb.Max.y), true);
        text(dl, font_role::body, ImVec2(pos.x, centered_y(pos.y, mt.row, font_role::body)), theme::mix(tone(token::text_dim), tone(token::text), std::fmax(on, hover * 0.7f)), label, visible_end(label));
        dl->PopClipRect();

        const float sw = mt.switch_width, sh = mt.switch_height;
        const ImVec2 ta(pos.x + w - sw, pos.y + (mt.row - sh) * 0.5f), tb(ta.x + sw, ta.y + sh);
        render::Gradient(dl, ta, tb, theme::mix(tone(token::control), theme::accent(), on), theme::mix(tone(token::control), theme::mix(theme::accent(), white(), 0.25f), on), sh * 0.5f, render::Horizontal);
        if (on < 0.99f)
            dl->AddRect(ta, tb, tone(token::border, 1.f - on), sh * 0.5f, 0, mt.hairline);
        const float r = std::fmax(1.f, sh * 0.5f - mt.switch_knob_inset);
        const ImVec2 knob(ta.x + sh * 0.5f + on * (sw - sh), ta.y + sh * 0.5f);
        dl->AddCircleFilled(ImVec2(knob.x, knob.y + mt.hairline), r + 0.5f, tone(token::shadow, 60.f / 255.f), 24);
        dl->AddCircleFilled(knob, r, theme::mix(tone(token::text_dim), white(), on), 24);
    }

    if (has_gear)
    {
        const char* end = visible_end(label);
        char title[128];
        const int length = static_cast<int>(std::min<std::ptrdiff_t>(end - label, static_cast<std::ptrdiff_t>(sizeof(title) - 1)));
        std::memcpy(title, label, static_cast<std::size_t>(length));
        title[length] = 0;
        popover(title, bind, options, ImVec2(bb.Max.x, bb.Max.y + mt.space_xs));
    }
    ImGui::PopID();
    return changed;
}

bool ui::toggle(const char* label, bool* value)
{
    return feature(label, value, nullptr, nullptr);
}

bool ui::slider(const char* label, float* value, float min, float max, const char* format)
{
    return slider_impl(label, ImGuiDataType_Float, value, &min, &max, format);
}

bool ui::slider(const char* label, int* value, int min, int max, const char* format)
{
    return slider_impl(label, ImGuiDataType_S32, value, &min, &max, format);
}

bool ui::begin_combo(const char* id_label, const char* preview, float width)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return false;
    const metrics& mt = m();
    const ImGuiID id = window->GetID(id_label);
    const ImVec2 pos = window->DC.CursorPos;
    const float w = width > 0.f ? width : std::fmax(1.f, ImGui::GetContentRegionAvail().x);
    const ImRect bb(pos, ImVec2(pos.x + w, pos.y + mt.control));
    ImGui::ItemSize(bb.GetSize());
    if (!ImGui::ItemAdd(bb, id))
        return false;
    const ImGuiID popup_id = ImHashStr("##ComboPopup", 0, id);
    bool popup_open = ImGui::IsPopupOpen(popup_id, ImGuiPopupFlags_None);
    bool hovered = false, held = false;
    const bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
    if (pressed && !popup_open)
    {
        ImGui::OpenPopupEx(popup_id, ImGuiPopupFlags_None);
        popup_open = true;
    }

    ImGui::PushID(id_label);
    const float hv = animate("##hv", hovered || popup_open ? 1.f : 0.f, 20.f);
    ImGui::PopID();
    ImDrawList* dl = window->DrawList;
    dl->AddRectFilled(bb.Min, bb.Max, theme::mix(tone(token::control), tone(token::control_hover), hv), mt.radius_frame);
    dl->AddRect(bb.Min, bb.Max, popup_open ? theme::accent(0.55f) : tone(token::border), mt.radius_frame, 0, mt.hairline);
    const float text_right = bb.Max.x - mt.popup_pad - mt.icon_sm - mt.space_xs;
    dl->PushClipRect(bb.Min, ImVec2(std::fmax(bb.Min.x, text_right), bb.Max.y), true);
    text(dl, font_role::body, ImVec2(bb.Min.x + mt.popup_pad, centered_y(bb.Min.y, mt.control, font_role::body)), tone(token::text), preview ? preview : "");
    dl->PopClipRect();
    draw_chevron(dl, bb, tone(token::text_dim));

    if (!popup_open)
        return false;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(mt.space_xs, mt.space_xs));
    const bool open = ImGui::BeginComboPopup(popup_id, bb, ImGuiComboFlags_None);
    ImGui::PopStyleVar();
    return open;
}

bool ui::combo_item(const char* label, bool selected)
{
    const bool pressed = ImGui::Selectable(label, selected, 0, ImVec2(0.f, m().list_item));
    if (selected && ImGui::IsWindowAppearing())
        ImGui::SetItemDefaultFocus();
    return pressed;
}

void ui::end_combo()
{
    ImGui::EndCombo();
}

bool ui::combo(const char* label, int* value, const char* const* items, int count)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return false;
    const metrics& mt = m();
    ImGui::PushID(label);
    const ImVec2 pos = window->DC.CursorPos;
    const float w = std::fmax(1.f, ImGui::GetContentRegionAvail().x);
    const float cw = std::fmin(w, std::fmax(mt.combo_min, w * ratio::combo_width));
    ImDrawList* dl = window->DrawList;
    dl->PushClipRect(pos, ImVec2(std::fmax(pos.x, pos.x + w - cw - mt.space_sm), pos.y + mt.control), true);
    text(dl, font_role::body, ImVec2(pos.x, centered_y(pos.y, mt.control, font_role::body)), tone(token::text_dim), label, visible_end(label));
    dl->PopClipRect();
    const char* preview = *value >= 0 && *value < count ? items[*value] : "";
    ImGui::SetCursorScreenPos(ImVec2(pos.x + w - cw, pos.y));
    bool changed = false;
    if (begin_combo("##combo", preview, cw))
    {
        for (int i = 0; i < count; ++i)
        {
            ImGui::PushID(i);
            if (combo_item(items[i], i == *value) && *value != i)
            {
                *value = i;
                changed = true;
            }
            ImGui::PopID();
        }
        end_combo();
    }
    ImGui::PopID();
    return changed;
}

bool ui::color_edit(const char* label, float* rgba)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return false;
    const metrics& mt = m();
    ImGui::PushID(label);
    const ImGuiID id = window->GetID("##swatch");
    ImRect bb;
    bool changed = false;
    if (add_row(id, mt.control, bb))
    {
        bool hovered = false, held = false;
        if (ImGui::ButtonBehavior(bb, id, &hovered, &held))
            ImGui::OpenPopup("##picker");
        ImDrawList* dl = window->DrawList;
        const ImVec2 a(bb.Max.x - mt.swatch_width, bb.Min.y + (mt.control - mt.swatch_height) * 0.5f);
        const ImVec2 b(bb.Max.x, a.y + mt.swatch_height);
        dl->PushClipRect(bb.Min, ImVec2(std::fmax(bb.Min.x, a.x - mt.space_sm), bb.Max.y), true);
        text(dl, font_role::body, ImVec2(bb.Min.x, centered_y(bb.Min.y, mt.control, font_role::body)), tone(token::text_dim), label, visible_end(label));
        dl->PopClipRect();
        const float alpha = std::clamp(ImGui::GetStyle().Alpha, 0.f, 1.f);
        const ImVec4 c(std::clamp(rgba[0], 0.f, 1.f), std::clamp(rgba[1], 0.f, 1.f), std::clamp(rgba[2], 0.f, 1.f), std::clamp(rgba[3], 0.f, 1.f) * alpha);
        ImGui::RenderColorRectWithAlphaCheckerboard(dl, a, b, ImGui::ColorConvertFloat4ToU32(c), alpha, mt.checker, ImVec2(0.f, 0.f), mt.radius_xs);
        dl->AddRect(a, b, hovered ? tone(token::text_dim) : tone(token::border), mt.radius_xs, 0, mt.hairline);
    }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(mt.popup_pad, mt.popup_pad));
    if (ImGui::BeginPopup("##picker"))
    {
        ImGui::SetNextItemWidth(mt.color_picker);
        changed = ImGui::ColorPicker4("##pick", rgba, ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoSmallPreview | ImGuiColorEditFlags_DisplayHex);
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();
    ImGui::PopID();
    return changed;
}

bool ui::flags(const char* label, std::uint32_t* mask, const char* const* names, const std::uint32_t* bits, int count)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return false;
    const metrics& mt = m();
    ImGui::PushID(label);
    const ImVec2 pos = window->DC.CursorPos;
    const float w = std::fmax(1.f, ImGui::GetContentRegionAvail().x);
    text(window->DrawList, font_role::body, ImVec2(pos.x, centered_y(pos.y, mt.row_dense, font_role::body)), tone(token::text_dim), label, visible_end(label));
    ImGui::Dummy(ImVec2(w, mt.row_dense));
    const float right = pos.x + w;
    bool changed = false;
    float last_max = pos.x;
    for (int i = 0; i < count; ++i)
    {
        const float chip_w = text_size(font_role::body, names[i]).x + mt.chip_pad * 2.f;
        if (i > 0 && last_max + mt.space_xs + chip_w <= right)
            ImGui::SameLine(0.f, mt.space_xs);
        ImGui::PushID(i);
        const bool active = (*mask & bits[i]) == bits[i];
        if (chip(names[i], active))
        {
            *mask = active ? (*mask & ~bits[i]) : (*mask | bits[i]);
            changed = true;
        }
        ImGui::PopID();
        last_max = ImGui::GetItemRectMax().x;
    }
    ImGui::PopID();
    return changed;
}

bool ui::segmented(const char* id_label, int* value, const char* const* labels, int count, float width)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems || count <= 0)
        return false;
    const metrics& mt = m();
    ImGui::PushID(id_label);
    const ImVec2 pos = window->DC.CursorPos;
    const float w = width > 0.f ? width : std::fmax(1.f, ImGui::GetContentRegionAvail().x);
    const float h = mt.segmented_height;
    const float seg = w / static_cast<float>(count);
    const ImRect total(pos, ImVec2(pos.x + w, pos.y + h));
    ImGui::ItemSize(total.GetSize());
    if (!ImGui::IsRectVisible(total.Min, total.Max))
    {
        ImGui::ItemAdd(total, window->GetID("##segmented"));
        ImGui::PopID();
        return false;
    }
    ImDrawList* dl = window->DrawList;
    dl->AddRectFilled(total.Min, total.Max, tone(token::control), mt.segmented_radius);
    const int current = std::clamp(*value, 0, count - 1);
    const float at = animate("##seg", static_cast<float>(current), 16.f);
    const float inset = mt.segmented_inset;
    const ImVec2 pa(pos.x + inset + at * seg, pos.y + inset), pb(pa.x + seg - inset * 2.f, pos.y + h - inset);
    const float pill_radius = std::fmax(0.f, mt.segmented_radius - inset * 0.5f);
    render::Gradient(dl, pa, pb, theme::mix(tone(token::control_hover), white(), 0.06f), tone(token::control_hover), pill_radius);
    dl->AddRect(pa, pb, theme::accent(0.45f), pill_radius, 0, mt.hairline);
    bool changed = false;
    for (int i = 0; i < count; ++i)
    {
        const ImRect bb(ImVec2(pos.x + i * seg, pos.y), ImVec2(pos.x + (i + 1) * seg, pos.y + h));
        const ImGuiID id = window->GetID(i);
        bool hovered = false, held = false;
        if (ImGui::ItemAdd(bb, id) && ImGui::ButtonBehavior(bb, id, &hovered, &held) && *value != i)
        {
            *value = i;
            changed = true;
            ImGui::MarkItemEdited(id);
        }
        const ImVec2 ts = text_size(font_role::body, labels[i]);
        const ImU32 col = i == current ? tone(token::text) : hovered ? theme::mix(tone(token::text_dim), tone(token::text), 0.5f) : tone(token::text_dim);
        dl->PushClipRect(bb.Min, bb.Max, true);
        text(dl, font_role::body, ImVec2(bb.Min.x + (seg - ts.x) * 0.5f, centered_y(pos.y, h, font_role::body)), col, labels[i]);
        dl->PopClipRect();
    }
    ImGui::PopID();
    return changed;
}

bool ui::button(const char* label, ImVec2 size, const char* icon_path, button_kind kind)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return false;
    const metrics& mt = m();
    const bool danger = kind == button_kind::destructive;
    const bool primary = kind == button_kind::primary;
    const ImGuiID id = window->GetID(label);
    const ImVec2 pos = window->DC.CursorPos;
    if (size.x <= 0.f)
        size.x = std::fmax(1.f, ImGui::GetContentRegionAvail().x);
    if (size.y <= 0.f)
        size.y = mt.button_height;
    const ImRect bb(pos, ImVec2(pos.x + size.x, pos.y + size.y));
    ImGui::ItemSize(size);
    if (!ImGui::ItemAdd(bb, id))
        return false;
    bool hovered = false, held = false;
    const bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
    ImGui::PushID(label);
    const float hv = animate("##hv", hovered ? 1.f : 0.f, 20.f);
    ImGui::PopID();
    ImU32 bg = danger ? theme::mix(tone(token::danger, 0.14f), tone(token::danger, 0.28f), hv) : primary ? theme::mix(theme::accent(0.85f), theme::accent(), hv) : theme::mix(tone(token::control), tone(token::control_hover), hv);
    if (held)
        bg = danger ? tone(token::danger, 0.4f) : primary ? theme::mix(theme::accent(), white(), 0.15f) : theme::accent(0.3f);
    ImDrawList* dl = window->DrawList;
    render::Gradient(dl, bb.Min, bb.Max, theme::mix(bg, white(), primary ? 0.18f : 0.04f), bg, mt.radius_md);
    if (!primary)
        dl->AddRect(bb.Min, bb.Max, danger ? tone(token::danger, 0.35f) : tone(token::border), mt.radius_md, 0, mt.hairline);
    const char* end = visible_end(label);
    const ImVec2 ts = text_size(font_role::body, label, end);
    const float iw = icon_path ? mt.icon_md + mt.inner_gap_x : 0.f;
    float x = pos.x + std::fmax(mt.space_sm, (size.x - ts.x - iw) * 0.5f);
    const ImU32 fg = danger ? tone(token::danger) : primary ? tone(token::on_accent) : tone(token::text);
    dl->PushClipRect(bb.Min, bb.Max, true);
    if (icon_path)
    {
        svg::Stroke(dl, icon_path, ImVec2(x, pos.y + (size.y - mt.icon_md) * 0.5f), mt.icon_md, fg, mt.stroke);
        x += iw;
    }
    text(dl, font_role::body, ImVec2(x, centered_y(pos.y, size.y, font_role::body)), fg, label, end);
    dl->PopClipRect();
    return pressed;
}

bool ui::chip(const char* label, bool active)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return false;
    const metrics& mt = m();
    const ImGuiID id = window->GetID(label);
    const char* end = visible_end(label);
    const ImVec2 ts = text_size(font_role::body, label, end);
    const ImVec2 size(ts.x + mt.chip_pad * 2.f, mt.control);
    const ImVec2 pos = window->DC.CursorPos;
    const ImRect bb(pos, ImVec2(pos.x + size.x, pos.y + size.y));
    ImGui::ItemSize(size);
    if (!ImGui::ItemAdd(bb, id))
        return false;
    bool hovered = false, held = false;
    const bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
    ImGui::PushID(label);
    const float on = animate("##on", active ? 1.f : 0.f, 16.f);
    const float hv = animate("##hv", hovered ? 1.f : 0.f, 20.f);
    ImGui::PopID();
    ImDrawList* dl = window->DrawList;
    const float rounding = size.y * 0.5f;
    dl->AddRectFilled(bb.Min, bb.Max, theme::mix(theme::mix(tone(token::control), tone(token::control_hover), hv), theme::accent(0.18f), on), rounding);
    dl->AddRect(bb.Min, bb.Max, theme::mix(tone(token::border), theme::accent(0.7f), on), rounding, 0, mt.hairline);
    text(dl, font_role::body, ImVec2(pos.x + mt.chip_pad, centered_y(pos.y, size.y, font_role::body)), theme::mix(tone(token::text_dim), tone(token::text), std::fmax(on, hv)), label, end);
    return pressed;
}

bool ui::search_box(const char* id, char* buffer, std::size_t size, const char* hint, float width)
{
    const metrics& mt = m();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(mt.search_pad, std::fmax(0.f, std::floor((mt.control - ImGui::GetFontSize()) * 0.5f))));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, mt.segmented_radius);
    ImGui::SetNextItemWidth(std::fmax(1.f, width));
    const bool changed = ImGui::InputTextWithHint(id, hint, buffer, size);
    ImGui::PopStyleVar(2);
    const float h = ImGui::GetItemRectSize().y;
    svg::Stroke(ImGui::GetWindowDrawList(), search_icon, ImVec2(pos.x + mt.popup_pad, pos.y + (h - mt.icon_md) * 0.5f), mt.icon_md, tone(token::text_faint), mt.stroke);
    return changed;
}

bool ui::input_int(const char* label, int* value, int min, int max)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return false;
    const metrics& mt = m();
    ImGui::PushID(label);
    const ImVec2 pos = window->DC.CursorPos;
    const float w = std::fmax(1.f, ImGui::GetContentRegionAvail().x);
    const float iw = std::fmin(w, std::fmax(mt.combo_min, w * ratio::input_width));
    ImDrawList* dl = window->DrawList;
    dl->PushClipRect(pos, ImVec2(std::fmax(pos.x, pos.x + w - iw - mt.space_sm), pos.y + mt.row), true);
    text(dl, font_role::body, ImVec2(pos.x, centered_y(pos.y, mt.row, font_role::body)), tone(token::text_dim), label, visible_end(label));
    dl->PopClipRect();
    ImGui::SetCursorScreenPos(ImVec2(pos.x + w - iw, pos.y + (mt.row - ImGui::GetFrameHeight()) * 0.5f));
    ImGui::SetNextItemWidth(iw);
    int v = *value;
    bool changed = false;
    if (ImGui::InputInt("##value", &v, 1, 10))
    {
        v = std::clamp(v, std::min(min, max), std::max(min, max));
        if (v != *value)
        {
            *value = v;
            changed = true;
        }
    }
    ImGui::SetCursorScreenPos(pos);
    ImGui::Dummy(ImVec2(w, mt.row));
    ImGui::PopID();
    return changed;
}

bool ui::input_text(const char* label, char* buffer, std::size_t size, const char* hint)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return false;
    const metrics& mt = m();
    ImGui::PushID(label);
    const ImVec2 pos = window->DC.CursorPos;
    const float w = std::fmax(1.f, ImGui::GetContentRegionAvail().x);
    const char* end = visible_end(label);
    const bool has_label = end != label;
    const float iw = has_label ? std::fmin(w, std::fmax(mt.combo_min, w * ratio::input_width)) : w;
    if (has_label)
    {
        ImDrawList* dl = window->DrawList;
        dl->PushClipRect(pos, ImVec2(std::fmax(pos.x, pos.x + w - iw - mt.space_sm), pos.y + mt.row), true);
        text(dl, font_role::body, ImVec2(pos.x, centered_y(pos.y, mt.row, font_role::body)), tone(token::text_dim), label, end);
        dl->PopClipRect();
    }
    ImGui::SetCursorScreenPos(ImVec2(pos.x + w - iw, pos.y + (mt.row - ImGui::GetFrameHeight()) * 0.5f));
    ImGui::SetNextItemWidth(iw);
    const bool changed = ImGui::InputTextWithHint("##text", hint ? hint : "", buffer, size);
    ImGui::SetCursorScreenPos(pos);
    ImGui::Dummy(ImVec2(w, mt.row));
    ImGui::PopID();
    return changed;
}

bool ui::key_button(const char* label, keys::bind* bind)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return false;
    const metrics& mt = m();
    ImGui::PushID(label);
    const ImVec2 pos = window->DC.CursorPos;
    const float w = std::fmax(1.f, ImGui::GetContentRegionAvail().x);
    const float bw = std::fmin(w, mt.key_button);
    const float h = mt.control;
    ImDrawList* dl = window->DrawList;
    dl->PushClipRect(pos, ImVec2(std::fmax(pos.x, pos.x + w - bw - mt.space_sm), pos.y + h), true);
    text(dl, font_role::body, ImVec2(pos.x, centered_y(pos.y, h, font_role::body)), tone(token::text_dim), label, visible_end(label));
    dl->PopClipRect();
    const bool capturing = keys::capturing == bind;
    char caption[64];
    std::snprintf(caption, sizeof(caption), "%s##key", capturing ? "Press a key..." : bind->key ? keys::key_name(bind->key) : "None");
    ImGui::SetCursorScreenPos(ImVec2(pos.x + w - bw, pos.y));
    const bool pressed = button(caption, ImVec2(bw, h), icon::Keyboard, capturing ? button_kind::primary : button_kind::normal);
    if (pressed)
        keys::capturing = capturing ? nullptr : bind;
    ImGui::SetCursorScreenPos(pos);
    ImGui::Dummy(ImVec2(w, h));
    ImGui::PopID();
    return pressed;
}

void ui::bind_editor(keys::bind* bind)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems || !bind)
        return;
    const metrics& mt = m();
    ImGui::PushID(bind);
    key_button("Key", bind);
    static const char* const modes[] = { "Toggle", "Hold", "Always", "Off" };
    int mode = std::clamp(static_cast<int>(bind->type), 0, 3);
    if (segmented("##mode", &mode, modes, 4, 0.f))
    {
        bind->type = static_cast<keys::mode>(mode);
        bind->toggled = false;
    }
    const ImGuiID id = window->GetID("##clear");
    const ImVec2 pos = window->DC.CursorPos;
    const ImVec2 clear_size = text_size(font_role::caption, "Clear");
    const float h = mt.row_dense;
    const ImRect bb(pos, ImVec2(pos.x + clear_size.x + mt.space_sm, pos.y + h));
    ImGui::ItemSize(ImVec2(std::fmax(1.f, ImGui::GetContentRegionAvail().x), h));
    bool hovered = false;
    if (ImGui::ItemAdd(bb, id))
    {
        bool held = false;
        if (ImGui::ButtonBehavior(bb, id, &hovered, &held))
        {
            bind->key = 0;
            bind->toggled = false;
            if (keys::capturing == bind)
                keys::capturing = nullptr;
        }
    }
    ImDrawList* dl = window->DrawList;
    const float ty = centered_y(pos.y, h, font_role::caption);
    text(dl, font_role::caption, ImVec2(pos.x, ty), hovered ? tone(token::danger) : tone(token::text_faint), "Clear");
    text(dl, font_role::caption, ImVec2(bb.Max.x + mt.space_sm, ty), tone(token::text_faint), "Esc while choosing = none");
    ImGui::PopID();
}

void ui::value(const char* label, const char* format, ...)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return;
    char buffer[160];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    const metrics& mt = m();
    const ImVec2 pos = window->DC.CursorPos;
    const float w = std::fmax(1.f, ImGui::GetContentRegionAvail().x);
    const float h = mt.row_dense;
    ImGui::Dummy(ImVec2(w, h));
    if (!ImGui::IsItemVisible())
        return;
    ImDrawList* dl = window->DrawList;
    const float ty = centered_y(pos.y, h, font_role::body);
    const ImVec2 ts = text_size(font_role::body, buffer);
    const float value_x = std::fmax(pos.x + w * ratio::title_split, pos.x + w - ts.x);
    dl->PushClipRect(pos, ImVec2(std::fmax(pos.x, value_x - mt.space_sm), pos.y + h), true);
    text(dl, font_role::body, ImVec2(pos.x, ty), tone(token::text_dim), label, visible_end(label));
    dl->PopClipRect();
    dl->PushClipRect(ImVec2(value_x, pos.y), ImVec2(pos.x + w, pos.y + h), true);
    text(dl, font_role::body, ImVec2(value_x, ty), tone(token::text), buffer);
    dl->PopClipRect();
}

void ui::note(const char* str)
{
    if (!str || !*str)
        return;
    font_scope scope(font_role::caption);
    ImGui::PushStyleColor(ImGuiCol_Text, theme::vec(tone(token::text_faint)));
    ImGui::PushTextWrapPos(0.f);
    ImGui::TextUnformatted(str);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

void ui::divider()
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return;
    const metrics& mt = m();
    const ImVec2 pos = window->DC.CursorPos;
    const float w = std::fmax(1.f, ImGui::GetContentRegionAvail().x);
    const float y = std::floor(pos.y + mt.space_xs) + 0.5f;
    window->DrawList->AddLine(ImVec2(pos.x, y), ImVec2(pos.x + w, y), tone(token::border), mt.hairline);
    ImGui::Dummy(ImVec2(w, mt.space_sm));
}

bool ui::toasts_active()
{
    const std::uint64_t now = GetTickCount64();
    const auto& lines = recent_lines();
    return !lines.empty() && now - lines.back().time < toast_life;
}

void ui::toasts()
{
    const metrics& mt = m();
    const std::uint64_t now = GetTickCount64();
    const auto& lines = recent_lines();
    if (lines.empty())
        return;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const ImVec2 screen = ImGui::GetIO().DisplaySize;
    float y = screen.y - mt.toast_margin;
    int shown = 0;
    for (auto it = lines.rbegin(); it != lines.rend() && shown < toast_limit; ++it)
    {
        if (it->time > now)
            continue;
        const std::uint64_t age = now - it->time;
        if (age >= toast_life)
            break;
        const float in = ease(static_cast<float>(age) / static_cast<float>(toast_fade_in));
        const float out = ease(static_cast<float>(toast_life - age) / static_cast<float>(toast_fade_out));
        const float a = std::fmin(in, out);
        const float w = mt.toast_width, h = mt.toast_height;
        const float x = screen.x - mt.toast_margin - w + (1.f - in) * mt.toast_slide;
        y -= h;
        const ImVec2 p0(x, y), p1(x + w, y + h);
        soft_shadow(dl, ImVec2(p0.x + mt.space_xxs, p0.y + mt.space_xs), ImVec2(p1.x - mt.space_xxs, p1.y + mt.space_xxs), mt.radius_card, mt.toast_shadow, 0.45f * a);
        dl->AddRectFilled(p0, p1, theme::mul_alpha(theme::raw(token::surface), 0.97f * a), mt.radius_card);
        dl->AddRect(p0, p1, theme::mul_alpha(theme::raw(token::border), a), mt.radius_card, 0, mt.hairline);
        const token level_tone = it->level == logs::Error ? token::danger : it->level == logs::Warning ? token::warn : it->level == logs::Success ? token::good : token::accent;
        const char* glyph = it->level == logs::Error ? icon::Cross : it->level == logs::Success ? icon::Check : icon::Alert;
        const ImU32 col = theme::mul_alpha(theme::raw(level_tone), a);
        const float box_y = p0.y + (h - mt.toast_icon_box) * 0.5f;
        dl->AddRectFilled(ImVec2(p0.x + mt.toast_icon_x, box_y), ImVec2(p0.x + mt.toast_icon_x + mt.toast_icon_box, box_y + mt.toast_icon_box), theme::with_alpha(theme::raw(level_tone), 40.f / 255.f * a), mt.radius_frame);
        svg::Stroke(dl, glyph, ImVec2(p0.x + mt.toast_icon_x + (mt.toast_icon_box - mt.icon_md) * 0.5f, box_y + (mt.toast_icon_box - mt.icon_md) * 0.5f), mt.icon_md, col, mt.stroke_toast);
        dl->PushClipRect(ImVec2(p0.x + mt.toast_text_x - mt.space_xxs, p0.y), ImVec2(p1.x - mt.popup_pad, p1.y), true);
        text(dl, font_role::body, ImVec2(p0.x + mt.toast_text_x, centered_y(p0.y, h, font_role::body)), theme::mul_alpha(theme::raw(token::text), a), it->text);
        dl->PopClipRect();
        y -= mt.toast_gap;
        ++shown;
    }
}
