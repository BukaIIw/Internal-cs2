#pragma once
#include "../../core/math.h"
#include "../../systems/input.h"
#include <array>
#include <cstdint>

namespace features::combat
{
    struct weapon_context
    {
        bool valid = false;
        std::uintptr_t weapon = 0;
        std::uintptr_t vdata = 0;
        std::uint16_t def = 0;
        int type = -1;
        bool gun = false;
        bool full_auto = false;
        bool scoped = false;
        bool needs_scope = false;
        bool can_fire = false;
        bool reloading = false;
        int ticks_to_fire = 0;
        int tick_base = 0;
        int revolver_ready_tick = 0;
        float revolver_ready_frac = 0.f;
        bool revolver_hauled = false;
        int shots_fired = 0;
        float last_shot_time = 0.f;
        std::uint32_t input_history = 0;
        std::uint32_t shoot_history = 0;
        int group = 0;
        int clip = 0;
        int bullets = 1;
        int mode = 0;
        float recoil_index = 0.f;
        float inaccuracy = 0.f;
        float spread = 0.f;
        float damage = 0.f;
        float range = 8192.f;
        float range_modifier = 1.f;
        float penetration = 1.f;
        float armor_ratio = 1.f;
        float headshot_multiplier = 4.f;
        float max_speed = 250.f;
        float velocity_modifier = 1.f;
        math::qangle punch{};
        math::vector3 eye{};
    };

    class shared
    {
    public:
        void update();
        const weapon_context& ctx() const { return m_ctx; }

    private:
        weapon_context m_ctx{};
    };

    class rage
    {
    public:
        void on_frame_stage(int stage);
        void on_create_move(systems::input::frame& frame);
        void on_create_move_post(systems::input::usercmd& cmd);
        void on_create_move_late(systems::input::usercmd& cmd);
        bool is_firing_this_tick() const { return m_firing; }
        bool should_stop() const { return m_stop; }
        void reset();

        struct debug_info
        {
            bool target = false;
            int nospread = 0;
            int hitgroup = -1;
            int points = 0;
            float damage = 0.f;
            float hitchance = 0.f;
            bool fired = false;
            int seed = -1;
            int seed_delta = 0;
        };
        debug_info debug{};

    private:
        void finish_shot(const math::qangle& view, int tick);
        void cancel_shot(systems::input::usercmd& cmd);

        bool m_firing = false;
        bool m_stop = false;
    };

    class doubletap
    {
    public:
        void on_create_move_post(systems::input::usercmd& cmd);
        void reset();
        bool charged(const weapon_context& ctx) const;

    private:
        bool m_release = false;
        int m_shots = 0;
        int m_second = 0;
    };

    class legit
    {
    public:
        void on_create_move(systems::input::frame& frame);

    private:
        math::qangle m_last_recoil{};
        math::qangle m_jitter{};
        float m_speed_scale = 1.f;
        int m_random_ticks = 0;
    };

    struct spread_preview
    {
        bool valid = false;
        bool has_bullet = false;
        math::vector3 eye{};
        math::vector3 forward{};
        math::vector3 bullet{};
        float tangent = 0.f;
    };

    spread_preview spread_view();

    class trigger
    {
    public:
        void on_create_move(systems::input::frame& frame);

    private:
        std::uint64_t m_seen = 0;
    };

    inline shared g_shared{};
    inline rage g_rage{};
    inline legit g_legit{};
    inline doubletap g_doubletap{};
    inline trigger g_trigger{};
}
