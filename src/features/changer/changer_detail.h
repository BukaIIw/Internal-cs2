#pragma once
#include "changer.h"
#include "../../core/memory.h"
#include "../../core/schema.h"
#include "../../systems/game_reads.h"
#include <cstdint>
#include <vector>

namespace features::changer::detail
{
    constexpr std::uint32_t faux_id_high = 0xF0000000u;
    constexpr std::uint32_t faux_id_low = 0x10u;
    constexpr std::uint64_t faux_item_id = 0xF000000000000010ull;
    constexpr std::size_t post_data_update_index = 10;
    constexpr std::uint32_t subclass_vdata_offset = 0x8;
    constexpr std::size_t max_weapons = 64;

    template <typename T>
    inline bool write_field(std::uintptr_t base, std::uint32_t offset, const T& value)
    {
        if (!base || !offset || !systems::reads::readable(base + offset, sizeof(T)))
            return false;
        memory::write<T>(base + offset, value);
        return true;
    }

    template <typename T>
    inline T read_field(std::uintptr_t base, std::uint32_t offset, T fallback = T{})
    {
        return systems::reads::field<T>(base, offset, fallback);
    }

    bool has_vfunc(std::uintptr_t object, std::size_t index);
    std::uintptr_t item_view(std::uintptr_t weapon);
    bool vdata_ready(std::uintptr_t weapon);
    std::uint32_t account_id(std::uintptr_t controller);
    std::vector<std::uint32_t> weapon_handles(std::uintptr_t pawn);
    std::uint32_t composite_offset();
    void set_mesh_mask(std::uintptr_t entity, const econ_item_system::paint_kit* pk);
    void rebuild_weapon_paint(std::uintptr_t weapon, const econ_item_system::paint_kit* pk);
    void post_data_update(std::uintptr_t entity);

    float sanitize_wear(float wear);
    int sanitize_seed(int seed);
    int sanitize_stattrak(int stattrak);

    bool selected_skin(std::int16_t def_index, int team, settings::changer::applied_skin& out);
    std::int16_t selected_knife(int team);
    std::int16_t selected_glove(int team);
    std::int16_t selected_agent(int team);
    bool knife_animations();

    void request_glove_refresh();
    bool consume_glove_refresh();

    void shutdown_econ();
}
