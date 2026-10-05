#pragma once
#include "keys.h"
#include <array>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace settings
{
    struct color
    {
        float r = 1.f, g = 1.f, b = 1.f, a = 1.f;
        float* data() { return &r; }
        const float* data() const { return &r; }
        std::uint32_t abgr() const;
    };

    namespace combat
    {
        enum hitbox_bits : std::uint32_t
        {
            hb_head = 1u << 0,
            hb_neck = 1u << 1,
            hb_chest = 1u << 2,
            hb_stomach = 1u << 3,
            hb_pelvis = 1u << 4,
            hb_arms = 1u << 5,
            hb_legs = 1u << 6,
            hb_feet = 1u << 7,
            hb_all = 0xFF
        };

        enum autostop_bits : std::uint32_t
        {
            as_between_shots = 1u << 0,
            as_lethal = 1u << 1,
            as_air = 1u << 2,
            as_landing = 1u << 3
        };

        enum weapon_group : int
        {
            wg_global,
            wg_pistol,
            wg_heavy_pistol,
            wg_smg,
            wg_rifle,
            wg_shotgun,
            wg_scout,
            wg_awp,
            wg_auto,
            wg_machinegun,
            wg_count
        };

        int weapon_group_of(std::uint16_t def);
        const char* weapon_group_name(int group);

        struct rage
        {
            bool override_global = false;
            bool enabled = false;
            keys::bind key{ 0, keys::mode::always };
            bool silent = true;
            bool autofire = true;
            bool autowall = false;
            bool autostop = true;
            std::uint32_t autostop_flags = as_landing;
            bool autoscope = true;
            float fov = 180.f;
            std::uint32_t hitboxes = hb_head | hb_chest | hb_stomach | hb_pelvis;
            std::uint32_t multipoint = hb_head | hb_chest | hb_stomach;
            float head_scale = 0.6f;
            float body_scale = 0.55f;
            float safe_scale = 0.8f;
            int hitchance = 65;
            int minimum_damage = 30;
            keys::bind damage_override_key{ 0, keys::mode::hold };
            int damage_override = 1;
            bool force_shot = true;
            int force_shot_iterations = 3;
            float force_shot_min_spread = 0.5f;
            bool prefer_body = false;
            bool nospread = false;
            bool seed_check = true;
            bool teammates = false;
            bool doubletap = false;
            keys::bind doubletap_key{ 0, keys::mode::toggle };
            int doubletap_mode = 0;
            int tick_source = 0;
        };

        struct legit
        {
            bool override_global = false;
            bool enabled = false;
            keys::bind key{ 1, keys::mode::hold };
            float fov = 4.f;
            float speed = 25.f;
            int speed_mode = 0;
            int randomization = 0;
            keys::bind random_key{ 0, keys::mode::always };
            std::uint32_t hitboxes = hb_head;
            bool rcs = true;
            float rcs_scale = 1.f;
            bool visible_only = true;
            bool teammates = false;
        };

        struct trigger
        {
            bool override_global = false;
            bool enabled = false;
            keys::bind key{ 6, keys::mode::hold };
            int delay = 0;
            std::uint32_t hitboxes = hb_all;
            int minimum_damage = 1;
            int hitchance = 0;
            bool seed_check = true;
            float safe_scale = 0.8f;
            bool teammates = false;
        };
    }

    struct movement
    {
        bool bhop = false;
        keys::bind bhop_key{ 0, keys::mode::always };
        bool airstrafe = false;
        bool airstrafe_fully_directional = false;
        int airstrafe_mode = 0;
        keys::bind airstrafe_key{ 0, keys::mode::always };
        bool jumpbug = false;
        keys::bind jumpbug_key{ 0, keys::mode::hold };
        bool fastladder = false;
        bool quickstop = false;
    };

    struct visuals
    {
        bool esp = false;
        bool box = true;
        bool name = true;
        bool health = true;
        bool weapon = true;
        bool distance = false;
        bool skeleton = false;
        bool skeleton_all = false;
        bool hitbox_zones = false;
        bool snaplines = false;
        bool teammates = false;
        color visible{ 0.30f, 0.85f, 0.45f, 1.f };
        color hidden{ 0.95f, 0.35f, 0.35f, 1.f };
        color team{ 0.35f, 0.60f, 1.f, 1.f };
        bool glow = false;
        bool glow_teammates = false;
        bool glow_by_visibility = true;
        color glow_visible{ 0.30f, 0.85f, 0.45f, 0.85f };
        color glow_hidden{ 0.95f, 0.35f, 0.35f, 0.85f };
        color glow_team{ 0.35f, 0.60f, 1.f, 0.85f };
        bool fov_circle = false;
        bool hands_tint = false;
        color hands_color{ 1.f, 1.f, 1.f, 1.f };
        bool weapon_tint = false;
        color weapon_color{ 1.f, 1.f, 1.f, 1.f };
        bool grenade_prediction = false;
        color grenade_color{ 0.95f, 0.85f, 0.35f, 1.f };
    };

    struct misc
    {
        bool thirdperson = false;
        keys::bind thirdperson_key{ 'V', keys::mode::toggle };
        float thirdperson_distance = 120.f;
        bool watermark = true;
        bool keybinds = true;
        bool shot_logs = true;
        bool shot_file = true;
        float keybinds_x = -1.f;
        float keybinds_y = -1.f;
    };

    namespace changer
    {
        struct applied_skin
        {
            int paint_kit_id = 0;
            int seed = 0;
            float wear = 0.0001f;
            int stattrak = -1;
            std::string nametag;
            std::array<int, 5> stickers{};
        };

        struct inventory_entry
        {
            int uid = 0;
            std::int16_t def_index = 0;
            applied_skin skin;
            bool equipped_t = false;
            bool equipped_ct = false;
        };

        struct agents
        {
            std::int16_t ct_def = 0;
            std::int16_t t_def = 0;
        };

        struct skins
        {
            std::map<std::int16_t, applied_skin> data;
        };
    }

    struct changer_settings
    {
        bool enabled = true;
        bool knife_animations = true;
        int language = 1;
        changer::skins skins;
        changer::agents agents;
        std::vector<changer::inventory_entry> inventory;
        int next_uid = 1;
        void rebuild(int team);
        const changer::applied_skin* find(std::int16_t def_index, int team) const;
        std::int16_t knife(int team) const;
        std::int16_t glove(int team) const;
    };

    struct ui_settings
    {
        color accent{ 0.25f, 0.72f, 1.f, 1.f };
        bool reduce_motion = false;
        float scale = 1.f;
        keys::bind menu_key{ 0x2D, keys::mode::toggle };
    };

    inline std::array<combat::rage, combat::wg_count> g_rage_groups{};
    inline std::array<combat::legit, combat::wg_count> g_legit_groups{};
    inline std::array<combat::trigger, combat::wg_count> g_trigger_groups{};
    inline combat::rage& g_rage = g_rage_groups[combat::wg_global];
    inline combat::legit& g_legit = g_legit_groups[combat::wg_global];
    inline combat::trigger& g_trigger = g_trigger_groups[combat::wg_global];
    inline int g_edit_group = combat::wg_global;
    inline bool g_edit_follow_weapon = true;

    const combat::rage& rage_for(int group);
    const combat::legit& legit_for(int group);
    const combat::trigger& trigger_for(int group);
    inline movement g_movement{};
    inline visuals g_visuals{};
    inline misc g_misc{};
    inline changer_settings g_changer{};
    inline ui_settings g_ui{};

    inline std::recursive_mutex g_changer_mutex;

    void register_all();
    bool save();
    bool load();
    const char* path();
}
