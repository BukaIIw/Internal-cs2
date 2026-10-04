#pragma once
#include <cstdint>
#include <vector>

namespace events
{
    enum class type : int
    {
        frame_stage,
        create_move,
        create_move_post,
        override_view,
        present,
        level_init,
        level_shutdown,
        local_pawn_changed,
        menu_toggle,
        unload,
        count
    };

    namespace priority
    {
        constexpr int first = -100;
        constexpr int systems = -50;
        constexpr int movement_pre = -20;
        constexpr int combat = 0;
        constexpr int movement = 20;
        constexpr int view = 50;
        constexpr int normal = 60;
        constexpr int last = 100;
    }

    struct frame_stage_args
    {
        int stage;
    };

    struct override_view_args
    {
        std::uintptr_t client_mode;
        std::uintptr_t view_setup;
    };

    using handler = void (*)(void* args);

    struct info
    {
        int type;
        int index;
        const char* name;
        int priority;
        std::uint64_t calls;
        double avg_us;
        double max_us;
        bool faulted;
    };

    void subscribe(type t, const char* name, handler h, int priority = priority::normal);
    void publish(type t, void* args = nullptr);
    const char* name(int t);
    std::vector<info> snapshot();
    void enable(int t, int index);
    void reset_stats();
}
