#pragma once
#include "../../core/settings.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace features::changer
{
    class econ_item_system
    {
    public:
        enum class item_category : std::uint8_t
        {
            gun,
            knife,
            glove,
            agent,
            other
        };

        struct paint_kit
        {
            int id{};
            std::string name{};
            std::string desc_token{};
            std::string name_token{};
            std::string localized_name{};
            float wear_min{};
            float wear_max{};
            bool legacy_model{};
            std::uint8_t rarity{};
        };

        struct item_def
        {
            std::int16_t def_index{};
            std::string item_class{};
            std::string name{};
            std::string localized_name{};
            std::string model_player{};
            std::string image_inventory{};
            int loadout_slot{};
            std::uint32_t used_by_classes{};
            item_category category{};
            std::uint8_t rarity{};

            int team() const
            {
                if ((used_by_classes & 0xc) == 0xc)
                    return 0;
                if (used_by_classes & 4)
                    return 2;
                if (used_by_classes & 8)
                    return 3;
                return 0;
            }
        };

        struct skin_entry
        {
            std::int16_t def_index{};
            int paint_kit_id{};
        };

        bool initialize();
        bool ready() const { return m_ready.load(); }

        const std::vector<paint_kit>& paint_kits() const { return m_paint_kits; }
        const std::vector<item_def>& item_defs() const { return m_item_defs; }
        const std::vector<const item_def*>& knives() const { return m_knives; }
        const std::vector<const item_def*>& gloves() const { return m_gloves; }
        const std::vector<const item_def*>& agents() const { return m_agents; }
        const std::vector<const item_def*>& guns() const { return m_guns; }
        const std::vector<skin_entry>& skins() const { return m_skins; }
        std::vector<int> skins_for(std::int16_t def_index) const;

        const item_def* find_def(std::int16_t def_index) const;
        const paint_kit* find_paint_kit(int id) const;
        int combined_rarity(std::int16_t def_index, int paint_kit_id) const;
        std::string image_path(std::int16_t def_index, int paint_kit_id, float wear) const;

    private:
        bool parse_item_defs(std::uintptr_t schema);
        bool parse_paint_kits(std::uintptr_t schema);
        void build_indices();
        void resolve_localized_names();
        void build_skin_index();
        item_category classify(const item_def& def) const;

        std::vector<paint_kit> m_paint_kits{};
        std::vector<item_def> m_item_defs{};
        std::vector<const item_def*> m_knives{};
        std::vector<const item_def*> m_gloves{};
        std::vector<const item_def*> m_agents{};
        std::vector<const item_def*> m_guns{};
        std::vector<skin_entry> m_skins{};
        std::unordered_map<std::int16_t, std::size_t> m_def_index_map{};
        std::unordered_map<int, std::size_t> m_paint_kit_map{};
        std::unordered_map<std::int16_t, std::vector<int>> m_skins_by_def{};
        std::atomic<bool> m_ready{ false };
    };

    class agents
    {
    public:
        void on_frame_stage_notify();
        void restore();

    private:
        std::string m_original_model{};
        std::uintptr_t m_tracked_pawn{};
        std::uintptr_t m_applied_handle{};
        std::int16_t m_applied_def{};
        bool m_overridden{};
        int m_tracked_team{};
    };

    class gloves
    {
    public:
        void on_frame_stage_notify();
        void restore();

    private:
        struct original_state
        {
            std::uint16_t def_index{};
            std::uint64_t item_id{};
            std::uint32_t id_high{};
            std::uint32_t id_low{};
            std::uint32_t account_id{};
            bool restore_custom_material{};
            bool initialized{};
            bool disallow_soc{};
            bool captured{};
        };

        struct attribute_state
        {
            float value{};
            bool present{};
        };

        bool capture_original(std::uintptr_t item_view);
        bool read_paint_attributes(std::uintptr_t item_view, std::array<attribute_state, 3>& attributes) const;
        bool restore_paint_attributes(std::uintptr_t item_view) const;
        bool paint_attributes_match(std::uintptr_t item_view, const settings::changer::applied_skin& skin) const;
        void apply(std::uintptr_t pawn, std::uintptr_t item_view, int team, const econ_item_system::item_def& def, const settings::changer::applied_skin& skin, std::uint32_t account_id);
        void restore(std::uintptr_t pawn, std::uintptr_t item_view, int team);
        void refresh(std::uintptr_t pawn, std::uintptr_t item_view, int team) const;
        void reset();

        original_state m_original{};
        std::array<attribute_state, 3> m_original_attributes{};
        std::uintptr_t m_tracked_pawn{};
        std::int16_t m_applied_def{};
        int m_applied_paint{};
        bool m_overridden{};
    };

    class guns
    {
    public:
        void on_frame_stage_notify();
        void restore();

    private:
        struct original_state
        {
            std::uint32_t id_high{};
            std::uint32_t id_low{};
            std::uint32_t account_id{};
            bool initialized{};
            int paint_kit{};
            int seed{};
            float wear{};
            int stattrak{};
        };

        struct applied_state
        {
            int paint_kit{};
            int seed{};
            float wear{};
            int stattrak{};
        };

        void apply(std::uintptr_t weapon, std::uintptr_t iv, std::uint32_t handle, std::uint32_t active_handle, std::uintptr_t pawn, const settings::changer::applied_skin& skin, std::uint32_t account_id);
        void rebuild_paint(std::uintptr_t weapon, std::uint32_t handle, std::uint32_t active_handle, std::uintptr_t pawn, const econ_item_system::paint_kit* pk);

        std::uint32_t m_last_active_handle{};
        std::uintptr_t m_tracked_pawn{};
        std::unordered_map<std::uint32_t, applied_state> m_applied{};
        std::unordered_map<std::uint32_t, original_state> m_originals{};
    };

    class knives
    {
    public:
        void on_frame_stage_notify();
        void restore();

    private:
        struct original_state
        {
            std::uint16_t def_index{};
            std::uint32_t id_high{};
            std::uint32_t id_low{};
            std::uint32_t account_id{};
            bool initialized{};
            int paint_kit{};
            int seed{};
            float wear{};
            int stattrak{};
            std::uint32_t subclass{};
            bool captured{};
        };

        void capture_original(std::uintptr_t weapon, std::uintptr_t iv);
        void apply(std::uintptr_t weapon, std::uintptr_t iv, const econ_item_system::item_def* def, const settings::changer::applied_skin& skin, std::uint32_t account_id, std::uintptr_t active_weapon, std::uintptr_t pawn);
        void restore(std::uintptr_t weapon, std::uintptr_t iv, std::uintptr_t active_weapon, std::uintptr_t pawn);
        void update_model(std::uintptr_t weapon, std::uintptr_t iv, std::uint16_t def_index, std::uintptr_t pawn);
        void rebuild_paint(std::uintptr_t weapon, std::uintptr_t active_weapon, std::uintptr_t pawn, const econ_item_system::paint_kit* pk);
        void on_deploy(std::uintptr_t pawn);

        original_state m_original{};
        std::uint32_t m_last_active_handle{};
        std::uint32_t m_weapon_handle{};
        std::uintptr_t m_tracked_pawn{};
        bool m_overridden{};
    };

    std::uintptr_t find_hud_model_weapon(std::uintptr_t pawn);
    void update_hud_mesh(std::uintptr_t pawn, const econ_item_system::paint_kit* pk);
    void invalidate_hud_icon(std::uintptr_t item_view);
    std::uint32_t make_subclass_token(std::int16_t def_index);

    void on_frame_stage(int stage);
    void on_unload();

    inline econ_item_system g_econ_item_system{};
    inline agents g_agents{};
    inline gloves g_gloves{};
    inline guns g_guns{};
    inline knives g_knives{};
}
