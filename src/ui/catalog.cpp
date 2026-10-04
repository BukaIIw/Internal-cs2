#include "catalog.h"
#include "theme.h"
#include "../items.h"
#include "../core/settings.h"
#include "../features/changer/changer.h"
#include <algorithm>
#include <unordered_map>

namespace
{
    namespace catalog = ui::catalog;
    using econ = features::changer::econ_item_system;

    struct state
    {
        catalog::source origin = catalog::source::none;
        int language = -1;
        int items_state = 0;
        bool built = false;
        std::uint32_t generation = 0;
        std::vector<catalog::item> list;
        std::vector<catalog::entry> entries;
        std::vector<catalog::sticker> stickers;
        std::unordered_map<std::int16_t, std::size_t> by_def;
        std::unordered_map<int, catalog::paint> paints;
        std::unordered_map<int, std::size_t> sticker_by_id;
    };

    state g_state;

    constexpr std::int16_t pistols[] = { 1, 2, 3, 4, 30, 32, 36, 61, 63, 64 };
    constexpr std::int16_t smgs[] = { 17, 19, 23, 24, 26, 33, 34 };
    constexpr std::int16_t heavies[] = { 14, 25, 27, 28, 29, 35 };

    int language()
    {
        return settings::g_changer.language ? 1 : 0;
    }

    bool items_ready()
    {
        return items::state.load() == 1;
    }

    template <std::size_t N>
    bool contains(const std::int16_t (&list)[N], std::int16_t def)
    {
        return std::find(std::begin(list), std::end(list), def) != std::end(list);
    }

    int gun_category(std::int16_t def)
    {
        if (items_ready())
            if (const items::Item* it = items::Find(def); it && it->category <= items::Heavy)
                return static_cast<int>(it->category);
        if (contains(pistols, def))
            return catalog::pistol;
        if (contains(smgs, def))
            return catalog::smg;
        if (contains(heavies, def))
            return catalog::heavy;
        return catalog::rifle;
    }

    int econ_teams(const econ::item_def& def)
    {
        switch (def.team())
        {
        case 2:
            return 1;
        case 3:
            return 2;
        default:
            return 3;
        }
    }

    std::string item_name(std::int16_t def, const std::string& fallback)
    {
        if (items_ready())
            if (const items::Item* it = items::Find(def))
                return items::Name(it->name);
        return fallback;
    }

    std::string paint_name(int id, const std::string& fallback)
    {
        if (items_ready())
            if (const items::Paint* p = items::FindPaint(id))
                return items::Name(p->name);
        return fallback;
    }

    void add_econ(int category, const std::vector<const econ::item_def*>& defs)
    {
        const econ& system = features::changer::g_econ_item_system;
        for (const econ::item_def* def : defs)
        {
            if (!def || g_state.by_def.count(def->def_index))
                continue;
            catalog::item it;
            it.def = def->def_index;
            it.category = category < 0 ? gun_category(def->def_index) : category;
            it.teams = econ_teams(*def);
            it.name = item_name(def->def_index, def->localized_name);
            if (category != catalog::agent)
                it.paints = system.skins_for(def->def_index);
            g_state.by_def[it.def] = g_state.list.size();
            g_state.list.push_back(std::move(it));
        }
    }

    void build_econ()
    {
        const econ& system = features::changer::g_econ_item_system;
        for (const econ::paint_kit& pk : system.paint_kits())
            g_state.paints[pk.id] = { pk.id, paint_name(pk.id, pk.localized_name), pk.wear_min, pk.wear_max };
        add_econ(-1, system.guns());
        add_econ(catalog::knife, system.knives());
        add_econ(catalog::glove, system.gloves());
        add_econ(catalog::agent, system.agents());
    }

    void build_items()
    {
        for (const items::Item& source : items::List())
        {
            catalog::item it;
            it.def = static_cast<std::int16_t>(source.def);
            it.category = std::clamp(static_cast<int>(source.category), 0, static_cast<int>(catalog::category_count) - 1);
            it.teams = source.teams;
            it.name = items::Name(source.name);
            for (const items::Skin& skin : source.skins)
            {
                it.paints.push_back(skin.paint);
                if (!g_state.paints.count(skin.paint))
                    if (const items::Paint* p = items::FindPaint(skin.paint))
                        g_state.paints[skin.paint] = { p->id, items::Name(p->name), p->wearMin, p->wearMax };
            }
            if (g_state.by_def.count(it.def))
                continue;
            g_state.by_def[it.def] = g_state.list.size();
            g_state.list.push_back(std::move(it));
        }
    }

    void build_entries()
    {
        for (std::size_t i = 0; i < g_state.list.size(); ++i)
        {
            const catalog::item& it = g_state.list[i];
            const auto add = [&](int paint_id) {
                catalog::entry e;
                e.item_index = i;
                e.paint = paint_id;
                e.rarity = catalog::rarity(it.def, paint_id);
                const catalog::paint* p = paint_id ? catalog::find_paint(paint_id) : nullptr;
                e.title = p ? p->name : it.name;
                e.full_name = catalog::full_name(it.def, paint_id);
                e.lower = catalog::lower(e.full_name.c_str());
                g_state.entries.push_back(std::move(e));
            };
            if (it.paints.empty())
                add(0);
            else
                for (int paint_id : it.paints)
                    add(paint_id);
        }
    }

    void build_stickers()
    {
        if (!items_ready())
            return;
        for (const items::Sticker& s : items::Stickers())
        {
            catalog::sticker st;
            st.id = s.id;
            st.rarity = s.rarity;
            st.name = items::Name(s.name);
            st.icon = items::StickerIcon(s);
            st.lower = catalog::lower(st.name.c_str());
            g_state.sticker_by_id.emplace(st.id, g_state.stickers.size());
            g_state.stickers.push_back(std::move(st));
        }
    }
}

void ui::catalog::update()
{
    items::language = language();
    const source origin = features::changer::g_econ_item_system.ready() ? source::econ : items_ready() ? source::items : source::none;
    const int lang = language();
    const int loaded = items::state.load();
    if (g_state.built && origin == g_state.origin && lang == g_state.language && loaded == g_state.items_state)
        return;
    g_state.origin = origin;
    g_state.language = lang;
    g_state.items_state = loaded;
    g_state.built = true;
    ++g_state.generation;
    g_state.list.clear();
    g_state.entries.clear();
    g_state.stickers.clear();
    g_state.by_def.clear();
    g_state.paints.clear();
    g_state.sticker_by_id.clear();
    if (origin == source::econ)
        build_econ();
    else if (origin == source::items)
        build_items();
    build_entries();
    build_stickers();
}

ui::catalog::source ui::catalog::current_source()
{
    return g_state.origin;
}

std::uint32_t ui::catalog::generation()
{
    return g_state.generation;
}

const char* ui::catalog::status_text()
{
    switch (g_state.origin)
    {
    case source::econ:
        return tr("игровая схема", "game schema");
    case source::items:
        return tr("файлы игры", "game files");
    default:
        return items::state.load() < 0 ? tr("ошибка", "error") : tr("загрузка", "loading");
    }
}

const std::vector<ui::catalog::item>& ui::catalog::list()
{
    return g_state.list;
}

const std::vector<ui::catalog::entry>& ui::catalog::entries()
{
    return g_state.entries;
}

const std::vector<ui::catalog::sticker>& ui::catalog::stickers()
{
    return g_state.stickers;
}

const ui::catalog::item* ui::catalog::find(std::int16_t def)
{
    const auto it = g_state.by_def.find(def);
    return it == g_state.by_def.end() ? nullptr : &g_state.list[it->second];
}

const ui::catalog::paint* ui::catalog::find_paint(int id)
{
    const auto it = g_state.paints.find(id);
    return it == g_state.paints.end() ? nullptr : &it->second;
}

const ui::catalog::sticker* ui::catalog::find_sticker(int id)
{
    if (!id)
        return nullptr;
    const auto it = g_state.sticker_by_id.find(id);
    return it == g_state.sticker_by_id.end() ? nullptr : &g_state.stickers[it->second];
}

int ui::catalog::rarity(std::int16_t def, int paint_id)
{
    if (g_state.origin == source::econ)
        return features::changer::g_econ_item_system.combined_rarity(def, paint_id);
    if (g_state.origin == source::items)
        if (const items::Item* it = items::Find(def))
            return items::Rarity(*it, paint_id);
    return 0;
}

std::string ui::catalog::full_name(std::int16_t def, int paint_id)
{
    const item* it = find(def);
    if (!it)
        return "#" + std::to_string(def);
    std::string result;
    if (it->category == knife || it->category == glove)
        result = "\xE2\x98\x85 ";
    result += it->name;
    if (const paint* p = paint_id ? find_paint(paint_id) : nullptr)
        result += " | " + p->name;
    return result;
}

std::string ui::catalog::icon(std::int16_t def, int paint_id, float wear)
{
    if (g_state.origin == source::econ)
        return features::changer::g_econ_item_system.image_path(def, paint_id, wear);
    if (g_state.origin == source::items)
        if (const items::Item* it = items::Find(def))
            return items::Icon(*it, paint_id, wear);
    return {};
}

ImU32 ui::catalog::rarity_color(int value)
{
    return theme::faded(static_cast<ImU32>(items::RarityColor(value)));
}

const char* ui::catalog::category_name(int value)
{
    static const char* const ru[] = { "Пистолеты", "Пистолеты-пулемёты", "Винтовки", "Тяжёлое", "Ножи", "Перчатки", "Агенты" };
    static const char* const en[] = { "Pistols", "SMGs", "Rifles", "Heavy", "Knives", "Gloves", "Agents" };
    const int i = std::clamp(value, 0, static_cast<int>(category_count) - 1);
    return language() ? en[i] : ru[i];
}

const char* ui::catalog::wear_name(float wear)
{
    static const char* const ru[] = { "Прямо с завода", "Немного поношенное", "После полевых испытаний", "Поношенное", "Закалённое в боях" };
    static const char* const en[] = { "Factory New", "Minimal Wear", "Field-Tested", "Well-Worn", "Battle-Scarred" };
    const int i = wear < 0.07f ? 0 : wear < 0.15f ? 1 : wear < 0.38f ? 2 : wear < 0.45f ? 3 : 4;
    return language() ? en[i] : ru[i];
}

const char* ui::catalog::tr(const char* ru, const char* en)
{
    return language() ? en : ru;
}

std::string ui::catalog::lower(const char* text)
{
    std::string result;
    if (!text)
        return result;
    for (auto p = reinterpret_cast<const unsigned char*>(text); *p; ++p)
    {
        const unsigned char c = *p;
        if (c >= 'A' && c <= 'Z')
            result += static_cast<char>(c + 32);
        else if (c == 0xD0 && p[1] >= 0x90 && p[1] <= 0x9F)
        {
            result += '\xD0';
            result += static_cast<char>(*++p + 0x20);
        }
        else if (c == 0xD0 && p[1] >= 0xA0 && p[1] <= 0xAF)
        {
            result += '\xD1';
            result += static_cast<char>(*++p - 0x20);
        }
        else if (c == 0xD0 && p[1] == 0x81)
        {
            result += "\xD1\x91";
            ++p;
        }
        else
            result += static_cast<char>(c);
    }
    return result;
}

bool ui::catalog::is_weapon(int value)
{
    return value >= pistol && value <= heavy;
}
