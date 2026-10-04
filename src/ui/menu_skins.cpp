#include "menu_pages.h"
#include "ui.h"
#include "catalog.h"
#include "svg.h"
#include "icons_svg.h"
#include "../icons.h"
#include "../items.h"
#include "../core/log.h"
#include "../core/settings.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iterator>
#include <mutex>
#include <string>
#include <vector>

namespace
{
    using ui::font_role;
    using ui::theme::token;
    namespace theme = ui::theme;
    namespace catalog = ui::catalog;
    using inventory_entry = settings::changer::inventory_entry;

    constexpr int team_t = 1;
    constexpr int team_ct = 2;
    constexpr int sticker_slots = 5;
    constexpr int max_seed = 1000;
    constexpr int max_kills = 999999;

    enum class item_kind
    {
        gun,
        knife,
        glove,
        agent
    };

    struct draft_state
    {
        std::int16_t def = 0;
        int paint = 0;
        float wear = 0.0001f;
        int seed = 0;
        int stattrak = -1;
        char tag[64]{};
        std::array<int, sticker_slots> stickers{};
        int equip = 0;
    };

    int g_category = catalog::rifle;
    std::int16_t g_selected = 0;
    char g_search[128]{};
    std::vector<std::size_t> g_results;
    std::string g_results_key;

    draft_state g_draft;
    bool g_open_draft = false;
    int g_sticker_slot = 0;
    char g_sticker_search[128]{};
    std::vector<std::size_t> g_sticker_filter;
    std::string g_sticker_key;

    int g_inventory_filter = 0;
    char g_inventory_search[128]{};
    int g_inventory_uid = 0;
    bool g_open_inventory = false;

    item_kind kind_of(std::int16_t def)
    {
        if (def >= 500 && def < 600)
            return item_kind::knife;
        if (def == 4725 || (def >= 5027 && def <= 5035))
            return item_kind::glove;
        if (const catalog::item* it = catalog::find(def); it && it->category == catalog::agent)
            return item_kind::agent;
        return item_kind::gun;
    }

    bool conflicts(const inventory_entry& other, std::int16_t def)
    {
        const item_kind a = kind_of(other.def_index);
        if (a != kind_of(def))
            return false;
        return a != item_kind::gun || other.def_index == def;
    }

    bool& equip_flag(inventory_entry& e, int team)
    {
        return team == team_t ? e.equipped_t : e.equipped_ct;
    }

    void set_equip(inventory_entry& e, int team, bool on)
    {
        auto& changer = settings::g_changer;
        if (on)
            for (inventory_entry& other : changer.inventory)
                if (other.uid != e.uid && conflicts(other, e.def_index))
                    equip_flag(other, team) = false;
        equip_flag(e, team) = on;
        if (kind_of(e.def_index) != item_kind::agent)
            return;
        std::int16_t& slot = team == team_t ? changer.agents.t_def : changer.agents.ct_def;
        if (on)
            slot = e.def_index;
        else if (slot == e.def_index)
            slot = 0;
    }

    inventory_entry* find_uid(int uid)
    {
        auto& inv = settings::g_changer.inventory;
        const auto it = std::find_if(inv.begin(), inv.end(), [uid](const inventory_entry& e) { return e.uid == uid; });
        return it == inv.end() ? nullptr : &*it;
    }

    std::vector<inventory_entry> inventory_snapshot()
    {
        std::lock_guard lock(settings::g_changer_mutex);
        return settings::g_changer.inventory;
    }

    void add_draft(const draft_state& d)
    {
        std::lock_guard lock(settings::g_changer_mutex);
        auto& changer = settings::g_changer;
        inventory_entry e{};
        e.uid = changer.next_uid++;
        e.def_index = d.def;
        e.skin.paint_kit_id = d.paint;
        e.skin.seed = d.seed;
        e.skin.wear = d.wear;
        e.skin.stattrak = d.stattrak;
        e.skin.nametag = d.tag;
        e.skin.stickers = d.stickers;
        changer.inventory.push_back(std::move(e));
        const int uid = changer.inventory.back().uid;
        for (const int team : { team_t, team_ct })
            if (d.equip & team)
                if (inventory_entry* added = find_uid(uid))
                    set_equip(*added, team, true);
    }

    void equip_uid(int uid, int team, bool on)
    {
        std::lock_guard lock(settings::g_changer_mutex);
        if (inventory_entry* e = find_uid(uid))
            set_equip(*e, team, on);
    }

    void remove_uid(int uid)
    {
        std::lock_guard lock(settings::g_changer_mutex);
        auto& inv = settings::g_changer.inventory;
        inventory_entry* e = find_uid(uid);
        if (!e)
            return;
        set_equip(*e, team_t, false);
        set_equip(*e, team_ct, false);
        inv.erase(inv.begin() + (e - inv.data()));
    }

    void remove_all()
    {
        std::lock_guard lock(settings::g_changer_mutex);
        auto& inv = settings::g_changer.inventory;
        for (inventory_entry& e : inv)
        {
            set_equip(e, team_t, false);
            set_equip(e, team_ct, false);
        }
        inv.clear();
    }

    float card_height(float w)
    {
        return std::floor(w * ui::ratio::item_image) + ui::m().item_card_text;
    }

    void draw_icon(ImDrawList* dl, const std::string& path, ImVec2 a, ImVec2 b)
    {
        const ui::metrics& mt = ui::m();
        const std::uint64_t tex = path.empty() ? 0 : icons::Get(path);
        if (!tex)
        {
            const float s = mt.icon_placeholder;
            svg::Stroke(dl, icon::Box, ImVec2((a.x + b.x - s) * 0.5f, (a.y + b.y - s) * 0.5f), s, theme::color(token::text_faint), mt.stroke_thin);
            return;
        }
        const float w = b.x - a.x, h = b.y - a.y;
        if (w <= 0.f || h <= 0.f)
            return;
        float iw = w, ih = w * ui::ratio::icon_aspect;
        if (ih > h)
        {
            ih = h;
            iw = h / ui::ratio::icon_aspect;
        }
        const ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
        const ImU32 tint = theme::color(token::white);
        dl->AddImage(ImTextureRef(static_cast<ImTextureID>(tex)), ImVec2(c.x - iw * 0.5f, c.y - ih * 0.5f), ImVec2(c.x + iw * 0.5f, c.y + ih * 0.5f), ImVec2(0.f, 0.f), ImVec2(1.f, 1.f), tint);
    }

    void badge(ImDrawList* dl, float& x, float y, const char* label, ImU32 col)
    {
        const ui::metrics& mt = ui::m();
        const ImVec2 ts = ui::text_size(font_role::badge, label);
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + ts.x + mt.badge_pad * 2.f, y + mt.badge_height), theme::mul_alpha(col, 0.22f), mt.radius_xs);
        ui::text(dl, font_role::badge, ImVec2(x + mt.badge_pad, y + (mt.badge_height - ui::font_size(font_role::badge)) * 0.5f), col, label);
        x += ts.x + mt.badge_pad * 2.f + mt.badge_gap;
    }

    bool item_card(int id, float w, const std::string& icon_path, const std::string& title, const char* subtitle, int rarity, int teams, bool stattrak, const std::string& tooltip)
    {
        const ui::metrics& mt = ui::m();
        const float h = card_height(w);
        ImGui::PushID(id);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const bool clicked = ImGui::InvisibleButton("##card", ImVec2(w, h));
        const bool hovered = ImGui::IsItemHovered();
        const float hv = ui::animate("##hv", hovered ? 1.f : 0.f, 18.f);
        ImGui::PopID();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImU32 rc = catalog::rarity_color(rarity);
        const float lift = hv * mt.item_card_lift;
        const ImVec2 a(p.x, p.y - lift), b(p.x + w, p.y + h - lift);
        const float r = mt.radius_lg;
        const float in = mt.hairline;
        if (hv > 0.01f)
            ui::soft_shadow(dl, ImVec2(a.x + mt.space_xs, a.y + mt.space_sm), ImVec2(b.x - mt.space_xs, b.y + mt.space_xxs), r, mt.item_card_shadow, 0.5f * hv);
        dl->AddRectFilled(a, b, theme::mix(theme::color(token::surface), theme::color(token::control_hover), hv * 0.6f), r);
        const float image_h = std::floor(w * ui::ratio::item_image);
        const ImVec2 ia(a.x + in, a.y + in), ib(b.x - in, a.y + image_h);
        dl->AddRectFilled(ia, ib, theme::mul_alpha(rc, 0.07f + 0.05f * hv), r - in, ImDrawFlags_RoundCornersTop);
        dl->PushClipRect(ia, ib, true);
        ui::glow(dl, ImVec2((a.x + b.x) * 0.5f, a.y + image_h * 0.55f), w * ui::ratio::card_glow, rc, 0.32f + 0.18f * hv);
        dl->PopClipRect();
        draw_icon(dl, icon_path, ImVec2(a.x + mt.space_md, a.y + mt.space_sm), ImVec2(b.x - mt.space_md, a.y + image_h - mt.space_xs));
        dl->AddRect(a, b, hovered ? theme::accent(0.55f) : theme::color(token::border, 0.9f), r, 0, mt.hairline);
        const float tx = a.x + mt.item_card_pad;
        dl->PushClipRect(ImVec2(tx, a.y), ImVec2(b.x - mt.item_card_pad, b.y), true);
        ui::text(dl, font_role::body, ImVec2(tx, a.y + image_h + mt.space_sm), theme::color(token::text), title.c_str());
        if (subtitle && *subtitle)
            ui::text(dl, font_role::caption, ImVec2(tx, a.y + image_h + mt.space_sm + ui::font_size(font_role::body) + mt.space_xs), theme::color(token::text_dim), subtitle);
        dl->PopClipRect();
        const float bar_w = (w - mt.item_card_pad * 2.f) * (ui::ratio::card_bar_idle + (1.f - ui::ratio::card_bar_idle) * hv);
        dl->AddRectFilled(ImVec2(tx, b.y - mt.space_xs - mt.item_card_bar), ImVec2(tx + bar_w, b.y - mt.space_xs), rc, mt.item_card_bar * 0.5f);
        float bx = a.x + mt.badge_inset;
        const float by = a.y + mt.badge_inset;
        if (teams & team_t)
            badge(dl, bx, by, "T", theme::color(token::team_t));
        if (teams & team_ct)
            badge(dl, bx, by, "CT", theme::color(token::team_ct));
        if (stattrak)
            badge(dl, bx, by, "ST", theme::color(token::stattrak));
        if (hovered && !tooltip.empty())
            ImGui::SetTooltip("%s", tooltip.c_str());
        return clicked;
    }

    template <typename F>
    void grid(int count, F&& draw)
    {
        const ui::metrics& mt = ui::m();
        const float gap = mt.item_card_gap;
        const float avail = std::fmax(1.f, ImGui::GetContentRegionAvail().x - mt.space_xs);
        const int cols = std::max(1, static_cast<int>((avail + gap) / (mt.item_card_min + gap)));
        const float w = std::fmax(1.f, std::floor((avail - gap * static_cast<float>(cols - 1)) / static_cast<float>(cols)));
        const int rows = (count + cols - 1) / cols;
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap, gap));
        ImGuiListClipper clipper;
        clipper.Begin(rows, card_height(w) + gap);
        while (clipper.Step())
            for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r)
                for (int c = 0; c < cols; ++c)
                {
                    const int i = r * cols + c;
                    if (i >= count)
                        break;
                    if (c)
                        ImGui::SameLine(0.f, gap);
                    draw(i, w);
                }
        ImGui::PopStyleVar();
    }

    void empty_state(const char* message)
    {
        const ui::metrics& mt = ui::m();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        svg::Stroke(dl, icon::Box, ImVec2(p.x + (w - mt.icon_empty) * 0.5f, p.y + mt.empty_icon_y), mt.icon_empty, theme::color(token::text_faint), mt.stroke_thin);
        const ImVec2 ts = ui::text_size(font_role::body, message);
        ui::text(dl, font_role::body, ImVec2(p.x + (w - ts.x) * 0.5f, p.y + mt.empty_text_y), theme::color(token::text_dim), message);
        ImGui::Dummy(ImVec2(w, mt.empty_text_y + ui::font_size(font_role::body)));
    }

    void colored_title(const std::string& title, ImU32 col)
    {
        ui::font_scope scope(font_role::title);
        ImGui::PushTextWrapPos(0.f);
        ImGui::PushStyleColor(ImGuiCol_Text, theme::vec(col));
        ImGui::TextUnformatted(title.c_str());
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();
    }

    void refresh_results()
    {
        std::string key = std::to_string(catalog::generation()) + "|" + std::to_string(g_category) + "|" + std::to_string(g_selected) + "|" + g_search;
        if (key == g_results_key)
            return;
        g_results_key = std::move(key);
        g_results.clear();
        const std::string needle = catalog::lower(g_search);
        const auto& items = catalog::list();
        const auto& entries = catalog::entries();
        for (std::size_t i = 0; i < entries.size(); ++i)
        {
            const catalog::entry& e = entries[i];
            const catalog::item& it = items[e.item_index];
            if (needle.empty())
            {
                if (it.category != g_category || (g_selected && it.def != g_selected))
                    continue;
            }
            else if (e.lower.find(needle) == std::string::npos)
                continue;
            g_results.push_back(i);
        }
    }

    void open_draft(const catalog::item& it, int paint)
    {
        g_draft = {};
        g_draft.def = it.def;
        g_draft.paint = paint;
        if (const catalog::paint* p = paint ? catalog::find_paint(paint) : nullptr)
            g_draft.wear = std::fmax(p->wear_min, 0.0001f);
        g_draft.equip = it.teams & (team_t | team_ct);
        g_open_draft = true;
    }

    void sticker_picker()
    {
        const ui::metrics& mt = ui::m();
        ImGui::SetNextWindowSize(ImVec2(mt.sticker_picker_width, mt.sticker_picker_height), ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(mt.space_md, mt.space_md));
        const bool open = ImGui::BeginPopup("##stickers");
        ImGui::PopStyleVar();
        if (!open)
            return;
        ui::search_box("##sticker_search", g_sticker_search, sizeof(g_sticker_search), catalog::tr("Поиск стикера", "Search sticker"), ImGui::GetContentRegionAvail().x);
        const auto& all = catalog::stickers();
        std::string key = std::to_string(catalog::generation()) + "|" + g_sticker_search;
        if (key != g_sticker_key)
        {
            g_sticker_key = std::move(key);
            g_sticker_filter.clear();
            const std::string needle = catalog::lower(g_sticker_search);
            for (std::size_t i = 0; i < all.size(); ++i)
                if (needle.empty() || all[i].lower.find(needle) != std::string::npos)
                    g_sticker_filter.push_back(i);
        }
        const int slot = std::clamp(g_sticker_slot, 0, sticker_slots - 1);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(mt.space_xs, mt.space_xs));
        ImGui::BeginChild("##sticker_list");
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float row = mt.sticker_row;
        const float step = row + mt.space_xs;
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(g_sticker_filter.size()), step);
        while (clipper.Step())
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
            {
                const catalog::sticker& s = all[g_sticker_filter[static_cast<std::size_t>(i)]];
                ImGui::PushID(s.id);
                const ImVec2 p = ImGui::GetCursorScreenPos();
                const float w = std::fmax(1.f, ImGui::GetContentRegionAvail().x);
                if (ImGui::InvisibleButton("##sticker", ImVec2(w, row)))
                {
                    g_draft.stickers[static_cast<std::size_t>(slot)] = s.id;
                    ImGui::CloseCurrentPopup();
                }
                const bool hovered = ImGui::IsItemHovered();
                if (hovered || g_draft.stickers[static_cast<std::size_t>(slot)] == s.id)
                    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + row), hovered ? theme::color(token::control_hover) : theme::accent(0.15f), mt.radius_md);
                const float icon_h = row - mt.space_xs * 2.f;
                draw_icon(dl, s.icon, ImVec2(p.x + mt.space_xs, p.y + mt.space_xs), ImVec2(p.x + mt.space_xs + mt.sticker_icon, p.y + mt.space_xs + icon_h));
                const ImVec2 text_min(p.x + mt.sticker_text, p.y);
                dl->PushClipRect(text_min, ImVec2(std::fmax(text_min.x, p.x + w - mt.space_xs), p.y + row), true);
                ui::text(dl, font_role::body, ImVec2(text_min.x, p.y + (row - ui::font_size(font_role::body)) * 0.5f), catalog::rarity_color(s.rarity), s.name.c_str());
                dl->PopClipRect();
                ImGui::PopID();
            }
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::EndPopup();
    }

    void draft_properties(const catalog::item& it, const catalog::paint* paint)
    {
        const bool weapon = catalog::is_weapon(it.category);
        const bool knife = it.category == catalog::knife;
        if (paint)
        {
            const float lo = std::fmax(paint->wear_min, 0.0001f);
            const float hi = std::fmax(lo, paint->wear_max);
            ui::slider(catalog::tr("Износ##wear", "Wear##wear"), &g_draft.wear, lo, hi, "%.5f");
            g_draft.wear = std::clamp(g_draft.wear, lo, hi);
            ui::input_int(catalog::tr("Паттерн##seed", "Pattern##seed"), &g_draft.seed, 0, max_seed);
        }
        if (!weapon && !knife)
            return;
        bool st = g_draft.stattrak >= 0;
        if (ui::toggle("StatTrak\xE2\x84\xA2##stattrak", &st))
            g_draft.stattrak = st ? 0 : -1;
        if (st)
            ui::input_int(catalog::tr("Убийства##kills", "Kills##kills"), &g_draft.stattrak, 0, max_kills);
        ui::input_text(catalog::tr("Неймтег##tag", "Name tag##tag"), g_draft.tag, sizeof(g_draft.tag), catalog::tr("Без неймтега", "No name tag"));
    }

    void draft_stickers(float inner)
    {
        const ui::metrics& mt = ui::m();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ui::text(dl, font_role::caption, ImGui::GetCursorScreenPos(), theme::color(token::text_dim), catalog::tr("Стикеры  (ПКМ — убрать)", "Stickers  (right-click to clear)"));
        ImGui::Dummy(ImVec2(inner, ui::font_size(font_role::caption) + mt.space_xs));
        const float sw = std::fmax(1.f, std::floor((inner - mt.sticker_gap * static_cast<float>(sticker_slots - 1)) / static_cast<float>(sticker_slots)));
        const float sh = mt.sticker_slot;
        bool open_picker = false;
        for (int i = 0; i < sticker_slots; ++i)
        {
            ImGui::PushID(i);
            if (i)
                ImGui::SameLine(0.f, mt.sticker_gap);
            int& slot = g_draft.stickers[static_cast<std::size_t>(i)];
            const catalog::sticker* s = catalog::find_sticker(slot);
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const ImVec2 q(p.x + sw, p.y + sh);
            const bool pressed = ImGui::InvisibleButton("##slot", ImVec2(sw, sh), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
            const bool hovered = ImGui::IsItemHovered();
            const bool right = pressed && ImGui::GetIO().MouseReleased[ImGuiMouseButton_Right];
            dl->AddRectFilled(p, q, hovered ? theme::color(token::control_hover) : theme::color(token::control), mt.radius_card);
            dl->AddRect(p, q, hovered ? theme::accent(0.5f) : theme::color(token::border), mt.radius_card, 0, mt.hairline);
            if (s)
                draw_icon(dl, s->icon, ImVec2(p.x + mt.space_sm, p.y + mt.space_sm), ImVec2(q.x - mt.space_sm, q.y - mt.space_sm));
            else if (slot)
                ui::text(dl, font_role::caption, ImVec2(p.x + mt.space_sm, p.y + mt.space_sm), theme::color(token::text_faint), std::to_string(slot).c_str());
            else
                svg::Stroke(dl, icon::Plus, ImVec2(p.x + (sw - mt.icon_lg) * 0.5f, p.y + (sh - mt.icon_lg) * 0.5f), mt.icon_lg, theme::color(token::text_faint), mt.stroke);
            if (s && hovered)
                ImGui::SetTooltip("%s", s->name.c_str());
            if (right)
                slot = 0;
            else if (pressed)
            {
                g_sticker_slot = i;
                open_picker = true;
            }
            ImGui::PopID();
        }
        if (open_picker)
            ImGui::OpenPopup("##stickers");
        sticker_picker();
    }

    void draft_popup()
    {
        const ui::metrics& mt = ui::m();
        if (g_open_draft)
        {
            ImGui::OpenPopup("##draft");
            g_open_draft = false;
        }
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(mt.draft_width, 0.f), ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(mt.draft_pad, mt.draft_pad));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, mt.radius_xl);
        const bool open = ImGui::BeginPopupModal("##draft", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);
        ImGui::PopStyleVar(2);
        if (!open)
            return;
        const catalog::item* it = catalog::find(g_draft.def);
        if (!it)
        {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }
        const catalog::paint* paint = g_draft.paint ? catalog::find_paint(g_draft.paint) : nullptr;
        const ImU32 rc = catalog::rarity_color(catalog::rarity(it->def, g_draft.paint));
        const std::string name = catalog::full_name(it->def, g_draft.paint);
        const float inner = mt.draft_width - mt.draft_pad * 2.f;
        ImDrawList* dl = ImGui::GetWindowDrawList();

        const ImVec2 pa = ImGui::GetCursorScreenPos();
        const ImVec2 pb(pa.x + mt.draft_preview_width, pa.y + mt.draft_preview_height);
        dl->AddRectFilled(pa, pb, theme::color(token::surface), mt.radius_lg);
        dl->PushClipRect(ImVec2(pa.x + mt.hairline, pa.y + mt.hairline), ImVec2(pb.x - mt.hairline, pb.y - mt.hairline), true);
        ui::glow(dl, ImVec2((pa.x + pb.x) * 0.5f, (pa.y + pb.y) * 0.5f), mt.draft_glow, rc, 0.45f);
        dl->PopClipRect();
        dl->AddRect(pa, pb, theme::color(token::border), mt.radius_lg, 0, mt.hairline);
        draw_icon(dl, catalog::icon(it->def, g_draft.paint, g_draft.wear), ImVec2(pa.x + mt.space_lg, pa.y + mt.space_lg), ImVec2(pb.x - mt.space_lg, pb.y - mt.space_lg));
        ImGui::Dummy(ImVec2(mt.draft_preview_width, mt.draft_preview_height));
        ImGui::SameLine(0.f, mt.draft_gap);

        ImGui::BeginGroup();
        colored_title(name, rc);
        const std::string meta = paint ? std::string(catalog::category_name(it->category)) + "  \xC2\xB7  " + catalog::wear_name(g_draft.wear) : std::string(catalog::category_name(it->category));
        ui::text(dl, font_role::compact, ImGui::GetCursorScreenPos(), theme::color(token::text_dim), meta.c_str());
        ImGui::Dummy(ImVec2(0.f, ui::font_size(font_role::compact) + mt.space_sm));
        draft_properties(*it, paint);
        ImGui::EndGroup();

        if (catalog::is_weapon(it->category))
        {
            ImGui::Dummy(ImVec2(0.f, mt.space_xs));
            draft_stickers(inner);
        }

        ImGui::Dummy(ImVec2(0.f, mt.space_xs));
        if (ui::begin_columns("##draft_teams"))
        {
            ui::next_column();
            bool t = (g_draft.equip & team_t) != 0;
            if ((it->teams & team_t) && ui::toggle(catalog::tr("Экипировать за T##t", "Equip for T##t"), &t))
                g_draft.equip = (g_draft.equip & ~team_t) | (t ? team_t : 0);
            ui::next_column();
            bool ct = (g_draft.equip & team_ct) != 0;
            if ((it->teams & team_ct) && ui::toggle(catalog::tr("Экипировать за CT##ct", "Equip for CT##ct"), &ct))
                g_draft.equip = (g_draft.equip & ~team_ct) | (ct ? team_ct : 0);
            ui::end_columns();
        }

        ImGui::Dummy(ImVec2(0.f, mt.space_xs));
        const float bw = mt.draft_button_width;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::fmax(0.f, inner - bw * 2.f - mt.space_md));
        if (ui::button(catalog::tr("Отмена##cancel", "Cancel##cancel"), ImVec2(bw, mt.draft_button_height)) || (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::IsPopupOpen("##stickers")))
            ImGui::CloseCurrentPopup();
        ImGui::SameLine(0.f, mt.space_md);
        if (ui::button(catalog::tr("Добавить##add", "Add to inventory##add"), ImVec2(bw, mt.draft_button_height), icon::Plus, ui::button_kind::primary))
        {
            add_draft(g_draft);
            logs::Add(logs::Success, "%s added", name.c_str());
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    bool catalog_ready()
    {
        if (catalog::current_source() != catalog::source::none)
            return true;
        if (items::state.load() < 0)
            ui::note((std::string(catalog::tr("Ошибка загрузки: ", "Load error: ")) + items::error).c_str());
        else
            ui::note(catalog::tr("Загрузка предметов из CS2...", "Loading items from CS2..."));
        return false;
    }

    void weapon_filter()
    {
        const ui::metrics& mt = ui::m();
        const catalog::item* current = g_selected ? catalog::find(g_selected) : nullptr;
        const char* all_label = catalog::tr("Всё оружие", "All weapons");
        if (ui::begin_combo("##weapon", current ? current->name.c_str() : all_label, mt.weapon_combo))
        {
            if (ui::combo_item(all_label, !g_selected))
                g_selected = 0;
            for (const catalog::item& it : catalog::list())
            {
                if (it.category != g_category)
                    continue;
                ImGui::PushID(it.def);
                if (ui::combo_item(it.name.c_str(), g_selected == it.def))
                {
                    g_selected = it.def;
                    g_search[0] = 0;
                }
                ImGui::PopID();
            }
            ui::end_combo();
        }
    }

    void add_tab()
    {
        const ui::metrics& mt = ui::m();
        if (!catalog_ready())
            return;
        for (int c = 0; c < catalog::category_count; ++c)
        {
            if (c)
                ImGui::SameLine(0.f, mt.inner_gap_x);
            ImGui::PushID(c);
            if (ui::chip(catalog::category_name(c), g_category == c && !g_search[0]))
            {
                g_category = c;
                g_selected = 0;
                g_search[0] = 0;
            }
            ImGui::PopID();
        }
        ImGui::Dummy(ImVec2(0.f, mt.space_xxs));
        if (g_category != catalog::agent)
        {
            weapon_filter();
            ImGui::SameLine(0.f, mt.space_sm);
        }
        ui::search_box("##search", g_search, sizeof(g_search), catalog::tr("Поиск по всем предметам", "Search all items"), ImGui::GetContentRegionAvail().x);
        refresh_results();
        ImGui::Dummy(ImVec2(0.f, mt.space_xxs));
        ImGui::BeginChild("##grid");
        const auto& items = catalog::list();
        const auto& entries = catalog::entries();
        if (g_results.empty())
            empty_state(catalog::tr("Ничего не найдено", "Nothing found"));
        grid(static_cast<int>(g_results.size()), [&](int i, float w) {
            const catalog::entry& e = entries[g_results[static_cast<std::size_t>(i)]];
            const catalog::item& it = items[e.item_index];
            const char* sub = it.category == catalog::agent ? (it.teams == team_t ? "T" : it.teams == team_ct ? "CT" : "") : e.paint ? it.name.c_str() : catalog::category_name(it.category);
            if (item_card(i, w, catalog::icon(it.def, e.paint, 0.f), e.title, sub, e.rarity, 0, false, e.full_name))
                open_draft(it, e.paint);
        });
        ImGui::EndChild();
        draft_popup();
    }

    void inventory_popup(const std::vector<inventory_entry>& all)
    {
        const ui::metrics& mt = ui::m();
        if (g_open_inventory)
        {
            ImGui::OpenPopup("##item");
            g_open_inventory = false;
        }
        ImGui::SetNextWindowSize(ImVec2(mt.inventory_popup, 0.f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(mt.popover_pad_x, mt.popover_pad_y));
        const bool open = ImGui::BeginPopup("##item");
        ImGui::PopStyleVar();
        if (!open)
            return;
        const auto e = std::find_if(all.begin(), all.end(), [](const inventory_entry& x) { return x.uid == g_inventory_uid; });
        if (e == all.end())
        {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }
        const catalog::item* it = catalog::find(e->def_index);
        const int paint = e->skin.paint_kit_id;
        colored_title(catalog::full_name(e->def_index, paint), catalog::rarity_color(catalog::rarity(e->def_index, paint)));
        if (paint)
            ui::value(catalog::wear_name(e->skin.wear), "%.5f  #%d", e->skin.wear, e->skin.seed);
        if (e->skin.stattrak >= 0)
            ui::value("StatTrak\xE2\x84\xA2", "%d", e->skin.stattrak);
        if (!e->skin.nametag.empty())
            ui::value(catalog::tr("Неймтег", "Name tag"), "%s", e->skin.nametag.c_str());
        const int teams = it ? it->teams : team_t | team_ct;
        bool t = e->equipped_t, ct = e->equipped_ct;
        if ((teams & team_t) && ui::toggle(catalog::tr("Экипировано за T##t", "Equipped for T##t"), &t))
            equip_uid(e->uid, team_t, t);
        if ((teams & team_ct) && ui::toggle(catalog::tr("Экипировано за CT##ct", "Equipped for CT##ct"), &ct))
            equip_uid(e->uid, team_ct, ct);
        ImGui::Dummy(ImVec2(0.f, mt.space_xxs));
        if (ui::button(catalog::tr("Удалить##delete", "Delete##delete"), ImVec2(0.f, mt.button_height), icon::Trash, ui::button_kind::destructive))
        {
            remove_uid(e->uid);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    bool match_filter(std::int16_t def)
    {
        switch (g_inventory_filter)
        {
        case 1:
            return kind_of(def) == item_kind::gun;
        case 2:
            return kind_of(def) == item_kind::knife;
        case 3:
            return kind_of(def) == item_kind::glove;
        case 4:
            return kind_of(def) == item_kind::agent;
        default:
            return true;
        }
    }

    void clear_popup()
    {
        const ui::metrics& mt = ui::m();
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(mt.popover_pad_x, mt.popover_pad_y));
        const bool open = ImGui::BeginPopup("##clear");
        ImGui::PopStyleVar();
        if (!open)
            return;
        ImGui::TextUnformatted(catalog::tr("Удалить все добавленные предметы?", "Delete all added items?"));
        if (ui::button(catalog::tr("Удалить##confirm", "Delete##confirm"), ImVec2(mt.button_wide, mt.button_small), nullptr, ui::button_kind::destructive))
        {
            remove_all();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine(0.f, mt.space_sm);
        if (ui::button(catalog::tr("Отмена##cancel", "Cancel##cancel"), ImVec2(mt.button_wide, mt.button_small)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    void inventory_tab()
    {
        const ui::metrics& mt = ui::m();
        const std::vector<inventory_entry> all = inventory_snapshot();
        const char* const filters[] = { catalog::tr("Все", "All"), catalog::tr("Оружие", "Weapons"), catalog::tr("Ножи", "Knives"), catalog::tr("Перчатки", "Gloves"), catalog::tr("Агенты", "Agents") };
        for (int i = 0; i < static_cast<int>(std::size(filters)); ++i)
        {
            if (i)
                ImGui::SameLine(0.f, mt.inner_gap_x);
            ImGui::PushID(i);
            if (ui::chip(filters[i], g_inventory_filter == i))
                g_inventory_filter = i;
            ImGui::PopID();
        }
        ImGui::SameLine(0.f, mt.space_md);
        const float clear_w = mt.button_wide;
        ui::search_box("##inventory_search", g_inventory_search, sizeof(g_inventory_search), catalog::tr("Поиск", "Search"), ImGui::GetContentRegionAvail().x - clear_w - mt.space_sm);
        ImGui::SameLine(0.f, mt.space_sm);
        if (ui::button(catalog::tr("Удалить всё##clear_all", "Clear all##clear_all"), ImVec2(clear_w, mt.control), icon::Trash, ui::button_kind::destructive))
            ImGui::OpenPopup("##clear");
        clear_popup();

        std::vector<const inventory_entry*> shown;
        const std::string needle = catalog::lower(g_inventory_search);
        for (const inventory_entry& e : all)
        {
            if (!match_filter(e.def_index))
                continue;
            if (!needle.empty() && catalog::lower(catalog::full_name(e.def_index, e.skin.paint_kit_id).c_str()).find(needle) == std::string::npos)
                continue;
            shown.push_back(&e);
        }
        ImGui::Dummy(ImVec2(0.f, mt.space_xxs));
        ImGui::BeginChild("##inventory");
        if (shown.empty())
            empty_state(all.empty() ? catalog::tr("Инвентарь пуст — добавь предметы во вкладке Add", "Inventory is empty. Add items in the Add tab") : catalog::tr("Ничего не найдено", "Nothing found"));
        grid(static_cast<int>(shown.size()), [&](int i, float w) {
            const inventory_entry& e = *shown[static_cast<std::size_t>(i)];
            const int paint = e.skin.paint_kit_id;
            const catalog::item* it = catalog::find(e.def_index);
            const catalog::paint* p = paint ? catalog::find_paint(paint) : nullptr;
            const std::string title = p ? p->name : it ? it->name : "#" + std::to_string(e.def_index);
            const char* sub = p && it ? it->name.c_str() : it ? catalog::category_name(it->category) : "";
            const int equipped = (e.equipped_t ? team_t : 0) | (e.equipped_ct ? team_ct : 0);
            if (item_card(e.uid, w, catalog::icon(e.def_index, paint, e.skin.wear), title, sub, catalog::rarity(e.def_index, paint), equipped, e.skin.stattrak >= 0, catalog::full_name(e.def_index, paint)))
            {
                g_inventory_uid = e.uid;
                g_open_inventory = true;
            }
        });
        ImGui::EndChild();
        inventory_popup(all);
    }

    void options_tab()
    {
        auto& changer = settings::g_changer;
        if (!ui::begin_columns("##skin_options"))
            return;
        ui::next_column();
        ui::begin_card("Skin changer", icon::Box);
        ui::toggle("Apply skins in match", &changer.enabled);
        static const char* const languages[] = { "Russian", "English" };
        ui::combo("Item names", &changer.language, languages, static_cast<int>(std::size(languages)));
        ui::end_card();

        ui::begin_card("Knife##options", icon::Box);
        ui::toggle("Native animations", &changer.knife_animations);
        ui::end_card();

        ui::next_column();
        ui::begin_card("Catalog", icon::List);
        ui::value("Source", "%s", catalog::status_text());
        ui::value("Items", "%d", static_cast<int>(catalog::list().size()));
        ui::value("Skins", "%d", static_cast<int>(catalog::entries().size()));
        ui::value("Stickers", "%d", static_cast<int>(catalog::stickers().size()));
        std::size_t count = 0;
        std::int16_t agent_t = 0, agent_ct = 0;
        {
            std::lock_guard lock(settings::g_changer_mutex);
            count = changer.inventory.size();
            agent_t = changer.agents.t_def;
            agent_ct = changer.agents.ct_def;
        }
        ui::value("Inventory", "%d", static_cast<int>(count));
        ui::value("Agent T", "%d", static_cast<int>(agent_t));
        ui::value("Agent CT", "%d", static_cast<int>(agent_ct));
        ui::note("Inventory is saved when the menu closes.");
        ui::end_card();
        ui::end_columns();
    }
}

void menu::pages::skins(int tab)
{
    switch (tab)
    {
    case 0:
        inventory_tab();
        break;
    case 1:
        add_tab();
        break;
    default:
        options_tab();
        break;
    }
}
