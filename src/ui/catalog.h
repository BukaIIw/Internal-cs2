#pragma once
#include "imgui.h"
#include <cstdint>
#include <string>
#include <vector>

namespace ui::catalog
{
    enum category : int
    {
        pistol,
        smg,
        rifle,
        heavy,
        knife,
        glove,
        agent,
        category_count
    };

    enum class source : int
    {
        none,
        econ,
        items
    };

    struct paint
    {
        int id = 0;
        std::string name;
        float wear_min = 0.f;
        float wear_max = 1.f;
    };

    struct item
    {
        std::int16_t def = 0;
        int category = rifle;
        int teams = 3;
        std::string name;
        std::vector<int> paints;
    };

    struct entry
    {
        std::size_t item_index = 0;
        int paint = 0;
        int rarity = 0;
        std::string title;
        std::string full_name;
        std::string lower;
    };

    struct sticker
    {
        int id = 0;
        int rarity = 0;
        std::string name;
        std::string icon;
        std::string lower;
    };

    void update();
    source current_source();
    std::uint32_t generation();
    const char* status_text();

    const std::vector<item>& list();
    const std::vector<entry>& entries();
    const std::vector<sticker>& stickers();
    const item* find(std::int16_t def);
    const paint* find_paint(int id);
    const sticker* find_sticker(int id);

    int rarity(std::int16_t def, int paint_id);
    std::string full_name(std::int16_t def, int paint_id);
    std::string icon(std::int16_t def, int paint_id, float wear);
    ImU32 rarity_color(int rarity);
    const char* category_name(int category);
    const char* wear_name(float wear);
    const char* tr(const char* ru, const char* en);
    std::string lower(const char* text);
    bool is_weapon(int category);
}
