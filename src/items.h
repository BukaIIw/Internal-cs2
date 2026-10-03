#pragma once
#include <string>
#include <vector>
#include <atomic>

namespace items
{
    enum Category { Pistol, Smg, Rifle, Heavy, Knife, Glove, Agent, CategoryCount };

    struct Paint
    {
        int id = 0;
        std::string code;
        std::string name[2];
        int rarity = 0;
        float wearMin = 0.06f;
        float wearMax = 0.8f;
        bool legacy = false;
    };

    struct Skin
    {
        int paint = 0;
        int rarity = 0;
    };

    struct Item
    {
        int def = 0;
        Category category = Pistol;
        std::string code, image, model;
        std::string name[2];
        int rarity = 0;
        int teams = 3;
        int slot = 0;
        std::vector<Skin> skins;
    };

    struct Sticker
    {
        int id = 0;
        std::string name[2];
        std::string image;
        int rarity = 0;
    };

    inline std::atomic<int> state{ 0 };
    inline std::string error;
    inline int language = 1;

    void Load();
    void LoadFrom(const std::wstring& pak);
    const std::vector<Item>& List();
    const Item* Find(int def);
    const std::string& Code(int def);
    const Paint* FindPaint(int id);
    const std::vector<Sticker>& Stickers();
    const Sticker* FindSticker(int id);

    const char* Name(const std::string (&name)[2]);
    std::string FullName(const Item& item, int paint);
    std::string Icon(const Item& item, int paint, float wear);
    std::string StickerIcon(const Sticker& s);
    const char* CategoryName(int category);
    const char* WearName(float wear);
    unsigned RarityColor(int rarity);
    int Quality(const Item& item, bool stattrak);
    int Rarity(const Item& item, int paint);
}
