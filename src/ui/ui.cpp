#include "ui.h"
#include "svg.h"
#include "render.h"
#include "icons_svg.h"
#include "../misc.h"
#include "../core/log.h"
#include <Windows.h>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>

namespace
{
    const ImVec4 kTones[] = {
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

    constexpr float kRow = 30.f;
    constexpr float kRadius = 8.f;

    std::string Visible(const char* label)
    {
        const char* hash = strstr(label, "##");
        return hash ? std::string(label, hash) : std::string(label);
    }

    ImVec2 Add(ImVec2 a, ImVec2 b)
    {
        return ImVec2(a.x + b.x, a.y + b.y);
    }

    void Popover(const char* title, misc::Bind* bind, void (*options)(), ImVec2 anchor)
    {
        const bool open = ImGui::IsPopupOpen("##opts");
        const float t = ui::Animate("##popanim", open ? 1.f : 0.f, 18.f);
        ImGui::SetNextWindowPos(anchor, ImGuiCond_Appearing, ImVec2(1.f, 0.f));
        ImGui::SetNextWindowSize(ImVec2(270.f, 0.f));
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * ui::Ease(t));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.f, 12.f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.f, 8.f));
        if (ImGui::BeginPopup("##opts"))
        {
            ImGui::PushFont(ui::fonts.semibold, 13.f);
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(ui::Color(ui::TextDim)));
            ImGui::TextUnformatted(title);
            ImGui::PopStyleColor();
            ImGui::PopFont();
            if (options)
                options();
            if (bind)
            {
                if (options)
                {
                    const ImVec2 p = ImGui::GetCursorScreenPos();
                    ImGui::GetWindowDrawList()->AddLine(p, ImVec2(p.x + ImGui::GetContentRegionAvail().x, p.y), ui::Color(ui::Border));
                    ImGui::Dummy(ImVec2(0.f, 2.f));
                }
                ui::KeybindEditor(bind);
            }
            ImGui::EndPopup();
        }
        else if (bind && misc::capturing == bind)
            misc::capturing = nullptr;
        ImGui::PopStyleVar(3);
    }
}

ImU32 ui::Color(Tone tone, float alpha)
{
    ImVec4 c = kTones[tone];
    c.w *= alpha;
    return ImGui::ColorConvertFloat4ToU32(c);
}

ImU32 ui::Accent(float alpha)
{
    return ImGui::ColorConvertFloat4ToU32(ImVec4(accent[0], accent[1], accent[2], accent[3] * alpha));
}

ImU32 ui::Mix(ImU32 a, ImU32 b, float t)
{
    const ImVec4 x = ImGui::ColorConvertU32ToFloat4(a), y = ImGui::ColorConvertU32ToFloat4(b);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(x.x + (y.x - x.x) * t, x.y + (y.y - x.y) * t, x.z + (y.z - x.z) * t, x.w + (y.w - x.w) * t));
}

float ui::Ease(float t)
{
    t = t < 0.f ? 0.f : t > 1.f ? 1.f : t;
    return 1.f - (1.f - t) * (1.f - t) * (1.f - t);
}

float ui::Animate(const char* key, float target, float speed)
{
    ImGuiStorage* st = ImGui::GetStateStorage();
    const ImGuiID id = ImGui::GetID(key);
    float v = st->GetFloat(id, target);
    if (reduceMotion)
        v = target;
    else
    {
        const float k = ImGui::GetIO().DeltaTime * speed;
        v += (target - v) * (k > 1.f ? 1.f : k);
        if (std::fabs(target - v) < 0.001f)
            v = target;
    }
    st->SetFloat(id, v);
    return v;
}

void ui::LoadFonts()
{
    ImGuiIO& io = ImGui::GetIO();
    char dir[MAX_PATH]{};
    GetWindowsDirectoryA(dir, MAX_PATH);
    const std::string base = std::string(dir) + "\\Fonts\\";
    auto exists = [](const std::string& p) { return GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES; };
    const std::string primary = base + "bahnschrift.ttf", fallback = base + "segoeui.ttf", symbols = base + "seguisym.ttf";
    ImFontConfig cfg;
    cfg.OversampleH = 1;
    cfg.OversampleV = 1;
    cfg.PixelSnapH = true;
    if (exists(primary))
        fonts.regular = io.Fonts->AddFontFromFileTTF(primary.c_str(), 15.f, &cfg);
    else if (exists(fallback))
        fonts.regular = io.Fonts->AddFontFromFileTTF(fallback.c_str(), 15.f, &cfg);
    if (!fonts.regular)
        fonts.regular = io.Fonts->AddFontDefault();
    else
    {
        ImFontConfig merge = cfg;
        merge.MergeMode = true;
        if (exists(primary) && exists(fallback))
            io.Fonts->AddFontFromFileTTF(fallback.c_str(), 15.f, &merge);
        if (exists(symbols))
            io.Fonts->AddFontFromFileTTF(symbols.c_str(), 15.f, &merge);
    }
    fonts.semibold = fonts.regular;
    io.FontDefault = fonts.regular;
}

void ui::ApplyStyle()
{
    ImGuiStyle& s = ImGui::GetStyle();
    s.FontSizeBase = 15.f;
    s.WindowRounding = 12.f;
    s.ChildRounding = 10.f;
    s.FrameRounding = 7.f;
    s.PopupRounding = 10.f;
    s.GrabRounding = 6.f;
    s.TabRounding = 6.f;
    s.ScrollbarRounding = 6.f;
    s.ScrollbarSize = 6.f;
    s.GrabMinSize = 10.f;
    s.WindowBorderSize = 1.f;
    s.ChildBorderSize = 1.f;
    s.PopupBorderSize = 1.f;
    s.FrameBorderSize = 0.f;
    s.ItemSpacing = ImVec2(8.f, 6.f);
    s.ItemInnerSpacing = ImVec2(6.f, 4.f);
    s.FramePadding = ImVec2(10.f, 6.f);
    s.WindowPadding = ImVec2(14.f, 14.f);
    s.CellPadding = ImVec2(5.f, 0.f);
    s.SelectableTextAlign = ImVec2(0.f, 0.5f);
    s.AntiAliasedLines = true;
    s.AntiAliasedFill = true;
    ImVec4* c = s.Colors;
    const ImVec4 acc(accent[0], accent[1], accent[2], 1.f);
    auto tone = [](Tone t, float a = 1.f) { ImVec4 v = kTones[t]; v.w = a; return v; };
    auto tint = [&](float a) { return ImVec4(acc.x, acc.y, acc.z, a); };
    c[ImGuiCol_Text] = tone(Text);
    c[ImGuiCol_TextDisabled] = tone(TextDim);
    c[ImGuiCol_WindowBg] = tone(Background);
    c[ImGuiCol_ChildBg] = ImVec4(0.f, 0.f, 0.f, 0.f);
    c[ImGuiCol_PopupBg] = tone(Surface, 0.99f);
    c[ImGuiCol_Border] = tone(Border);
    c[ImGuiCol_BorderShadow] = ImVec4(0.f, 0.f, 0.f, 0.f);
    c[ImGuiCol_FrameBg] = tone(Control);
    c[ImGuiCol_FrameBgHovered] = tone(ControlHover);
    c[ImGuiCol_FrameBgActive] = tone(ControlHover);
    c[ImGuiCol_TitleBg] = tone(Rail);
    c[ImGuiCol_TitleBgActive] = tone(Rail);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0.f, 0.f, 0.f, 0.f);
    c[ImGuiCol_ScrollbarGrab] = tone(Border);
    c[ImGuiCol_ScrollbarGrabHovered] = tone(TextFaint);
    c[ImGuiCol_ScrollbarGrabActive] = tint(0.8f);
    c[ImGuiCol_CheckMark] = acc;
    c[ImGuiCol_SliderGrab] = acc;
    c[ImGuiCol_SliderGrabActive] = acc;
    c[ImGuiCol_Button] = tone(Control);
    c[ImGuiCol_ButtonHovered] = tone(ControlHover);
    c[ImGuiCol_ButtonActive] = tint(0.45f);
    c[ImGuiCol_Header] = tint(0.16f);
    c[ImGuiCol_HeaderHovered] = tone(ControlHover);
    c[ImGuiCol_HeaderActive] = tint(0.28f);
    c[ImGuiCol_Separator] = tone(Border);
    c[ImGuiCol_ResizeGrip] = ImVec4(0.f, 0.f, 0.f, 0.f);
    c[ImGuiCol_ResizeGripHovered] = ImVec4(0.f, 0.f, 0.f, 0.f);
    c[ImGuiCol_ResizeGripActive] = ImVec4(0.f, 0.f, 0.f, 0.f);
    c[ImGuiCol_Tab] = tone(Control);
    c[ImGuiCol_TabHovered] = tone(ControlHover);
    c[ImGuiCol_TabSelected] = tint(0.3f);
    c[ImGuiCol_TableBorderLight] = ImVec4(0.f, 0.f, 0.f, 0.f);
    c[ImGuiCol_TableBorderStrong] = ImVec4(0.f, 0.f, 0.f, 0.f);
    c[ImGuiCol_TextSelectedBg] = tint(0.3f);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.f, 0.f, 0.f, 0.45f);
}

ImVec2 ui::Snap(ImVec2 p)
{
    return ImVec2(std::floor(p.x + 0.5f), std::floor(p.y + 0.5f));
}

void ui::Print(ImDrawList* dl, ImVec2 pos, ImU32 color, const char* text)
{
    dl->AddText(Snap(pos), color, text);
}

void ui::Print(ImDrawList* dl, ImFont* font, float size, ImVec2 pos, ImU32 color, const char* text)
{
    dl->AddText(font, size, Snap(pos), color, text);
}

void ui::Heading(ImDrawList* dl, ImVec2 pos, float size, ImU32 color, const char* text)
{
    dl->AddText(fonts.regular, size, Snap(pos), color, text);
}

void ui::RadialGlow(ImDrawList* dl, ImVec2 c, float radius, ImU32 color, float alpha)
{
    render::Glow(dl, c, radius, render::Fade(color | 0xFF000000, alpha));
}

void ui::SoftShadow(ImDrawList* dl, ImVec2 mn, ImVec2 mx, float rounding, float spread, ImU32 color, float alpha)
{
    render::Shadow(dl, mn, mx, rounding, spread, render::Fade(color | 0xFF000000, alpha));
}

void ui::Chrome(ImVec2 pos, ImVec2 size)
{
    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    const float alpha = ImGui::GetStyle().Alpha;
    SoftShadow(bg, ImVec2(pos.x + 4.f, pos.y + 10.f), ImVec2(pos.x + size.x - 4.f, pos.y + size.y + 6.f), 14.f, 34.f, IM_COL32(0, 0, 0, 255), 0.55f * alpha);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float w = size.x - 24.f;
    render::Gradient(dl, ImVec2(pos.x + 12.f, pos.y), ImVec2(pos.x + 12.f + w * 0.5f, pos.y + 1.f), Accent(0.f), Accent(0.9f), 0.f, render::Horizontal);
    render::Gradient(dl, ImVec2(pos.x + 12.f + w * 0.5f, pos.y), ImVec2(pos.x + 12.f + w, pos.y + 1.f), Accent(0.9f), Accent(0.f), 0.f, render::Horizontal);
}

bool ui::BeginCard(const char* title, const char* iconPath)
{
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.f, 12.f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
    const ImVec2 clipMin = ImGui::GetWindowDrawList()->GetClipRectMin(), clipMax = ImGui::GetWindowDrawList()->GetClipRectMax();
    const bool open = ImGui::BeginChild(title, ImVec2(0.f, 0.f), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    {
        const ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
        const ImVec2 we(wp.x + ws.x, wp.y + ws.y);
        dl->PushClipRect(ImVec2(wp.x > clipMin.x ? wp.x : clipMin.x, wp.y > clipMin.y ? wp.y : clipMin.y), ImVec2(we.x < clipMax.x ? we.x : clipMax.x, we.y < clipMax.y ? we.y : clipMax.y), false);
        render::Gradient(dl, wp, we, Mix(Color(Surface), Color(ControlHover), 0.35f), Color(Surface), 10.f);
        render::Border(dl, wp, we, Color(Border, 0.8f), 10.f);
        render::Gradient(dl, ImVec2(wp.x + 10.f, wp.y), ImVec2(we.x - 10.f, wp.y + 1.f), IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 14), 0.f, render::Horizontal);
        dl->PopClipRect();
    }
    const ImVec2 p = ImGui::GetCursorScreenPos();
    float x = p.x;
    if (iconPath)
    {
        svg::Stroke(dl, iconPath, ImVec2(p.x, p.y + 1.f), 16.f, Accent(), 2.f);
        x += 24.f;
    }
    Heading(dl, ImVec2(x, p.y + 1.f), 15.f, Color(Text), Visible(title).c_str());
    ImGui::Dummy(ImVec2(0.f, 22.f));
    return open;
}

void ui::EndCard()
{
    ImGui::EndChild();
    ImGui::Dummy(ImVec2(0.f, 2.f));
}

bool ui::BeginColumns(const char* id)
{
    return ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoPadOuterX);
}

void ui::NextColumn()
{
    ImGui::TableNextColumn();
}

void ui::EndColumns()
{
    ImGui::EndTable();
}

bool ui::Feature(const char* label, bool* value, misc::Bind* bind, void (*options)())
{
    if (bind)
        misc::Register(bind);
    ImGui::PushID(label);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    const bool hasGear = bind || options;
    constexpr float sw = 32.f, sh = 18.f, gear = 17.f;
    const ImVec2 gearPos(pos.x + w - sw - 12.f - gear, pos.y + (kRow - gear) * 0.5f);

    ImGui::InvisibleButton("##row", ImVec2(w, kRow));
    const bool hovered = ImGui::IsItemHovered();
    const bool gearHovered = hasGear && ImGui::IsMouseHoveringRect(ImVec2(gearPos.x - 4.f, pos.y), ImVec2(gearPos.x + gear + 4.f, pos.y + kRow));
    bool changed = false;
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
    {
        if (gearHovered)
            ImGui::OpenPopup("##opts");
        else
        {
            *value = !*value;
            changed = true;
        }
    }
    if (hasGear && ImGui::IsItemClicked(ImGuiMouseButton_Right))
        ImGui::OpenPopup("##opts");

    const float on = Animate("##on", *value ? 1.f : 0.f);
    const float hover = Animate("##hover", hovered ? 1.f : 0.f, 20.f);
    const float th = ImGui::GetTextLineHeight();
    Print(dl, ImVec2(pos.x, pos.y + (kRow - th) * 0.5f), Mix(Color(TextDim), Color(Text), on > hover * 0.7f ? on : hover * 0.7f), Visible(label).c_str());

    float right = gearPos.x - 8.f;
    if (hasGear)
        svg::Stroke(dl, icon::Gear, gearPos, gear, gearHovered ? Color(Text) : Color(TextFaint), 1.9f);
    if (bind && bind->key && bind->mode != misc::Always)
    {
        const char* key = misc::KeyName(bind->key);
        const float fs = 12.f;
        const ImVec2 ts = fonts.regular->CalcTextSizeA(fs, 200.f, 0.f, key);
        const ImVec2 a(right - ts.x - 12.f, pos.y + (kRow - 18.f) * 0.5f), b(right, a.y + 18.f);
        dl->AddRectFilled(a, b, Color(Control), 5.f);
        Print(dl, fonts.regular, fs, ImVec2(a.x + 6.f, a.y + (18.f - ts.y) * 0.5f), Color(TextDim), key);
    }

    const ImVec2 ta(pos.x + w - sw, pos.y + (kRow - sh) * 0.5f), tb(ta.x + sw, ta.y + sh);
    render::Gradient(dl, ta, tb, Mix(Color(Control), Accent(), on), Mix(Color(Control), Mix(Accent(), IM_COL32(255, 255, 255, 255), 0.25f), on), sh * 0.5f, render::Horizontal);
    if (on < 0.99f)
        dl->AddRect(ta, tb, Color(Border, 1.f - on), sh * 0.5f);
    const float r = sh * 0.5f - 3.f;
    const ImVec2 knob(ta.x + sh * 0.5f + on * (sw - sh), ta.y + sh * 0.5f);
    dl->AddCircleFilled(ImVec2(knob.x, knob.y + 1.f), r + 0.5f, IM_COL32(0, 0, 0, 60), 24);
    dl->AddCircleFilled(knob, r, Mix(Color(TextDim), IM_COL32(255, 255, 255, 255), on), 24);

    if (hasGear)
        Popover(Visible(label).c_str(), bind, options, ImVec2(pos.x + w, pos.y + kRow + 4.f));
    ImGui::PopID();
    return changed;
}

bool ui::Switch(const char* label, bool* value)
{
    return Feature(label, value, nullptr, nullptr);
}

bool ui::Slider(const char* label, float* value, float min, float max, const char* format)
{
    ImGui::PushID(label);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float w = ImGui::GetContentRegionAvail().x;
    char text[48];
    snprintf(text, sizeof(text), format, *value);
    const ImVec2 head = ImGui::GetCursorScreenPos();
    const float th = ImGui::GetTextLineHeight();
    Print(dl, head, Color(TextDim), Visible(label).c_str());
    const ImVec2 vs = ImGui::CalcTextSize(text);
    Print(dl, ImVec2(head.x + w - vs.x, head.y), Color(Text), text);
    ImGui::Dummy(ImVec2(w, th));

    const ImVec2 tp = ImGui::GetCursorScreenPos();
    constexpr float h = 16.f;
    ImGui::InvisibleButton("##track", ImVec2(w, h));
    const bool active = ImGui::IsItemActive();
    const bool hovered = ImGui::IsItemHovered();
    bool changed = false;
    if (active && max > min)
    {
        float f = (ImGui::GetIO().MousePos.x - tp.x) / w;
        f = f < 0.f ? 0.f : f > 1.f ? 1.f : f;
        const float v = min + f * (max - min);
        if (v != *value)
        {
            *value = v;
            changed = true;
        }
    }
    float f = max > min ? (*value - min) / (max - min) : 0.f;
    f = f < 0.f ? 0.f : f > 1.f ? 1.f : f;
    const float cy = tp.y + h * 0.5f;
    const float kx = tp.x + 6.f + f * (w - 12.f);
    dl->AddRectFilled(ImVec2(tp.x, cy - 2.f), ImVec2(tp.x + w, cy + 2.f), Color(Control), 2.f);
    render::Gradient(dl, ImVec2(tp.x, cy - 2.f), ImVec2(kx, cy + 2.f), Accent(0.55f), Accent(), 2.f, render::Horizontal);
    const float grow = Animate("##grow", active ? 1.f : hovered ? 0.5f : 0.f, 20.f);
    if (grow > 0.01f)
        RadialGlow(dl, ImVec2(kx, cy), 10.f + 7.f * grow, Accent(), 0.35f * grow);
    dl->AddCircleFilled(ImVec2(kx, cy), 6.f, IM_COL32(255, 255, 255, 255), 32);
    dl->AddCircle(ImVec2(kx, cy), 6.f, Accent(), 32, 2.f);
    ImGui::Dummy(ImVec2(0.f, 2.f));
    ImGui::PopID();
    return changed;
}

bool ui::SliderInt(const char* label, int* value, int min, int max, const char* format)
{
    float f = static_cast<float>(*value);
    Slider(label, &f, static_cast<float>(min), static_cast<float>(max), format);
    const int v = static_cast<int>(std::lround(f));
    if (v == *value)
        return false;
    *value = v;
    return true;
}

bool ui::Combo(const char* label, int* value, const char* items)
{
    ImGui::PushID(label);
    const float w = ImGui::GetContentRegionAvail().x;
    const float cw = w * 0.52f < 130.f ? (w < 130.f ? w : 130.f) : w * 0.52f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float fh = ImGui::GetFrameHeight();
    Print(dl, ImVec2(pos.x, pos.y + (fh - ImGui::GetTextLineHeight()) * 0.5f), Color(TextDim), Visible(label).c_str());
    const char* preview = "";
    int count = 0;
    for (const char* p = items; *p; p += strlen(p) + 1, ++count)
        if (count == *value)
            preview = p;
    ImGui::SetCursorScreenPos(ImVec2(pos.x + w - cw, pos.y));
    ImGui::SetNextItemWidth(cw);
    bool changed = false;
    const bool open = ImGui::BeginCombo("##combo", preview, ImGuiComboFlags_NoArrowButton);
    svg::Stroke(dl, icon::Chevron, ImVec2(pos.x + w - 22.f, pos.y + (fh - 14.f) * 0.5f), 14.f, Color(TextDim), 2.f);
    if (open)
    {
        int i = 0;
        for (const char* p = items; *p; p += strlen(p) + 1, ++i)
        {
            const bool selected = i == *value;
            if (ImGui::Selectable(p, selected, 0, ImVec2(0.f, 24.f)))
            {
                *value = i;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::PopID();
    return changed;
}

bool ui::ColorEdit(const char* label, float* rgba)
{
    ImGui::PushID(label);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    const float th = ImGui::GetTextLineHeight();
    Print(dl, ImVec2(pos.x, pos.y + (26.f - th) * 0.5f), Color(TextDim), Visible(label).c_str());
    const ImVec2 a(pos.x + w - 30.f, pos.y + 5.f), b(pos.x + w, pos.y + 21.f);
    ImGui::InvisibleButton("##row", ImVec2(w, 26.f));
    const bool hovered = ImGui::IsItemHovered();
    if (ImGui::IsItemClicked())
        ImGui::OpenPopup("##picker");
    dl->AddRectFilled(a, b, ImGui::ColorConvertFloat4ToU32(ImVec4(rgba[0], rgba[1], rgba[2], 1.f)), 5.f);
    dl->AddRect(a, b, hovered ? Color(TextDim) : Color(Border), 5.f);
    bool changed = false;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.f, 10.f));
    if (ImGui::BeginPopup("##picker"))
    {
        ImGui::SetNextItemWidth(200.f);
        changed = ImGui::ColorPicker4("##pick", rgba, ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoSmallPreview | ImGuiColorEditFlags_DisplayHex);
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();
    ImGui::PopID();
    return changed;
}

bool ui::Segmented(const char* id, int* value, const char* const* labels, int count, float width)
{
    ImGui::PushID(id);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float w = width > 0.f ? width : ImGui::GetContentRegionAvail().x;
    constexpr float h = 30.f;
    const float seg = w / count;
    dl->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + h), Color(Control), 9.f);
    const float at = Animate("##seg", static_cast<float>(*value), 16.f);
    const ImVec2 pa(pos.x + 3.f + at * seg, pos.y + 3.f), pb(pa.x + seg - 6.f, pos.y + h - 3.f);
    render::Gradient(dl, pa, pb, Mix(Color(ControlHover), IM_COL32(255, 255, 255, 255), 0.06f), Color(ControlHover), 7.f);
    dl->AddRect(pa, pb, Accent(0.45f), 7.f);
    bool changed = false;
    for (int i = 0; i < count; ++i)
    {
        ImGui::SetCursorScreenPos(ImVec2(pos.x + i * seg, pos.y));
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("##s", ImVec2(seg, h)) && *value != i)
        {
            *value = i;
            changed = true;
        }
        const bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();
        const ImVec2 ts = ImGui::CalcTextSize(labels[i]);
        const ImU32 col = i == *value ? Color(Text) : hovered ? Mix(Color(TextDim), Color(Text), 0.5f) : Color(TextDim);
        Print(dl, ImVec2(pos.x + i * seg + (seg - ts.x) * 0.5f, pos.y + (h - ts.y) * 0.5f), col, labels[i]);
    }
    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + h));
    ImGui::Dummy(ImVec2(w, 0.f));
    ImGui::PopID();
    return changed;
}

bool ui::Button(const char* label, ImVec2 size, const char* iconPath, int kind)
{
    const bool danger = kind == Destructive, primary = kind == Primary;
    ImGui::PushID(label);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    if (size.x <= 0.f)
        size.x = ImGui::GetContentRegionAvail().x;
    if (size.y <= 0.f)
        size.y = 32.f;
    const bool pressed = ImGui::InvisibleButton("##b", size);
    const bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
    const float hv = Animate("##hv", hovered ? 1.f : 0.f, 20.f);
    ImU32 bg = danger ? Mix(Color(Danger, 0.14f), Color(Danger, 0.28f), hv) : primary ? Mix(Accent(0.85f), Accent(1.f), hv) : Mix(Color(Control), Color(ControlHover), hv);
    if (active)
        bg = danger ? Color(Danger, 0.4f) : primary ? Mix(Accent(), IM_COL32(255, 255, 255, 255), 0.15f) : Accent(0.3f);
    const ImVec2 end = Add(pos, size);
    if (primary)
        render::Gradient(dl, pos, end, Mix(bg, IM_COL32(255, 255, 255, 255), 0.18f), bg, kRadius);
    else
        render::Gradient(dl, pos, end, Mix(bg, IM_COL32(255, 255, 255, 255), 0.04f), bg, kRadius);
    if (!primary)
        dl->AddRect(pos, end, danger ? Color(Danger, 0.35f) : Color(Border), kRadius);
    const std::string text = Visible(label);
    const ImVec2 ts = ImGui::CalcTextSize(text.c_str());
    const float iw = iconPath ? 22.f : 0.f;
    float x = pos.x + (size.x - ts.x - iw) * 0.5f;
    const ImU32 fg = danger ? Color(Danger) : primary ? IM_COL32(10, 14, 20, 255) : Color(Text);
    if (iconPath)
    {
        svg::Stroke(dl, iconPath, ImVec2(x, pos.y + (size.y - 16.f) * 0.5f), 16.f, fg, 2.f);
        x += iw;
    }
    Print(dl, ImVec2(x, pos.y + (size.y - ts.y) * 0.5f), fg, text.c_str());
    ImGui::PopID();
    return pressed;
}

void ui::KeybindEditor(misc::Bind* bind)
{
    ImGui::PushID(bind);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float w = ImGui::GetContentRegionAvail().x;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float th = ImGui::GetTextLineHeight();
    Print(dl, ImVec2(pos.x, pos.y + (30.f - th) * 0.5f), Color(TextDim), "Key");
    ImGui::SetCursorScreenPos(ImVec2(pos.x + w - 140.f, pos.y));
    const bool capturing = misc::capturing == bind;
    char text[48];
    snprintf(text, sizeof(text), "%s##key", capturing ? "Press a key..." : bind->key ? misc::KeyName(bind->key) : "None");
    if (Button(text, ImVec2(140.f, 30.f), icon::Keyboard))
        misc::capturing = capturing ? nullptr : bind;
    static const char* const modes[] = { "Toggle", "Hold", "Always" };
    Segmented("##mode", &bind->mode, modes, 3, w);
    const ImVec2 cp = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##clear", ImVec2(60.f, th + 4.f));
    if (ImGui::IsItemClicked())
    {
        bind->key = 0;
        bind->toggled = false;
        if (misc::capturing == bind)
            misc::capturing = nullptr;
    }
    Print(dl, ImVec2(cp.x, cp.y + 2.f), ImGui::IsItemHovered() ? Color(Danger) : Color(TextFaint), "Clear");
    Print(dl, ImVec2(cp.x + 70.f, cp.y + 2.f), Color(TextFaint), "Esc while choosing = none");
    ImGui::PopID();
}

void ui::Value(const char* label, const char* fmt, ...)
{
    char text[128];
    va_list args;
    va_start(args, fmt);
    vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    Print(dl, pos, Color(TextDim), label);
    const ImVec2 ts = ImGui::CalcTextSize(text);
    Print(dl, ImVec2(pos.x + w - ts.x, pos.y), Color(Text), text);
    ImGui::Dummy(ImVec2(w, ImGui::GetTextLineHeight() + 2.f));
}

void ui::Note(const char* text)
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(Color(TextFaint)));
    ImGui::PushTextWrapPos(0.f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

bool ui::ToastsActive()
{
    const uint64_t now = GetTickCount64();
    const auto lines = logs::Snapshot();
    return !lines.empty() && now - lines.back().time < 4000;
}

void ui::Toasts()
{
    constexpr uint64_t kLife = 4000;
    const uint64_t now = GetTickCount64();
    const auto lines = logs::Snapshot();
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const ImVec2 screen = ImGui::GetIO().DisplaySize;
    float y = screen.y - 24.f;
    int shown = 0;
    for (auto it = lines.rbegin(); it != lines.rend() && shown < 4; ++it)
    {
        const uint64_t age = now - it->time;
        if (age >= kLife)
            break;
        const float in = Ease(age / 220.f);
        const float out = Ease((kLife - age) / 300.f);
        const float a = in < out ? in : out;
        constexpr float w = 310.f, h = 46.f;
        const float x = screen.x - 24.f - w + (1.f - in) * 40.f;
        y -= h;
        const ImVec2 p0(x, y), p1(x + w, y + h);
        SoftShadow(dl, ImVec2(p0.x + 2.f, p0.y + 4.f), ImVec2(p1.x - 2.f, p1.y + 2.f), 10.f, 14.f, IM_COL32(0, 0, 0, 255), 0.45f * a);
        dl->AddRectFilled(p0, p1, Color(Surface, 0.97f * a), 10.f);
        dl->AddRect(p0, p1, Color(Border, a), 10.f);
        const Tone tone = it->level == logs::Error ? Danger : it->level == logs::Warning ? Warn : it->level == logs::Success ? Good : Text;
        const char* glyph = it->level == logs::Error ? icon::Cross : it->level == logs::Success ? icon::Check : icon::Alert;
        const ImU32 col = it->level == logs::Info ? Accent(a) : Color(tone, a);
        dl->AddRectFilled(ImVec2(p0.x + 12.f, p0.y + 11.f), ImVec2(p0.x + 36.f, p0.y + 35.f), (col & 0x00FFFFFF) | (static_cast<ImU32>(40 * a) << 24), 7.f);
        svg::Stroke(dl, glyph, ImVec2(p0.x + 16.f, p0.y + 15.f), 16.f, col, 2.2f);
        dl->PushClipRect(ImVec2(p0.x + 44.f, p0.y), ImVec2(p1.x - 10.f, p1.y), true);
        Print(dl, ImVec2(p0.x + 46.f, p0.y + (h - ImGui::GetTextLineHeight()) * 0.5f), Color(Text, a), it->text);
        dl->PopClipRect();
        y -= 8.f;
        ++shown;
    }
}
