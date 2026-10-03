#include "menu.h"
#include "hooks.h"
#include "items.h"
#include "skins.h"
#include "movement.h"
#include "misc.h"
#include "aim.h"
#include "ragebot.h"
#include "visuals.h"
#include "glow.h"
#include "hands.h"
#include "features/hitbox.h"
#include "features/nospread.h"
#include "features/spread.h"
#include "features/jumpcheck.h"
#include "ui/render.h"
#include "ui/ui.h"
#include "ui/svg.h"
#include "ui/icons_svg.h"
#include "core/log.h"
#include "core/settings.h"
#include "core/events.h"
#include "core/patterns.h"
#include <Windows.h>
#include <cstdio>
#include "icons.h"
#include "game.h"
#include "imgui.h"
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>
#include <cmath>

namespace
{
    struct Result
    {
        const items::Item* item;
        int paint;
        int rarity;
    };

    constexpr float kCardMin = 158.f;
    constexpr float kGap = 10.f;

    int category = 0;
    int selected = 0;
    char search[96]{};
    std::vector<Result> results;
    std::string resultsKey;

    int invFilter = 0;
    char invSearch[96]{};

    skins::Entry draft;
    bool openDraft = false;
    int stickerSlot = 0;
    char stickerSearch[96]{};
    std::vector<int> stickerFilter;
    std::string stickerKey;

    uint32_t inventoryUid = 0;
    bool openInventory = false;

    const char* T(const char* ru, const char* en)
    {
        return items::language ? en : ru;
    }

    std::string Lower(const char* s)
    {
        std::string r;
        for (auto p = reinterpret_cast<const unsigned char*>(s); *p; ++p)
        {
            unsigned char c = *p;
            if (c >= 'A' && c <= 'Z')
                r += static_cast<char>(c + 32);
            else if (c == 0xD0 && p[1] >= 0x90 && p[1] <= 0x9F)
            {
                r += '\xD0';
                r += static_cast<char>(*++p + 0x20);
            }
            else if (c == 0xD0 && p[1] >= 0xA0 && p[1] <= 0xAF)
            {
                r += '\xD1';
                r += static_cast<char>(*++p - 0x20);
            }
            else if (c == 0xD0 && p[1] == 0x81)
            {
                r += "\xD1\x91";
                ++p;
            }
            else
                r += static_cast<char>(c);
        }
        return r;
    }

    ImU32 WithAlpha(ImU32 c, float a)
    {
        return (c & 0x00FFFFFF) | (static_cast<ImU32>(std::clamp(a, 0.f, 1.f) * 255.f) << 24);
    }

    void Glow(ImDrawList* dl, ImVec2 center, float radius, ImU32 color, float alpha)
    {
        ui::RadialGlow(dl, center, radius, color, alpha);
    }

    float CardHeight(float w)
    {
        return w * 0.66f + 52.f;
    }

    void DrawIcon(ImDrawList* dl, const std::string& icon, ImVec2 a, ImVec2 b)
    {
        if (uint64_t tex = icons::Get(icon))
        {
            const float w = b.x - a.x, h = b.y - a.y;
            float iw = w, ih = w * 0.75f;
            if (ih > h)
            {
                ih = h;
                iw = h / 0.75f;
            }
            const ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
            dl->AddImage(ImTextureRef(static_cast<ImTextureID>(tex)), ImVec2(c.x - iw * 0.5f, c.y - ih * 0.5f), ImVec2(c.x + iw * 0.5f, c.y + ih * 0.5f));
        }
        else
        {
            const float s = 26.f;
            svg::Stroke(dl, icon::Box, ImVec2((a.x + b.x - s) * 0.5f, (a.y + b.y - s) * 0.5f), s, ui::Color(ui::TextFaint), 1.6f);
        }
    }

    void Badge(ImDrawList* dl, float& x, float y, const char* text, ImU32 color)
    {
        const float fs = 11.f;
        const ImVec2 ts = ui::fonts.regular->CalcTextSizeA(fs, 100.f, 0.f, text);
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + ts.x + 10.f, y + 17.f), WithAlpha(color, 0.22f), 5.f);
        ui::Print(dl, ui::fonts.regular, fs, ImVec2(x + 5.f, y + (17.f - ts.y) * 0.5f), color, text);
        x += ts.x + 14.f;
    }

    bool ItemCard(int id, float w, const std::string& icon, const std::string& title, const char* subtitle, int rarity, uint8_t teams, bool stattrak, const std::string& tooltip)
    {
        const float h = CardHeight(w);
        ImGui::PushID(id);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const bool clicked = ImGui::InvisibleButton("##card", ImVec2(w, h));
        const bool hovered = ImGui::IsItemHovered();
        const float hv = ui::Animate("##hv", hovered ? 1.f : 0.f, 18.f);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImU32 rc = items::RarityColor(rarity);
        const ImVec2 a(p.x, p.y - hv * 2.f), b(p.x + w, p.y + h - hv * 2.f);
        if (hv > 0.01f)
            ui::SoftShadow(dl, ImVec2(a.x + 4.f, a.y + 8.f), ImVec2(b.x - 4.f, b.y + 3.f), 12.f, 16.f, IM_COL32(0, 0, 0, 255), 0.5f * hv);
        dl->AddRectFilled(a, b, ui::Mix(ui::Color(ui::Surface), ui::Color(ui::ControlHover), hv * 0.6f), 12.f);
        const float imgH = w * 0.66f;
        dl->AddRectFilled(ImVec2(a.x + 1.f, a.y + 1.f), ImVec2(b.x - 1.f, a.y + imgH), WithAlpha(rc, 0.07f + 0.05f * hv), 11.f, ImDrawFlags_RoundCornersTop);
        dl->PushClipRect(ImVec2(a.x + 1.f, a.y + 1.f), ImVec2(b.x - 1.f, a.y + imgH), true);
        Glow(dl, ImVec2((a.x + b.x) * 0.5f, a.y + imgH * 0.55f), w * 0.42f, rc, 0.32f + 0.18f * hv);
        dl->PopClipRect();
        DrawIcon(dl, icon, ImVec2(a.x + 12.f, a.y + 8.f), ImVec2(b.x - 12.f, a.y + imgH - 4.f));
        dl->AddRect(a, b, hovered ? ui::Accent(0.55f) : ui::Color(ui::Border, 0.9f), 12.f);
        dl->PushClipRect(ImVec2(a.x + 10.f, a.y), ImVec2(b.x - 10.f, b.y), true);
        ui::Print(dl, ui::fonts.regular, 14.f, ImVec2(a.x + 11.f, a.y + imgH + 6.f), ui::Color(ui::Text), title.c_str());
        if (subtitle && *subtitle)
            ui::Print(dl, ui::fonts.regular, 12.f, ImVec2(a.x + 11.f, a.y + imgH + 25.f), ui::Color(ui::TextDim), subtitle);
        dl->PopClipRect();
        dl->AddRectFilled(ImVec2(a.x + 11.f, b.y - 6.f), ImVec2(a.x + 11.f + (w - 22.f) * (0.35f + 0.65f * hv), b.y - 4.f), rc, 1.f);
        float bx = a.x + 9.f;
        if (teams & 1)
            Badge(dl, bx, a.y + 9.f, "T", IM_COL32(234, 182, 92, 255));
        if (teams & 2)
            Badge(dl, bx, a.y + 9.f, "CT", IM_COL32(110, 160, 255, 255));
        if (stattrak)
            Badge(dl, bx, a.y + 9.f, "ST", IM_COL32(255, 140, 70, 255));
        if (hovered && !tooltip.empty())
            ImGui::SetTooltip("%s", tooltip.c_str());
        ImGui::PopID();
        return clicked;
    }

    template <typename F>
    void Grid(int count, F&& draw)
    {
        const float avail = ImGui::GetContentRegionAvail().x - 4.f;
        const int cols = std::max(1, static_cast<int>((avail + kGap) / (kCardMin + kGap)));
        const float w = std::floor((avail - kGap * (cols - 1)) / cols);
        const int rows = (count + cols - 1) / cols;
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(kGap, kGap));
        ImGuiListClipper clipper;
        clipper.Begin(rows, CardHeight(w) + kGap);
        while (clipper.Step())
            for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r)
            {
                for (int c = 0; c < cols; ++c)
                {
                    const int i = r * cols + c;
                    if (i >= count)
                        break;
                    if (c)
                        ImGui::SameLine(0.f, kGap);
                    draw(i, w);
                }
            }
        ImGui::PopStyleVar();
    }

    bool Chip(const char* label, bool active)
    {
        ImGui::PushID(label);
        const ImVec2 ts = ImGui::CalcTextSize(label);
        const ImVec2 size(ts.x + 26.f, 30.f);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const bool pressed = ImGui::InvisibleButton("##chip", size);
        const bool hovered = ImGui::IsItemHovered();
        const float on = ui::Animate("##on", active ? 1.f : 0.f, 16.f);
        const float hv = ui::Animate("##hv", hovered ? 1.f : 0.f, 20.f);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 b(p.x + size.x, p.y + size.y);
        dl->AddRectFilled(p, b, ui::Mix(ui::Mix(ui::Color(ui::Control), ui::Color(ui::ControlHover), hv), ui::Accent(0.18f), on), 15.f);
        dl->AddRect(p, b, ui::Mix(ui::Color(ui::Border), ui::Accent(0.7f), on), 15.f);
        ui::Print(dl, ImVec2(p.x + 13.f, p.y + (size.y - ts.y) * 0.5f), ui::Mix(ui::Color(ui::TextDim), ui::Color(ui::Text), on > hv ? on : hv), label);
        ImGui::PopID();
        return pressed;
    }

    void SearchBox(const char* id, char* buf, size_t size, const char* hint, float width)
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(34.f, 7.f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 9.f);
        ImGui::SetNextItemWidth(width);
        ImGui::InputTextWithHint(id, hint, buf, size);
        ImGui::PopStyleVar(2);
        constexpr const char* kSearch = "M17 10.5a6.5 6.5 0 1 1-13 0a6.5 6.5 0 1 1 13 0ZM15.2 15.2L20 20";
        svg::Stroke(ImGui::GetWindowDrawList(), kSearch, ImVec2(p.x + 10.f, p.y + (ImGui::GetItemRectSize().y - 16.f) * 0.5f), 16.f, ui::Color(ui::TextFaint), 2.f);
    }

    void Refresh()
    {
        std::string key = std::to_string(category) + "|" + std::to_string(selected) + "|" + std::to_string(items::language) + "|" + search;
        if (key == resultsKey)
            return;
        resultsKey = std::move(key);
        results.clear();
        const std::string needle = Lower(search);
        for (auto& it : items::List())
        {
            if (needle.empty())
            {
                if (it.category != category || (selected && it.def != selected))
                    continue;
            }
            if (it.category == items::Agent)
            {
                if (needle.empty() || Lower(items::Name(it.name)).find(needle) != std::string::npos)
                    results.push_back({ &it, 0, it.rarity });
                continue;
            }
            for (auto& s : it.skins)
                if (needle.empty() || Lower(items::FullName(it, s.paint).c_str()).find(needle) != std::string::npos)
                    results.push_back({ &it, s.paint, items::Rarity(it, s.paint) });
        }
    }

    void OpenDraft(const items::Item& it, int paint)
    {
        draft = {};
        draft.def = it.def;
        draft.paint = paint;
        if (auto p = items::FindPaint(paint))
            draft.wear = std::max(p->wearMin, 0.0001f);
        draft.equip = static_cast<uint8_t>(it.teams);
        openDraft = true;
    }

    void StickerPicker()
    {
        ImGui::SetNextWindowSize(ImVec2(440.f, 420.f), ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.f, 12.f));
        if (!ImGui::BeginPopup("##stickers"))
        {
            ImGui::PopStyleVar();
            return;
        }
        SearchBox("##ss", stickerSearch, sizeof(stickerSearch), T("Поиск стикера", "Search sticker"), ImGui::GetContentRegionAvail().x);
        const auto& all = items::Stickers();
        std::string key = std::to_string(items::language) + "|" + stickerSearch;
        if (key != stickerKey)
        {
            stickerKey = std::move(key);
            stickerFilter.clear();
            const std::string needle = Lower(stickerSearch);
            for (int i = 0; i < static_cast<int>(all.size()); ++i)
                if (needle.empty() || Lower(items::Name(all[i].name)).find(needle) != std::string::npos)
                    stickerFilter.push_back(i);
        }
        ImGui::BeginChild("##list");
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(stickerFilter.size()), 46.f);
        while (clipper.Step())
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
            {
                const auto& s = all[stickerFilter[i]];
                ImGui::PushID(s.id);
                const ImVec2 p = ImGui::GetCursorScreenPos();
                const float w = ImGui::GetContentRegionAvail().x;
                if (ImGui::InvisibleButton("##st", ImVec2(w, 42.f)))
                {
                    draft.stickers[stickerSlot] = s.id;
                    ImGui::CloseCurrentPopup();
                }
                const bool hovered = ImGui::IsItemHovered();
                if (hovered || draft.stickers[stickerSlot] == s.id)
                    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + 42.f), hovered ? ui::Color(ui::ControlHover) : ui::Accent(0.15f), 8.f);
                DrawIcon(dl, items::StickerIcon(s), ImVec2(p.x + 4.f, p.y + 3.f), ImVec2(p.x + 52.f, p.y + 39.f));
                ui::Print(dl, ImVec2(p.x + 62.f, p.y + (42.f - ImGui::GetTextLineHeight()) * 0.5f), items::RarityColor(s.rarity), items::Name(s.name));
                ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + 46.f));
                ImGui::PopID();
            }
        ImGui::EndChild();
        ImGui::EndPopup();
        ImGui::PopStyleVar();
    }

    void DraftPopup()
    {
        if (openDraft)
        {
            ImGui::OpenPopup("##draft");
            openDraft = false;
        }
        ImGui::SetNextWindowSize(ImVec2(660.f, 0.f), ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.f, 18.f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 14.f);
        const bool open = ImGui::BeginPopupModal("##draft", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::PopStyleVar(2);
        if (!open)
            return;
        const items::Item* it = items::Find(draft.def);
        if (!it)
        {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }
        const items::Paint* paint = items::FindPaint(draft.paint);
        const int rarity = items::Rarity(*it, draft.paint);
        const ImU32 rc = items::RarityColor(rarity);
        const bool weapon = it->category <= items::Heavy;
        const bool knife = it->category == items::Knife;
        ImDrawList* dl = ImGui::GetWindowDrawList();

        const ImVec2 pp = ImGui::GetCursorScreenPos();
        const ImVec2 pb(pp.x + 250.f, pp.y + 210.f);
        dl->AddRectFilled(pp, pb, ui::Color(ui::Surface), 12.f);
        dl->PushClipRect(ImVec2(pp.x + 1.f, pp.y + 1.f), ImVec2(pb.x - 1.f, pb.y - 1.f), true);
        Glow(dl, ImVec2((pp.x + pb.x) * 0.5f, (pp.y + pb.y) * 0.5f), 115.f, rc, 0.45f);
        dl->PopClipRect();
        dl->AddRect(pp, pb, ui::Color(ui::Border), 12.f);
        DrawIcon(dl, items::Icon(*it, draft.paint, draft.wear), ImVec2(pp.x + 14.f, pp.y + 14.f), ImVec2(pb.x - 14.f, pb.y - 14.f));
        ImGui::Dummy(ImVec2(250.f, 210.f));
        ImGui::SameLine(0.f, 18.f);

        ImGui::BeginGroup();
        const float rw = 356.f;
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + rw);
        ImGui::PushFont(nullptr, 18.f);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(rc));
        ImGui::TextUnformatted(items::FullName(*it, draft.paint).c_str());
        ImGui::PopStyleColor();
        ImGui::PopFont();
        ImGui::PopTextWrapPos();
        ImGui::TextDisabled("%s%s%s", items::CategoryName(it->category), paint ? "  \xC2\xB7  " : "", paint ? items::WearName(draft.wear) : "");
        ImGui::Dummy(ImVec2(rw, 6.f));
        ImGui::PushItemWidth(rw);
        ImGui::BeginChild("##props", ImVec2(rw, 0.f), ImGuiChildFlags_AutoResizeY);
        if (paint)
        {
            ui::Slider(T("Износ", "Wear"), &draft.wear, paint->wearMin, paint->wearMax, "%.5f");
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::Color(ui::TextDim)), "%s", T("Паттерн", "Pattern"));
            ImGui::SetNextItemWidth(-1.f);
            if (ImGui::InputInt("##seed", &draft.seed))
                draft.seed = std::clamp(draft.seed, 0, 1000);
        }
        if (weapon || knife)
        {
            bool st = draft.stattrak >= 0;
            if (ui::Switch("StatTrak\xE2\x84\xA2", &st))
                draft.stattrak = st ? 0 : -1;
            if (st)
            {
                ImGui::SetNextItemWidth(-1.f);
                if (ImGui::InputInt("##kills", &draft.stattrak))
                    draft.stattrak = std::max(draft.stattrak, 0);
            }
            ImGui::SetNextItemWidth(-1.f);
            ImGui::InputTextWithHint("##tag", T("Неймтег", "Name tag"), draft.tag, sizeof(draft.tag));
        }
        ImGui::EndChild();
        ImGui::PopItemWidth();
        ImGui::EndGroup();

        if (weapon)
        {
            ImGui::Dummy(ImVec2(0.f, 6.f));
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::Color(ui::TextDim)), "%s", T("Стикеры  (ПКМ — убрать)", "Stickers  (right-click to clear)"));
            const float sw = (624.f - 4.f * 10.f) / 5.f;
            for (int i = 0; i < 5; ++i)
            {
                ImGui::PushID(i);
                if (i)
                    ImGui::SameLine(0.f, 10.f);
                const items::Sticker* s = items::FindSticker(draft.stickers[i]);
                const ImVec2 p = ImGui::GetCursorScreenPos();
                const bool pressed = ImGui::InvisibleButton("##slot", ImVec2(sw, 72.f));
                const bool hovered = ImGui::IsItemHovered();
                dl->AddRectFilled(p, ImVec2(p.x + sw, p.y + 72.f), hovered ? ui::Color(ui::ControlHover) : ui::Color(ui::Control), 10.f);
                dl->AddRect(p, ImVec2(p.x + sw, p.y + 72.f), hovered ? ui::Accent(0.5f) : ui::Color(ui::Border), 10.f);
                if (s)
                    DrawIcon(dl, items::StickerIcon(*s), ImVec2(p.x + 8.f, p.y + 6.f), ImVec2(p.x + sw - 8.f, p.y + 66.f));
                else
                    svg::Stroke(dl, icon::Plus, ImVec2(p.x + sw * 0.5f - 10.f, p.y + 26.f), 20.f, ui::Color(ui::TextFaint), 2.f);
                if (s && hovered)
                    ImGui::SetTooltip("%s", items::Name(s->name));
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                    draft.stickers[i] = 0;
                if (pressed)
                {
                    stickerSlot = i;
                    ImGui::OpenPopup("##stickers");
                }
                ImGui::PopID();
            }
            StickerPicker();
        }

        ImGui::Dummy(ImVec2(0.f, 6.f));
        if (ui::BeginColumns("##teams"))
        {
            ui::NextColumn();
            bool t = draft.equip & 1, ct = draft.equip & 2;
            if (it->teams & 1 && ui::Switch(T("Экипировать за T", "Equip for T"), &t))
                draft.equip = static_cast<uint8_t>((draft.equip & ~1) | (t ? 1 : 0));
            ui::NextColumn();
            if (it->teams & 2 && ui::Switch(T("Экипировать за CT", "Equip for CT"), &ct))
                draft.equip = static_cast<uint8_t>((draft.equip & ~2) | (ct ? 2 : 0));
            ui::EndColumns();
        }

        ImGui::Dummy(ImVec2(0.f, 6.f));
        const float bw = 170.f;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 624.f - bw * 2.f - 10.f);
        if (ui::Button(T("Отмена", "Cancel"), ImVec2(bw, 36.f)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
            ImGui::CloseCurrentPopup();
        ImGui::SameLine(0.f, 10.f);
        if (ui::Button(T("Добавить", "Add to inventory"), ImVec2(bw, 36.f), icon::Plus, ui::Primary))
        {
            skins::Add(draft);
            logs::Add(logs::Success, "%s added", items::FullName(*it, draft.paint).c_str());
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    void AddTab()
    {
        if (items::state == 0)
        {
            ui::Note(T("Загрузка предметов из CS2...", "Loading items from CS2..."));
            return;
        }
        if (items::state < 0)
        {
            ImGui::TextColored(ImVec4(1.f, 0.4f, 0.4f, 1.f), "%s: %s", T("Ошибка загрузки", "Load error"), items::error.c_str());
            return;
        }
        for (int c = 0; c < items::CategoryCount; ++c)
        {
            if (c)
                ImGui::SameLine(0.f, 6.f);
            if (Chip(items::CategoryName(c), category == c && !search[0]))
            {
                category = c;
                selected = 0;
                search[0] = 0;
            }
        }
        ImGui::Dummy(ImVec2(0.f, 2.f));
        const float avail = ImGui::GetContentRegionAvail().x;
        if (category != items::Agent)
        {
            const items::Item* cur = selected ? items::Find(selected) : nullptr;
            ImGui::SetNextItemWidth(200.f);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 9.f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.f, 7.f));
            const ImVec2 cp = ImGui::GetCursorScreenPos();
            ImDrawList* cdl = ImGui::GetWindowDrawList();
            const bool comboOpen = ImGui::BeginCombo("##weapon", cur ? items::Name(cur->name) : T("Всё оружие", "All weapons"), ImGuiComboFlags_NoArrowButton);
            svg::Stroke(cdl, icon::Chevron, ImVec2(cp.x + 200.f - 26.f, cp.y + (ImGui::GetFrameHeight() - 14.f) * 0.5f), 14.f, ui::Color(ui::TextDim), 2.f);
            if (comboOpen)
            {
                if (ImGui::Selectable(T("Всё оружие", "All weapons"), !selected, 0, ImVec2(0.f, 24.f)))
                    selected = 0;
                for (auto& it : items::List())
                {
                    if (it.category != category)
                        continue;
                    ImGui::PushID(it.def);
                    if (ImGui::Selectable(items::Name(it.name), selected == it.def, 0, ImVec2(0.f, 24.f)))
                    {
                        selected = it.def;
                        search[0] = 0;
                    }
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            ImGui::PopStyleVar(2);
            ImGui::SameLine(0.f, 8.f);
        }
        SearchBox("##search", search, sizeof(search), T("Поиск по всем предметам", "Search all items"), ImGui::GetContentRegionAvail().x);
        (void)avail;
        Refresh();
        ImGui::Dummy(ImVec2(0.f, 2.f));
        ImGui::BeginChild("##grid");
        if (results.empty())
            ui::Note(T("Ничего не найдено", "Nothing found"));
        Grid(static_cast<int>(results.size()), [&](int i, float w) {
            const Result& r = results[i];
            const items::Paint* p = r.paint ? items::FindPaint(r.paint) : nullptr;
            const std::string title = p ? items::Name(p->name) : items::Name(r.item->name);
            const char* sub = r.item->category == items::Agent ? (r.item->teams == 1 ? "T" : r.item->teams == 2 ? "CT" : "") : items::Name(r.item->name);
            if (ItemCard(i, w, items::Icon(*r.item, r.paint, 0.f), title, sub, r.rarity, 0, false, items::FullName(*r.item, r.paint)))
                OpenDraft(*r.item, r.paint);
        });
        ImGui::EndChild();
        DraftPopup();
    }

    void InventoryPopup(const std::vector<skins::Entry>& list)
    {
        if (openInventory)
        {
            ImGui::OpenPopup("##item");
            openInventory = false;
        }
        ImGui::SetNextWindowSize(ImVec2(280.f, 0.f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.f, 12.f));
        const bool open = ImGui::BeginPopup("##item");
        ImGui::PopStyleVar();
        if (!open)
            return;
        auto e = std::find_if(list.begin(), list.end(), [](const skins::Entry& x) { return x.uid == inventoryUid; });
        const items::Item* it = e != list.end() ? items::Find(e->def) : nullptr;
        if (!it)
        {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }
        ImGui::PushTextWrapPos(0.f);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(items::RarityColor(items::Rarity(*it, e->paint))));
        ImGui::TextUnformatted(items::FullName(*it, e->paint).c_str());
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();
        if (e->paint)
            ui::Value(items::WearName(e->wear), "%.5f  #%d", e->wear, e->seed);
        if (e->stattrak >= 0)
            ui::Value("StatTrak\xE2\x84\xA2", "%d", e->stattrak);
        if (e->tag[0])
            ui::Value(T("Неймтег", "Name tag"), "%s", e->tag);
        bool t = e->equip & 1, ct = e->equip & 2;
        uint8_t mask = e->equip;
        if (it->teams & 1 && ui::Switch(T("Экипировано за T", "Equipped for T"), &t))
            mask = static_cast<uint8_t>((mask & ~1) | (t ? 1 : 0));
        if (it->teams & 2 && ui::Switch(T("Экипировано за CT", "Equipped for CT"), &ct))
            mask = static_cast<uint8_t>((mask & ~2) | (ct ? 2 : 0));
        if (mask != e->equip)
            skins::SetEquip(e->uid, mask);
        if (e->blocked)
            ui::Note(T("Слот лоадаута занят другим оружием — лоадаут не тронут, скин виден, когда держишь это оружие.", "Loadout slot holds another weapon; loadout kept, skin shows when you hold this weapon."));
        ImGui::Dummy(ImVec2(0.f, 2.f));
        if (ui::Button(T("Удалить", "Delete"), ImVec2(0.f, 32.f), icon::Trash, ui::Destructive))
        {
            skins::Remove(e->uid);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    bool MatchFilter(const items::Item& it)
    {
        switch (invFilter)
        {
        case 1: return it.category <= items::Heavy;
        case 2: return it.category == items::Knife;
        case 3: return it.category == items::Glove;
        case 4: return it.category == items::Agent;
        default: return true;
        }
    }

    void InventoryTab()
    {
        const auto all = skins::Snapshot();
        const char* filters[] = { T("Все", "All"), T("Оружие", "Weapons"), T("Ножи", "Knives"), T("Перчатки", "Gloves"), T("Агенты", "Agents") };
        for (int i = 0; i < 5; ++i)
        {
            if (i)
                ImGui::SameLine(0.f, 6.f);
            if (Chip(filters[i], invFilter == i))
                invFilter = i;
        }
        ImGui::SameLine(0.f, 10.f);
        const float del = 120.f;
        SearchBox("##invsearch", invSearch, sizeof(invSearch), T("Поиск", "Search"), ImGui::GetContentRegionAvail().x - del - 8.f);
        ImGui::SameLine(0.f, 8.f);
        if (ui::Button(T("Удалить всё", "Clear all"), ImVec2(del, 32.f), icon::Trash, ui::Destructive))
            ImGui::OpenPopup("##clear");
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.f, 12.f));
        if (ImGui::BeginPopup("##clear"))
        {
            ImGui::TextUnformatted(T("Удалить все добавленные предметы?", "Delete all added items?"));
            if (ui::Button(T("Удалить", "Delete"), ImVec2(120.f, 30.f), nullptr, ui::Destructive))
            {
                skins::RemoveAll();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine(0.f, 8.f);
            if (ui::Button(T("Отмена", "Cancel"), ImVec2(120.f, 30.f)))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar();
        if (!skins::inventoryReady)
            ui::Note(T("Ожидание инвентаря CS2 — предметы появятся в игре после подключения к серверам Steam.", "Waiting for the CS2 inventory; items appear in game once Steam is connected."));
        if (items::state != 1)
            return;

        std::vector<const skins::Entry*> list;
        const std::string needle = Lower(invSearch);
        for (auto& e : all)
        {
            const items::Item* it = items::Find(e.def);
            if (!it || !MatchFilter(*it))
                continue;
            if (!needle.empty() && Lower(items::FullName(*it, e.paint).c_str()).find(needle) == std::string::npos)
                continue;
            list.push_back(&e);
        }
        ImGui::Dummy(ImVec2(0.f, 2.f));
        ImGui::BeginChild("##inv");
        if (list.empty())
        {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float w = ImGui::GetContentRegionAvail().x;
            ImDrawList* dl = ImGui::GetWindowDrawList();
            svg::Stroke(dl, icon::Box, ImVec2(p.x + w * 0.5f - 24.f, p.y + 60.f), 48.f, ui::Color(ui::TextFaint), 1.6f);
            const char* msg = all.empty() ? T("Инвентарь пуст — добавь предметы во вкладке Add", "Inventory is empty. Add items in the Add tab") : T("Ничего не найдено", "Nothing found");
            const ImVec2 ts = ImGui::CalcTextSize(msg);
            ui::Print(dl, ImVec2(p.x + (w - ts.x) * 0.5f, p.y + 122.f), ui::Color(ui::TextDim), msg);
        }
        Grid(static_cast<int>(list.size()), [&](int i, float w) {
            const auto& e = *list[i];
            const items::Item* it = items::Find(e.def);
            const items::Paint* p = e.paint ? items::FindPaint(e.paint) : nullptr;
            const std::string title = p ? items::Name(p->name) : items::Name(it->name);
            const std::string sub = p ? std::string(items::Name(it->name)) : std::string(items::CategoryName(it->category));
            if (ItemCard(static_cast<int>(e.uid), w, items::Icon(*it, e.paint, e.wear), title, sub.c_str(), items::Rarity(*it, e.paint), e.equip, e.stattrak >= 0, items::FullName(*it, e.paint)))
            {
                inventoryUid = e.uid;
                openInventory = true;
            }
        });
        ImGui::EndChild();
        InventoryPopup(all);
    }

    struct Page
    {
        const char* name;
        const char* subtitle;
        const char* icon;
        const char* const* tabs;
        int tabCount;
    };

    const char* const kAimTabs[] = { "Legit", "Rage" };
    const char* const kSkinTabs[] = { "Inventory", "Add", "Options" };
    const char* const kMiscTabs[] = { "Movement", "Camera", "Interface" };
    const char* const kSettingsTabs[] = { "General", "Theme", "Events", "Patterns", "Log" };
    const Page kPages[] = {
        { "Aimbot", "Aim assistance", icon::Target, kAimTabs, 2 },
        { "Visuals", "Player overlays", icon::Eye, nullptr, 0 },
        { "Skins", "Inventory and skin changer", icon::Box, kSkinTabs, 3 },
        { "Misc", "Movement, camera and overlay", icon::Sliders, kMiscTabs, 3 },
        { "Settings", "Config, theme and log", icon::Gear, kSettingsTabs, 5 },
    };
    constexpr int kPageCount = 5;
    constexpr float kRail = 64.f;
    constexpr ImVec2 kWindow(860.f, 580.f);

    int page = 2;
    int tabs[kPageCount]{};
    float openAnim = 0.f;
    float pageAnim = 1.f;
    int shownKey = -1;
    ImVec2 basePos(-1.f, -1.f);

    void AimLegit()
    {
        if (!ui::BeginColumns("##legit"))
            return;
        ui::NextColumn();
        if (ui::BeginCard("Legitbot", icon::Target))
        {
            ui::Feature("Enabled##legit", &aim::legit, &aim::legitKey);
            ui::Slider("Field of view", &aim::legitFov, 0.5f, 30.f, "%.1f\xC2\xB0");
            ui::Slider("Smoothing", &aim::legitSmooth, 1.f, 30.f, "%.1f");
            ui::Combo("Hitbox##legit", &aim::legitHitbox, "Head\0Neck\0Chest\0Nearest\0");
            ui::Switch("Recoil control", &aim::legitRcs);
        }
        ui::EndCard();
        ui::NextColumn();
        if (ui::BeginCard("Triggerbot", icon::Target))
        {
            ui::Feature("Enabled##trigger", &aim::trigger, &aim::triggerKey);
            ui::SliderInt("Delay", &aim::triggerDelay, 0, 300, "%.0f ms");
            ui::SliderInt("Min damage##trigger", &aim::triggerMinDamage, 1, 100, "%.0f hp");
            ui::Switch("Head##trigger", &aim::triggerHead);
            ui::Switch("Neck##trigger", &aim::triggerNeck);
            ui::Switch("Chest##trigger", &aim::triggerChest);
            ui::Switch("Stomach##trigger", &aim::triggerStomach);
            ui::Switch("Arms##trigger", &aim::triggerArms);
            ui::Switch("Legs##trigger", &aim::triggerLegs);
        }
        ui::EndCard();
        if (ui::BeginCard("Targets##legit", icon::Eye))
        {
            ui::Switch("Teammates##legit", &aim::teammates);
            ui::Switch("Draw FOV circle", &visuals::fovCircle);
        }
        ui::EndCard();
        ui::EndColumns();
    }

    void AimRage()
    {
        if (!ui::BeginColumns("##rage"))
            return;
        ui::NextColumn();
        if (ui::BeginCard("Ragebot", icon::Target))
        {
            ui::Feature("Enabled##rage", &ragebot::enabled, &ragebot::key);
            ui::Slider("Field of view##rage", &ragebot::fov, 1.f, 180.f, "%.0f\xC2\xB0");
            ui::SliderInt("Target lock ticks", &ragebot::lockTicks, 0, 64, "%.0f");
            ui::Switch("Prediction", &ragebot::prediction);
            ui::SliderInt("Extra lead ticks", &ragebot::extraTicks, 0, 8, "%.0f");
            ui::SliderInt("Min damage##rage", &ragebot::minDamage, 1, 100, "%.0f hp");
            ui::Value("Locked", "%s", ragebot::debug.locked ? "yes" : "no");
            ui::Value("Target speed", "%.0f", ragebot::debug.speed);
            ui::Value("Hitbox", "%s", ragebot::debug.group >= 0 ? Hitboxes::GroupName(ragebot::debug.group) : "-");
            ui::Value("Traces", "%d", ragebot::debug.traces);
            ui::Value("Damage", "%.0f", ragebot::debug.damage);
        }
        ui::EndCard();
        if (ui::BeginCard("Hitboxes", icon::Target))
        {
            ui::Switch("Head##hb", &Hitboxes::head);
            ui::Switch("Neck##hb", &Hitboxes::neck);
            ui::Switch("Chest##hb", &Hitboxes::chest);
            ui::Switch("Stomach##hb", &Hitboxes::stomach);
            ui::Switch("Arms##hb", &Hitboxes::arms);
            ui::Switch("Legs##hb", &Hitboxes::legs);
            ui::Switch("Prefer body", &Hitboxes::preferBody);
            ui::Switch("Multipoint", &Hitboxes::multipoint);
            ui::Slider("Head scale", &Hitboxes::headScale, 0.f, 1.f, "%.2f");
            ui::Slider("Body scale", &Hitboxes::bodyScale, 0.f, 1.f, "%.2f");
            ui::Slider("Point scale", &ragebot::pointScale, 0.1f, 1.f, "%.2f");
            ui::SliderInt("Point density", &ragebot::density, 1, 4, "%.0f");
            if (!Hitboxes::Ready())
                ui::Note("Hitbox data not found, bones are used.");
        }
        ui::EndCard();
        ui::NextColumn();
        if (ui::BeginCard("Firing", icon::Sliders))
        {
            ui::Switch("Silent aim", &ragebot::silent);
            ui::Switch("Autofire", &ragebot::autofire);
        }
        ui::EndCard();
        if (ui::BeginCard("Hitchance", icon::Sliders))
        {
            ui::Combo("Mode##spread", &Spread::mode, "Off\0Chance\0Exact seed\0");
            ui::Slider("Min chance", &Spread::minChance, 0.f, 100.f, "%.0f%%");
            ui::SliderInt("Samples", &Spread::samples, 16, 512, "%.0f");
            ui::Value("Last chance", "%.0f%%", Spread::lastChance);
            ui::Value("Seed hits", "%d", Spread::lastExact);
            ui::Value("Seed tick", "%d", Spread::lastTick);
            if (!Spread::SeedReady())
                ui::Note("Seed functions not found, exact mode uses estimate.");
        }
        ui::EndCard();
        if (ui::BeginCard("No spread", icon::Sliders))
        {
            ui::Switch("Enabled##nospread", &NoSpread::enabled);
            ui::Slider("Search##nospread", &NoSpread::maxSearch, 1.f, 45.f, "%.0f deg");
            ui::SliderInt("Iterations##nospread", &NoSpread::iterations, 1, 8, "%.0f");
            ui::Value("Seed##nospread", "%u", NoSpread::lastSeed);
            ui::Value("Error##nospread", "%.4f", NoSpread::lastError);
            if (!NoSpread::Ready())
                ui::Note("Spread random not found.");
            else if (!ragebot::silent)
                ui::Note("Requires silent aim.");
        }
        ui::EndCard();
        if (ui::BeginCard("Targets##rage", icon::Eye))
        {
            ui::Switch("Teammates##rage", &aim::teammates);
        }
        ui::EndCard();
        ui::EndColumns();
    }

    void VisualsPage()
    {
        if (!ui::BeginColumns("##visuals"))
            return;
        ui::NextColumn();
        if (ui::BeginCard("Players", icon::Eye))
        {
            ui::Switch("Enabled##esp", &visuals::esp);
            ui::Switch("Box", &visuals::box);
            ui::Switch("Name", &visuals::name);
            ui::Switch("Health bar", &visuals::health);
            ui::Switch("Weapon", &visuals::weapon);
            ui::Switch("Distance", &visuals::distance);
            ui::Switch("Skeleton", &visuals::skeleton);
            ui::Switch("Snaplines", &visuals::snaplines);
            ui::Switch("Teammates##esp", &visuals::teammates);
        }
        ui::EndCard();
        ui::NextColumn();
        if (ui::BeginCard("Colors", icon::Palette))
        {
            ui::ColorEdit("Visible", visuals::visibleColor);
            ui::ColorEdit("Hidden", visuals::hiddenColor);
            ui::ColorEdit("Teammates##color", visuals::teamColor);
        }
        ui::EndCard();
        if (ui::BeginCard("Glow", icon::Eye))
        {
            ui::Switch("Enabled##glow", &glow::enabled);
            ui::Switch("Teammates##glow", &glow::teammates);
            ui::Switch("Color by visibility##glow", &glow::byVisibility);
            ui::ColorEdit("Enemy##glowc", glow::enemyColor);
            ui::ColorEdit("Behind wall##glowc", glow::hiddenColor);
            ui::ColorEdit("Team##glowc", glow::teamColor);
        }
        ui::EndCard();
        ui::EndColumns();
    }

    void SkinOptions()
    {
        if (!ui::BeginColumns("##skinopts"))
            return;
        ui::NextColumn();
        if (ui::BeginCard("Skin changer", icon::Box))
        {
            ui::Switch("Apply skins in match", &skins::enabled);
            if (ui::Combo("Paint source", &skins::paintMode, "Inventory item\0Fallback fields\0"))
                skins::Refresh();
            ui::Combo("Item names", &items::language, "Russian\0English\0");
            if (ui::Button("Reapply skins", ImVec2(0.f, 32.f), icon::Check))
                skins::Refresh();
        }
        ui::EndCard();
        if (ui::BeginCard("Knife", icon::Box))
        {
            if (ui::Switch("Native animations", &skins::knifeAnimations))
                skins::Refresh();
            const int mode = skins::knifeMode;
            ui::Value("Mode", "%s", mode == 2 ? "subclass" : mode < 0 ? "model only" : "-");
        }
        ui::EndCard();
        ui::NextColumn();
        if (ui::BeginCard("Diagnostics", icon::Bug))
        {
            const skins::Debug d = skins::debug;
            ui::Value("Inventory", "%s", skins::inventoryReady ? "ready" : "waiting");
            if (!d.valid)
                ui::Note("Hold a weapon in a match to see its state.");
            else
            {
                const items::Item* it = items::Find(d.def);
                ui::Value("Weapon", "%s (%d)", it ? items::Name(it->name) : "?", d.def);
                ui::Value("Item ID", "%llu", static_cast<unsigned long long>(d.itemId));
                ui::Value("Paint", "%d", d.paint);
                ui::Value("Fallback paint", "%d", d.fallbackPaint);
                ui::Value("Legacy model", "%s", d.legacy ? "yes" : "no");
                ui::Value("Mesh mask", "%llu / %llu", static_cast<unsigned long long>(d.mask), static_cast<unsigned long long>(d.attachMask));
                ui::Value("Hands clone", "%s", d.attach ? "yes" : "no");
                ui::Value("Regenerations", "%d", d.regenerations);
                ui::Note(d.model);
            }
        }
        ui::EndCard();
        ui::EndColumns();
    }

    void StrafeOptions()
    {
        ui::Combo("Mode##strafe", &movement::strafeMode, "Analog\0Subtick yaw\0");
    }

    void MiscPage(int tab)
    {
        if (!ui::BeginColumns("##misc"))
            return;
        ui::NextColumn();
        if (tab == 0)
        {
            if (ui::BeginCard("Movement", icon::Run))
            {
                ui::Feature("Bunnyhop", &movement::bhop, &misc::bhopBind);
                ui::Feature("Autostrafe", &movement::autostrafe, &misc::strafeBind, StrafeOptions);
                ui::Switch("Subtick jump", &JumpCheck::enabled);
                ui::Switch("Trace ground##jump", &JumpCheck::traceGround);
                ui::Slider("Jump lead", &JumpCheck::lead, 0.f, 0.5f, "%.2f tick");
                if (!movement::installed)
                    ui::Note("CreateMove hook not found.");
            }
            ui::EndCard();
            ui::NextColumn();
            if (ui::BeginCard("State", icon::Bug))
            {
                const auto& d = movement::debug;
                ui::Value("On ground", "%s", d.ground ? "yes" : "no");
                ui::Value("Jump held", "%s", d.held ? "yes" : "no");
                ui::Value("Speed", "%.0f u/s", d.speed);
                ui::Value("Commands", "%d", d.cmds);
                const auto& j = JumpCheck::last;
                ui::Value("Landing", "%.2f", j.fraction);
                ui::Value("Last landed", "%d +%.2f", j.landedTick, j.landedFrac);
                ui::Value("Last press", "%d +%.2f", j.pressTick, j.pressFrac);
                ui::Value("Jump precision", "%s", j.precision ? "on" : "off");
                if (d.fault)
                    ui::Value("Fault", "0x%08lX (%d)", d.fault, d.faultStage);
            }
            ui::EndCard();
        }
        else if (tab == 1)
        {
            if (ui::BeginCard("Thirdperson", icon::Camera))
            {
                ui::Feature("Enabled##third", &misc::thirdperson, &misc::thirdBind);
                ui::Slider("Distance", &misc::thirdDistance, 50.f, 300.f, "%.0f");
                if (!misc::cameraHooked)
                    ui::Note("Camera hook not found.");
            }
            ui::EndCard();
            ui::NextColumn();
            if (ui::BeginCard("Hands", icon::Palette))
            {
                ui::Switch("Arms color", &hands::arms);
                ui::ColorEdit("Arms", hands::armsColor);
                ui::Switch("Weapon color", &hands::weapon);
                ui::ColorEdit("Weapon", hands::weaponColor);
                ui::Value("Draw hook", "%s", hands::debug.hooked ? "hooked" : "missing");
                ui::Value("Viewmodel", "%s", hands::debug.source == 1 ? "services" : hands::debug.source == 2 ? "entity scan" : "-");
                ui::Value("Weapon model", "%s", hands::debug.attachment ? "found" : "-");
                ui::Value("Draw calls", "%u", hands::debug.calls);
                if (hands::debug.ownerOffset >= 0)
                    ui::Value("Owner", "0x%X %s", hands::debug.ownerOffset, hands::debug.ownerKind == 2 ? "ptr" : "handle");
                else
                    ui::Value("Owner", "calibrating (0x%X)", hands::debug.candidate);
                ui::Value("Tinted meshes", "%u", hands::debug.tinted);
            }
            ui::EndCard();
        }
        else
        {
            if (ui::BeginCard("Overlay", icon::List))
            {
                ui::Switch("Watermark", &misc::watermark);
                ui::Switch("Keybind list", &misc::keybinds);
            }
            ui::EndCard();
            ui::NextColumn();
        }
        ui::EndColumns();
    }

    void SettingsGeneral()
    {
        if (!ui::BeginColumns("##general"))
            return;
        ui::NextColumn();
        if (ui::BeginCard("Config", icon::Save))
        {
            if (ui::Button("Save", ImVec2(0.f, 32.f), icon::Save))
                settings::Save();
            if (ui::Button("Load", ImVec2(0.f, 32.f), icon::Folder))
                settings::Load();
            ui::Note(settings::Path());
        }
        ui::EndCard();
        ui::NextColumn();
        if (ui::BeginCard("Session", icon::Power))
        {
            ui::Value("Game", "%s", game::Ready() ? "ready" : game::Error().c_str());
            ui::Value("Items", "%s", items::state == 1 ? "loaded" : items::state ? "error" : "loading");
            ui::Value("CreateMove", "%s", movement::installed ? "hooked" : "missing");
            ui::Value("Camera", "%s", misc::cameraHooked ? "hooked" : "missing");
            ImGui::Dummy(ImVec2(0.f, 4.f));
            if (ui::Button("Unload", ImVec2(0.f, 34.f), icon::Power, ui::Destructive))
                hooks::unload = true;
        }
        ui::EndCard();
        ui::EndColumns();
    }

    void SettingsTheme()
    {
        if (!ui::BeginColumns("##theme"))
            return;
        ui::NextColumn();
        if (ui::BeginCard("Appearance", icon::Palette))
        {
            ui::ColorEdit("Accent", ui::accent);
            static const float presets[][3] = {
                { 0.25f, 0.72f, 1.f }, { 0.45f, 0.86f, 0.62f }, { 1.f, 0.62f, 0.32f },
                { 0.96f, 0.42f, 0.55f }, { 0.68f, 0.56f, 1.f }, { 0.92f, 0.92f, 0.95f },
            };
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 p = ImGui::GetCursorScreenPos();
            for (int i = 0; i < 6; ++i)
            {
                const ImVec2 c(p.x + 11.f + i * 30.f, p.y + 11.f);
                ImGui::SetCursorScreenPos(ImVec2(c.x - 11.f, c.y - 11.f));
                ImGui::PushID(i);
                if (ImGui::InvisibleButton("##preset", ImVec2(22.f, 22.f)))
                    memcpy(ui::accent, presets[i], sizeof(presets[i]));
                const bool hovered = ImGui::IsItemHovered();
                ImGui::PopID();
                dl->AddCircleFilled(c, hovered ? 10.f : 9.f, ImGui::ColorConvertFloat4ToU32(ImVec4(presets[i][0], presets[i][1], presets[i][2], 1.f)), 32);
            }
            ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + 30.f));
            ui::Switch("Reduce animations", &ui::reduceMotion);
        }
        ui::EndCard();
        ui::NextColumn();
        ui::EndColumns();
    }

    void SettingsEvents()
    {
        if (!ui::BeginCard("Event bus", icon::List))
        {
            ui::EndCard();
            return;
        }
        if (ui::Button("Reset stats", ImVec2(130.f, 28.f), icon::Trash))
            events::ResetStats();
        ImGui::Dummy(ImVec2(0.f, 4.f));
        const float w = ImGui::GetContentRegionAvail().x;
        const float cols[] = { 0.f, w * 0.2f, w * 0.48f, w * 0.62f, w * 0.75f, w * 0.88f };
        const char* heads[] = { "Event", "Subscriber", "Calls", "Avg us", "Peak us", "State" };
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        for (int i = 0; i < 6; ++i)
            ui::Print(dl, ui::fonts.regular, 12.f, ImVec2(p.x + cols[i], p.y), ui::Color(ui::TextFaint), heads[i]);
        ImGui::Dummy(ImVec2(w, 20.f));
        ImGui::BeginChild("##events", ImVec2(0.f, 340.f));
        dl = ImGui::GetWindowDrawList();
        int lastType = -1;
        for (const events::Info& e : events::Snapshot())
        {
            p = ImGui::GetCursorScreenPos();
            constexpr float h = 26.f;
            if (e.type != lastType && lastType >= 0)
                dl->AddLine(ImVec2(p.x, p.y), ImVec2(p.x + w, p.y), ui::Color(ui::Border, 0.6f));
            const float ty = p.y + (h - ImGui::GetTextLineHeight()) * 0.5f;
            if (e.type != lastType)
                ui::Print(dl, ImVec2(p.x + cols[0], ty), ui::Accent(), events::Name(e.type));
            lastType = e.type;
            char buf[32];
            ui::Print(dl, ImVec2(p.x + cols[1], ty), ui::Color(ui::Text), e.name);
            snprintf(buf, sizeof(buf), "%llu", static_cast<unsigned long long>(e.calls));
            ui::Print(dl, ImVec2(p.x + cols[2], ty), ui::Color(ui::TextDim), buf);
            snprintf(buf, sizeof(buf), "%.1f", e.avgUs);
            ui::Print(dl, ImVec2(p.x + cols[3], ty), ui::Color(ui::TextDim), buf);
            snprintf(buf, sizeof(buf), "%.1f", e.maxUs);
            ui::Print(dl, ImVec2(p.x + cols[4], ty), ui::Color(e.maxUs > 1000.0 ? ui::Warn : ui::TextDim), buf);
            if (e.faulted)
            {
                ImGui::SetCursorScreenPos(ImVec2(p.x + cols[5], p.y + 2.f));
                ImGui::PushID(e.type * 64 + e.index);
                if (ui::Button("Enable", ImVec2(w - cols[5], h - 4.f), nullptr, ui::Destructive))
                    events::Enable(e.type, e.index);
                ImGui::PopID();
            }
            else
            {
                dl->AddCircleFilled(ImVec2(p.x + cols[5] + 5.f, p.y + h * 0.5f), 3.5f, ui::Color(ui::Good), 16);
                ui::Print(dl, ImVec2(p.x + cols[5] + 16.f, ty), ui::Color(ui::TextDim), "ok");
            }
            ImGui::SetCursorScreenPos(p);
            ImGui::Dummy(ImVec2(w, h));
        }
        ImGui::EndChild();
        ui::EndCard();
    }

    void SettingsPatterns()
    {
        if (ui::BeginCard("Signatures", icon::Bug))
        {
            const bool busy = patterns::Busy();
            const float bw = (ImGui::GetContentRegionAvail().x - 16.f) / 3.f;
            if (ui::Button(busy ? "Working..." : "Verify", ImVec2(bw, 30.f), icon::Check))
                patterns::Verify();
            ImGui::SameLine(0.f, 8.f);
            if (ui::Button("Generate all", ImVec2(bw, 30.f), icon::Plus, ui::Primary))
                patterns::GenerateAll();
            ImGui::SameLine(0.f, 8.f);
            if (ui::Button("Export", ImVec2(bw, 30.f), icon::Save))
                patterns::Export();
            ui::Switch("Wildcard struct offsets", &patterns::looseOffsets);
            ui::Note("Click a row to copy. Generated patterns come from the clean module file on disk.");
            ImGui::Dummy(ImVec2(0.f, 4.f));
            ImGui::BeginChild("##patterns", ImVec2(0.f, 250.f));
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const float w = ImGui::GetContentRegionAvail().x;
            const std::vector<patterns::Entry> list = patterns::Snapshot();
            for (int i = 0; i < static_cast<int>(list.size()); ++i)
            {
                const patterns::Entry& e = list[i];
                const bool hasGen = !e.generated.empty();
                const float h = hasGen ? 52.f : 36.f;
                const ImVec2 p = ImGui::GetCursorScreenPos();
                ImGui::PushID(i);
                const bool clicked = ImGui::InvisibleButton("##row", ImVec2(w - 70.f, h));
                const bool hovered = ImGui::IsItemHovered();
                ImGui::SetCursorScreenPos(ImVec2(p.x + w - 62.f, p.y + 6.f));
                if (ui::Button(e.pending ? "..." : "Gen", ImVec2(60.f, 24.f)) && !e.pending)
                    patterns::Generate(i);
                ImGui::PopID();
                if (hovered)
                    dl->AddRectFilled(p, ImVec2(p.x + w - 70.f, p.y + h - 2.f), ui::Color(ui::ControlHover, 0.6f), 6.f);
                const ui::Tone tone = e.status == patterns::Missing ? ui::Danger : e.status == patterns::Ambiguous ? ui::Warn : e.status == patterns::Unique ? ui::Good : ui::TextDim;
                dl->AddCircleFilled(ImVec2(p.x + 10.f, p.y + 13.f), 3.5f, ui::Color(tone), 16);
                ui::Print(dl, ImVec2(p.x + 22.f, p.y + 5.f), ui::Color(ui::Text), e.name.c_str());
                char meta[64];
                if (e.status == patterns::Missing)
                    snprintf(meta, sizeof(meta), "%s  not found", e.module.c_str());
                else if (e.matches > 1)
                    snprintf(meta, sizeof(meta), "%s+0x%X  %d+ matches", e.module.c_str(), e.rva, e.matches);
                else
                    snprintf(meta, sizeof(meta), "%s+0x%X", e.module.c_str(), e.rva);
                const ImVec2 ms = ImGui::CalcTextSize(meta);
                ui::Print(dl, ImVec2(p.x + w - 78.f - ms.x, p.y + 5.f), ui::Color(ui::TextFaint), meta);
                dl->PushClipRect(p, ImVec2(p.x + w - 76.f, p.y + h), true);
                ui::Print(dl, ui::fonts.regular, 12.f, ImVec2(p.x + 22.f, p.y + 21.f), ui::Color(ui::TextDim), e.pattern.c_str());
                if (hasGen)
                    ui::Print(dl, ui::fonts.regular, 12.f, ImVec2(p.x + 22.f, p.y + 36.f), ui::Accent(), e.generated.c_str());
                dl->PopClipRect();
                if (clicked)
                {
                    ImGui::SetClipboardText(hasGen && e.generated != "-" ? e.generated.c_str() : e.pattern.c_str());
                    logs::Add(logs::Info, "Copied %s", e.name.c_str());
                }
                ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h));
            }
            ImGui::Dummy(ImVec2(0.f, 0.f));
            ImGui::EndChild();
        }
        ui::EndCard();

        if (ui::BeginCard("Generator", icon::Plus))
        {
            static char module[64] = "client.dll";
            static char rva[24] = "";
            const float avail = ImGui::GetContentRegionAvail().x;
            ImGui::SetNextItemWidth(avail * 0.35f);
            ImGui::InputTextWithHint("##module", "module", module, sizeof(module));
            ImGui::SameLine(0.f, 8.f);
            ImGui::SetNextItemWidth(avail * 0.35f);
            ImGui::InputTextWithHint("##rva", "RVA, e.g. 37C8E0", rva, sizeof(rva), ImGuiInputTextFlags_CharsHexadecimal);
            ImGui::SameLine(0.f, 8.f);
            bool pending = false;
            const std::string result = patterns::Custom(&pending);
            if (ui::Button(pending ? "..." : "Generate", ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight()), nullptr, ui::Primary) && rva[0] && !pending)
                patterns::GenerateAt(module, static_cast<uint32_t>(strtoul(rva, nullptr, 16)));
            if (!result.empty())
            {
                ImGui::Dummy(ImVec2(0.f, 4.f));
                ImGui::PushTextWrapPos(0.f);
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::Accent()), "%s", result.c_str());
                ImGui::PopTextWrapPos();
                if (ui::Button("Copy", ImVec2(110.f, 28.f)))
                    ImGui::SetClipboardText(result.c_str());
            }
        }
        ui::EndCard();
    }

    void SettingsLog()
    {
        if (ui::BeginCard("Log", icon::List))
        {
            if (ui::Button("Clear", ImVec2(110.f, 28.f), icon::Trash))
                logs::Clear();
            ImGui::BeginChild("##lines", ImVec2(0.f, 330.f));
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const uint64_t now = GetTickCount64();
            for (const auto& line : logs::Snapshot())
            {
                const ImVec2 p = ImGui::GetCursorScreenPos();
                const ui::Tone tone = line.level == logs::Error ? ui::Danger : line.level == logs::Warning ? ui::Warn : line.level == logs::Success ? ui::Good : ui::TextDim;
                dl->AddCircleFilled(ImVec2(p.x + 5.f, p.y + ImGui::GetTextLineHeight() * 0.5f), 3.5f, ui::Color(tone), 16);
                char age[24];
                snprintf(age, sizeof(age), "%llus", static_cast<unsigned long long>((now - line.time) / 1000));
                ui::Print(dl, ImVec2(p.x + 16.f, p.y), ui::Color(ui::TextFaint), age);
                ui::Print(dl, ImVec2(p.x + 60.f, p.y), ui::Color(ui::Text), line.text);
                ImGui::Dummy(ImVec2(0.f, ImGui::GetTextLineHeight() + 4.f));
            }
            ImGui::EndChild();
        }
        ui::EndCard();
    }

    void Content()
    {
        const int tab = tabs[page];
        switch (page)
        {
        case 0: tab == 0 ? AimLegit() : AimRage(); break;
        case 1: VisualsPage(); break;
        case 2:
            if (tab == 0)
                InventoryTab();
            else if (tab == 1)
                AddTab();
            else
                SkinOptions();
            break;
        case 3: MiscPage(tab); break;
        default:
            if (tab == 0)
                SettingsGeneral();
            else if (tab == 1)
                SettingsTheme();
            else if (tab == 2)
                SettingsEvents();
            else if (tab == 3)
                SettingsPatterns();
            else
                SettingsLog();
            break;
        }
    }

    void Logo(ImDrawList* dl, ImVec2 pos, float size)
    {
        svg::Fill(dl, icon::LogoOuter, pos, size, ui::Accent());
        svg::Fill(dl, icon::LogoInner, pos, size, ui::Color(ui::Rail));
        svg::Fill(dl, icon::LogoNotch, pos, size, ui::Color(ui::Rail));
        svg::Fill(dl, icon::LogoCore, pos, size, ui::Accent());
    }

    void NavItem(ImDrawList* dl, int index, ImVec2 center)
    {
        constexpr float s = 40.f;
        ImGui::SetCursorScreenPos(ImVec2(center.x - s * 0.5f, center.y - s * 0.5f));
        ImGui::PushID(index);
        if (ImGui::InvisibleButton("##nav", ImVec2(s, s)))
            page = index;
        const bool hovered = ImGui::IsItemHovered();
        const float sel = ui::Animate("##sel", page == index ? 1.f : 0.f, 16.f);
        const float hv = ui::Animate("##hv", hovered ? 1.f : 0.f, 20.f);
        ImGui::PopID();
        const ImVec2 a(center.x - s * 0.5f, center.y - s * 0.5f), b(center.x + s * 0.5f, center.y + s * 0.5f);
        if (hv > 0.01f)
            dl->AddRectFilled(a, b, ui::Color(ui::ControlHover, hv * (1.f - sel)), 11.f);
        if (sel > 0.01f)
        {
            dl->AddRectFilled(a, b, ui::Accent(0.14f * sel), 11.f);
            const float bar = 18.f * sel;
            dl->AddRectFilled(ImVec2(center.x - kRail * 0.5f, center.y - bar * 0.5f), ImVec2(center.x - kRail * 0.5f + 3.f, center.y + bar * 0.5f), ui::Accent(sel), 2.f);
        }
        const ImU32 col = ui::Mix(ui::Mix(ui::Color(ui::TextFaint), ui::Color(ui::Text), hv), ui::Accent(), sel);
        svg::Stroke(dl, kPages[index].icon, ImVec2(center.x - 11.f, center.y - 11.f), 22.f, col, 1.9f);
        if (hv > 0.01f)
        {
            ImDrawList* fg = ImGui::GetForegroundDrawList();
            const char* name = kPages[index].name;
            const ImVec2 ts = ImGui::CalcTextSize(name);
            const ImVec2 ta(b.x + 14.f + (1.f - hv) * 6.f, center.y - 13.f), tb(ta.x + ts.x + 18.f, center.y + 13.f);
            ui::SoftShadow(fg, ImVec2(ta.x, ta.y + 2.f), ImVec2(tb.x, tb.y + 2.f), 7.f, 10.f, IM_COL32(0, 0, 0, 255), 0.4f * hv);
            render::Gradient(fg, ta, tb, ui::Color(ui::ControlHover, hv), ui::Color(ui::Surface, hv), 7.f);
            fg->AddRect(ta, tb, ui::Color(ui::Border, hv), 7.f);
            ui::Print(fg, ImVec2(ta.x + 9.f, center.y - ts.y * 0.5f), ui::Color(ui::Text, hv), name);
        }
    }

    void Frame()
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
        ui::Chrome(wp, ws);
        render::Gradient(dl, wp, ImVec2(wp.x + ws.x, wp.y + ws.y), ui::Mix(ui::Color(ui::Background), ui::Color(ui::Surface), 0.45f), ui::Color(ui::Background), 12.f);
        dl->AddRectFilled(wp, ImVec2(wp.x + kRail, wp.y + ws.y), ui::Color(ui::Rail), 12.f, ImDrawFlags_RoundCornersLeft);
        dl->AddLine(ImVec2(wp.x + kRail, wp.y), ImVec2(wp.x + kRail, wp.y + ws.y), ui::Color(ui::Border));
        Logo(dl, ImVec2(wp.x + 16.f, wp.y + 18.f), 32.f);
        for (int i = 0; i < kPageCount - 1; ++i)
            NavItem(dl, i, ImVec2(wp.x + kRail * 0.5f, wp.y + 96.f + i * 52.f));
        NavItem(dl, kPageCount - 1, ImVec2(wp.x + kRail * 0.5f, wp.y + ws.y - 40.f));

        const Page& pg = kPages[page];
        const float x0 = wp.x + kRail + 24.f;
        render::Gradient(dl, ImVec2(wp.x + kRail + 1.f, wp.y + 1.f), ImVec2(wp.x + ws.x - 1.f, wp.y + 72.f), ui::Accent(0.08f), ui::Accent(0.f), 0.f, render::Horizontal);
        ui::Heading(dl, ImVec2(x0, wp.y + 15.f), 21.f, ui::Color(ui::Text), pg.name);
        ui::Print(dl, ui::fonts.regular, 13.f, ImVec2(x0, wp.y + 42.f), ui::Color(ui::TextDim), pg.subtitle);
        if (pg.tabCount)
        {
            const float w = 92.f * pg.tabCount;
            ImGui::SetCursorScreenPos(ImVec2(wp.x + ws.x - 24.f - w, wp.y + 20.f));
            ui::Segmented("##tabs", &tabs[page], pg.tabs, pg.tabCount, w);
        }
        dl->AddLine(ImVec2(wp.x + kRail, wp.y + 72.f), ImVec2(wp.x + ws.x, wp.y + 72.f), ui::Color(ui::Border));

        const int key = page * 8 + tabs[page];
        if (key != shownKey)
        {
            shownKey = key;
            pageAnim = 0.f;
        }
        pageAnim = ui::reduceMotion ? 1.f : std::min(1.f, pageAnim + ImGui::GetIO().DeltaTime * 7.f);
        const float e = ui::Ease(pageAnim);

        ImGui::SetCursorScreenPos(ImVec2(wp.x + kRail + 1.f, wp.y + 73.f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(22.f, 16.f));
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * e);
        ImGui::BeginChild("##content", ImVec2(ws.x - kRail - 2.f, ws.y - 74.f), ImGuiChildFlags_AlwaysUseWindowPadding);
        const float shift = (1.f - e) * 14.f;
        if (shift > 0.5f)
            ImGui::Indent(shift);
        Content();
        if (shift > 0.5f)
            ImGui::Unindent(shift);
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
    }
}

void menu::Style()
{
    ui::ApplyStyle();
}

bool menu::NeedsFrame()
{
    return open || openAnim > 0.001f || ui::ToastsActive();
}

void menu::Render()
{
    const float dt = ImGui::GetIO().DeltaTime;
    openAnim = ui::reduceMotion ? (open ? 1.f : 0.f) : std::clamp(openAnim + (open ? dt : -dt) * 7.f, 0.f, 1.f);
    ui::Toasts();
    if (openAnim <= 0.001f)
        return;
    ui::ApplyStyle();
    icons::Frame();
    const float e = ui::Ease(openAnim);
    const ImVec2 screen = ImGui::GetIO().DisplaySize;
    if (basePos.x < 0.f && screen.x > kWindow.x)
        basePos = ImVec2((screen.x - kWindow.x) * 0.5f, (screen.y - kWindow.y) * 0.5f);
    if (basePos.x >= 0.f)
        ImGui::SetNextWindowPos(ImVec2(basePos.x, basePos.y + (1.f - e) * 14.f), openAnim < 1.f ? ImGuiCond_Always : ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(kWindow, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, e);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings;
    if (!open)
        flags |= ImGuiWindowFlags_NoInputs;
    const bool visible = ImGui::Begin("##internal", nullptr, flags);
    ImGui::PopStyleVar();
    if (visible)
    {
        if (openAnim >= 1.f)
            basePos = ImGui::GetWindowPos();
        Frame();
    }
    ImGui::End();
    ImGui::PopStyleVar();
}
