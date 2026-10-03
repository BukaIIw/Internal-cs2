#include "items.h"
#include "kv.h"
#include "vpk.h"
#include <Windows.h>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <charconv>

namespace
{
    std::vector<items::Item> list;
    std::unordered_map<int, size_t> byDef;
    std::unordered_map<int, std::string> codes;
    std::unordered_map<int, items::Paint> paints;
    std::vector<items::Sticker> stickers;
    std::unordered_map<int, size_t> stickerById;
    const std::string none;

    std::string Lower(std::string_view s)
    {
        std::string r(s);
        for (char& c : r)
            if (c >= 'A' && c <= 'Z')
                c += 32;
        return r;
    }

    int ToInt(std::string_view s)
    {
        int v = 0;
        std::from_chars(s.data(), s.data() + s.size(), v);
        return v;
    }

    float ToFloat(std::string_view s, float def)
    {
        float v = def;
        if (!s.empty())
            std::from_chars(s.data(), s.data() + s.size(), v);
        return v;
    }

    std::string Token(std::string_view s)
    {
        if (!s.empty() && s[0] == '#')
            s.remove_prefix(1);
        return std::string(s);
    }

    int RarityValue(std::string_view s)
    {
        static const char* names[] = { "default", "common", "uncommon", "rare", "mythical", "legendary", "ancient", "immortal" };
        for (int i = 0; i < 8; ++i)
            if (kv::Equal(s, names[i]))
                return i;
        return 0;
    }

    int SlotIndex(std::string_view s)
    {
        struct Group
        {
            std::string_view name;
            int base;
        };
        static const Group groups[] = { { "secondary", 2 }, { "smg", 8 }, { "rifle", 14 }, { "heavy", 20 } };
        for (auto& g : groups)
            if (s.size() == g.name.size() + 1 && s.substr(0, g.name.size()) == g.name && s.back() >= '0' && s.back() <= '5')
                return g.base + (s.back() - '0');
        return -1;
    }

    struct Prefabs
    {
        std::unordered_map<std::string_view, const kv::Node*> map;

        template <typename F>
        bool Chain(const kv::Node& n, F&& f) const
        {
            std::string_view chain = n.Get("prefab");
            while (!chain.empty())
            {
                const size_t sp = chain.find(' ');
                std::string_view name = chain.substr(0, sp);
                chain = sp == std::string_view::npos ? std::string_view{} : chain.substr(sp + 1);
                if (name.empty())
                    continue;
                auto it = map.find(name);
                if (it != map.end() && f(name, *it->second))
                    return true;
            }
            return false;
        }

        const kv::Node* Lookup(const kv::Node& n, std::string_view key, int depth = 0) const
        {
            if (auto v = n.Find(key))
                return v;
            const kv::Node* found = nullptr;
            if (depth < 16)
                Chain(n, [&](std::string_view, const kv::Node& p) { return (found = Lookup(p, key, depth + 1)) != nullptr; });
            return found;
        }

        std::string_view Value(const kv::Node& n, std::string_view key) const
        {
            auto v = Lookup(n, key);
            return v ? v->value : std::string_view{};
        }

        bool Has(const kv::Node& n, std::string_view prefab, int depth = 0) const
        {
            if (depth > 16)
                return false;
            return Chain(n, [&](std::string_view name, const kv::Node& p) { return name == prefab || Has(p, prefab, depth + 1); });
        }
    };

    std::unordered_map<std::string, std::string> Localize(const char* file, const std::unordered_set<std::string>& needed)
    {
        std::unordered_map<std::string, std::string> out;
        std::vector<uint8_t> raw;
        if (!vpk::Read(file, raw))
            return out;
        std::string text(raw.begin(), raw.end());
        raw.clear();
        kv::Node root;
        kv::Parse(text, root);
        const kv::Node* lang = root.Find("lang");
        const kv::Node* tokens = lang ? lang->Find("Tokens") : nullptr;
        if (!tokens)
            return out;
        for (auto& t : tokens->children)
        {
            if (t.key.empty() || t.key[0] == '[')
                continue;
            std::string key = Lower(t.key);
            if (needed.count(key))
                out.emplace(std::move(key), std::string(t.value));
        }
        return out;
    }

    bool Build()
    {
        std::vector<uint8_t> raw;
        if (!vpk::Read("scripts/items/items_game.txt", raw))
        {
            items::error = "items_game.txt";
            return false;
        }
        std::string text(raw.begin(), raw.end());
        raw.clear();
        raw.shrink_to_fit();
        kv::Node root;
        kv::Parse(text, root);
        const kv::Node* game = root.Find("items_game");
        if (!game)
        {
            items::error = "items_game";
            return false;
        }

        Prefabs prefabs;
        game->Each("prefabs", [&](const kv::Node& b) {
            for (auto& c : b.children)
                prefabs.map[c.key] = &c;
        });

        std::unordered_map<std::string, int> paintByCode;
        game->Each("paint_kits", [&](const kv::Node& b) {
            for (auto& c : b.children)
            {
                const int id = ToInt(c.key);
                if (id <= 0 || id == 9001)
                    continue;
                items::Paint p;
                p.id = id;
                p.code = Lower(c.Get("name"));
                p.name[0] = Token(c.Get("description_tag"));
                p.wearMin = ToFloat(c.Get("wear_remap_min"), 0.06f);
                p.wearMax = ToFloat(c.Get("wear_remap_max"), 0.8f);
                p.legacy = c.Get("use_legacy_model") == "1";
                paintByCode[p.code] = id;
                paints[id] = std::move(p);
            }
        });
        game->Each("paint_kits_rarity", [&](const kv::Node& b) {
            for (auto& c : b.children)
            {
                auto it = paintByCode.find(Lower(c.key));
                if (it != paintByCode.end())
                    paints[it->second].rarity = RarityValue(c.value);
            }
        });

        std::unordered_map<std::string, size_t> byCode;
        game->Each("items", [&](const kv::Node& b) {
            for (auto& c : b.children)
            {
                const int def = ToInt(c.key);
                if (def <= 0)
                    continue;
                items::Item it;
                it.def = def;
                it.code = Lower(c.Get("name"));
                codes[def] = it.code;
                if (prefabs.Has(c, "melee_unusual"))
                    it.category = items::Knife;
                else if (prefabs.Has(c, "hands_paintable"))
                {
                    it.category = items::Glove;
                    it.slot = 41;
                }
                else if (prefabs.Has(c, "customplayertradable"))
                {
                    it.category = items::Agent;
                    it.slot = 38;
                }
                else
                {
                    const int slot = SlotIndex(prefabs.Value(c, "flexible_loadout_slot"));
                    const std::string_view type = prefabs.Value(c, "item_type_name");
                    if (slot < 0)
                        continue;
                    if (kv::Equal(type, "#CSGO_Type_Pistol"))
                        it.category = items::Pistol;
                    else if (kv::Equal(type, "#CSGO_Type_SMG"))
                        it.category = items::Smg;
                    else if (kv::Equal(type, "#CSGO_Type_Rifle") || kv::Equal(type, "#CSGO_Type_SniperRifle"))
                        it.category = items::Rifle;
                    else if (kv::Equal(type, "#CSGO_Type_Shotgun") || kv::Equal(type, "#CSGO_Type_Machinegun"))
                        it.category = items::Heavy;
                    else
                        continue;
                    it.slot = slot;
                }
                it.name[0] = Token(prefabs.Value(c, "item_name"));
                it.image = Lower(prefabs.Value(c, "image_inventory"));
                it.model = std::string(prefabs.Value(c, "model_player"));
                it.rarity = RarityValue(prefabs.Value(c, "item_rarity"));
                if (auto used = prefabs.Lookup(c, "used_by_classes"))
                {
                    int t = 0;
                    if (used->Find("terrorists"))
                        t |= 1;
                    if (used->Find("counter-terrorists"))
                        t |= 2;
                    if (t)
                        it.teams = t;
                }
                if (it.name[0].empty() || (it.image.empty() && it.category != items::Glove))
                    continue;
                if ((it.category == items::Knife || it.category == items::Agent) && it.model.empty())
                    continue;
                byCode[it.code] = list.size();
                list.push_back(std::move(it));
            }
        });

        const std::string suffix = "_light_png.vtex_c";
        for (auto& f : vpk::List("panorama/images/econ/default_generated"))
        {
            if (f.size() <= suffix.size() || f.compare(f.size() - suffix.size(), suffix.size(), suffix))
                continue;
            std::string_view stem(f.data(), f.size() - suffix.size());
            for (size_t i = stem.rfind('_'); i != std::string_view::npos && i > 0; i = stem.rfind('_', i - 1))
            {
                auto it = byCode.find(std::string(stem.substr(0, i)));
                if (it == byCode.end())
                    continue;
                auto pk = paintByCode.find(std::string(stem.substr(i + 1)));
                if (pk == paintByCode.end())
                    continue;
                list[it->second].skins.push_back({ pk->second, 0 });
                break;
            }
        }

        game->Each("sticker_kits", [&](const kv::Node& b) {
            for (auto& c : b.children)
            {
                const int id = ToInt(c.key);
                std::string_view mat = c.Get("sticker_material");
                if (id <= 0 || mat.empty())
                    continue;
                items::Sticker s;
                s.id = id;
                s.name[0] = Token(c.Get("item_name"));
                s.image = "econ/stickers/" + Lower(mat);
                s.rarity = RarityValue(c.Get("item_rarity"));
                auto found = stickerById.find(id);
                if (found != stickerById.end())
                    stickers[found->second] = std::move(s);
                else
                {
                    stickerById[id] = stickers.size();
                    stickers.push_back(std::move(s));
                }
            }
        });

        root = kv::Node{};
        text.clear();
        text.shrink_to_fit();

        std::unordered_set<std::string> needed;
        for (auto& it : list)
            needed.insert(Lower(it.name[0]));
        for (auto& [id, p] : paints)
            needed.insert(Lower(p.name[0]));
        for (auto& s : stickers)
            needed.insert(Lower(s.name[0]));
        auto en = Localize("resource/csgo_english.txt", needed);
        auto ru = Localize("resource/csgo_russian.txt", needed);
        auto resolve = [&](std::string (&n)[2]) {
            const std::string key = Lower(n[0]);
            auto e = en.find(key);
            auto r = ru.find(key);
            n[1] = e != en.end() ? e->second : n[0];
            n[0] = r != ru.end() ? r->second : n[1];
        };
        for (auto& it : list)
            resolve(it.name);
        for (auto& [id, p] : paints)
            resolve(p.name);
        for (auto& s : stickers)
            resolve(s.name);

        list.erase(std::remove_if(list.begin(), list.end(), [](const items::Item& it) {
            return it.skins.empty() && it.category != items::Knife && it.category != items::Agent;
        }), list.end());

        for (auto& it : list)
        {
            std::sort(it.skins.begin(), it.skins.end(), [](const items::Skin& a, const items::Skin& b) { return a.paint < b.paint; });
            it.skins.erase(std::unique(it.skins.begin(), it.skins.end(), [](const items::Skin& a, const items::Skin& b) { return a.paint == b.paint; }), it.skins.end());
            if (it.category == items::Knife)
                it.skins.insert(it.skins.begin(), { 0, 0 });
            for (auto& s : it.skins)
                s.rarity = items::Rarity(it, s.paint);
            std::stable_sort(it.skins.begin(), it.skins.end(), [](const items::Skin& a, const items::Skin& b) {
                if (a.paint == 0 || b.paint == 0)
                    return a.paint == 0 && b.paint != 0;
                if (a.rarity != b.rarity)
                    return a.rarity > b.rarity;
                return paints[a.paint].name[1] < paints[b.paint].name[1];
            });
        }
        std::sort(list.begin(), list.end(), [](const items::Item& a, const items::Item& b) {
            if (a.category != b.category)
                return a.category < b.category;
            if (a.category == items::Agent && a.teams != b.teams)
                return a.teams < b.teams;
            return a.name[1] < b.name[1];
        });
        for (size_t i = 0; i < list.size(); ++i)
            byDef[list[i].def] = i;
        std::sort(stickers.begin(), stickers.end(), [](const items::Sticker& a, const items::Sticker& b) { return a.id > b.id; });
        stickerById.clear();
        for (size_t i = 0; i < stickers.size(); ++i)
            stickerById[stickers[i].id] = i;
        return true;
    }
}

void items::Load()
{
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(GetModuleHandleW(L"client.dll"), path, MAX_PATH);
    std::wstring dir = path;
    for (int i = 0; i < 3; ++i)
        dir = dir.substr(0, dir.find_last_of(L"\\/"));
    LoadFrom(dir + L"\\pak01_dir.vpk");
}

void items::LoadFrom(const std::wstring& pak)
{
    if (!vpk::Open(pak))
    {
        error = "pak01_dir.vpk";
        state = -1;
        return;
    }
    state = Build() ? 1 : -1;
}

const std::vector<items::Item>& items::List()
{
    return list;
}

const items::Item* items::Find(int def)
{
    auto it = byDef.find(def);
    return it == byDef.end() ? nullptr : &list[it->second];
}

const std::string& items::Code(int def)
{
    auto it = codes.find(def);
    return it == codes.end() ? none : it->second;
}

const items::Paint* items::FindPaint(int id)
{
    auto it = paints.find(id);
    return it == paints.end() ? nullptr : &it->second;
}

const std::vector<items::Sticker>& items::Stickers()
{
    return stickers;
}

const items::Sticker* items::FindSticker(int id)
{
    auto it = stickerById.find(id);
    return it == stickerById.end() ? nullptr : &stickers[it->second];
}

const char* items::Name(const std::string (&name)[2])
{
    return name[language].c_str();
}

std::string items::FullName(const Item& item, int paint)
{
    std::string r;
    if (item.category == Knife || item.category == Glove)
        r = "\xE2\x98\x85 ";
    r += Name(item.name);
    if (auto p = paint ? FindPaint(paint) : nullptr)
        r += std::string(" | ") + Name(p->name);
    return r;
}

std::string items::Icon(const Item& item, int paint, float wear)
{
    const Paint* p = paint ? FindPaint(paint) : nullptr;
    if (!p)
        return "panorama/images/" + item.image + "_png.vtex_c";
    const char* w = wear < 0.15f ? "_light" : wear < 0.45f ? "_medium" : "_heavy";
    return "panorama/images/econ/default_generated/" + item.code + "_" + p->code + w + "_png.vtex_c";
}

std::string items::StickerIcon(const Sticker& s)
{
    return "panorama/images/" + s.image + "_png.vtex_c";
}

const char* items::CategoryName(int category)
{
    static const char* ru[] = { "Пистолеты", "Пистолеты-пулемёты", "Винтовки", "Тяжёлое", "Ножи", "Перчатки", "Агенты" };
    static const char* en[] = { "Pistols", "SMGs", "Rifles", "Heavy", "Knives", "Gloves", "Agents" };
    return language ? en[category] : ru[category];
}

const char* items::WearName(float wear)
{
    static const char* ru[] = { "Прямо с завода", "Немного поношенное", "После полевых испытаний", "Поношенное", "Закалённое в боях" };
    static const char* en[] = { "Factory New", "Minimal Wear", "Field-Tested", "Well-Worn", "Battle-Scarred" };
    const int i = wear < 0.07f ? 0 : wear < 0.15f ? 1 : wear < 0.38f ? 2 : wear < 0.45f ? 3 : 4;
    return language ? en[i] : ru[i];
}

unsigned items::RarityColor(int rarity)
{
    static const unsigned colors[] = { 0xFFB0B0B0, 0xFFD9C3B0, 0xFFD9985E, 0xFFFF694B, 0xFFFF4788, 0xFFE62CD3, 0xFF4B4BEB, 0xFF39AEE4 };
    return colors[std::clamp(rarity, 0, 7)];
}

int items::Quality(const Item& item, bool stattrak)
{
    if (item.category == Knife || item.category == Glove)
        return 3;
    return stattrak ? 9 : 4;
}

int items::Rarity(const Item& item, int paint)
{
    if (item.category == Knife || item.category == Glove)
        return 6;
    const Paint* p = paint ? FindPaint(paint) : nullptr;
    if (!p)
        return item.rarity;
    const int base = item.rarity ? item.rarity : 1;
    return std::clamp(base + p->rarity - 1, 0, p->rarity == 7 ? 7 : 6);
}
