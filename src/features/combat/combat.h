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
        bool is_firing_this_tick() const { return m_firing; }
        bool should_stop() const { return m_stop; }
        void reset();

        struct debug_info
        {
            bool target = false;
            int hitgroup = -1;
            int points = 0;
            float damage = 0.f;
            float hitchance = 0.f;
            bool fired = false;
        };
        debug_info debug{};

    private:
        bool m_firing = false;
        bool m_stop = false;
    };

    class legit
    {
    public:
        void on_create_move(systems::input::frame& frame);
    };

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
    inline trigger g_trigger{};
}
