#pragma once
#include "combat.h"
#include "hitbox.h"
#include "../../core/math.h"
#include "../../systems/entities.h"
#include <cstdint>

namespace features::combat::shots
{
    constexpr int max_entries = 8;
    constexpr int text_size = 160;

    struct fired
    {
        systems::entities::player player{};
        int slot = -1;
        int group = -1;
        float damage = 0.f;
        float hitchance = 0.f;
        int backtrack_tick = 0;
        bool nospread = false;
        bool pitch_broken = false;
        bool penetrated = false;
        int tick = 0;
        math::vector3 eye{};
        math::qangle view{};
        math::qangle recoil{};
        hitbox::set boxes{};
    };

    struct entry
    {
        std::uint64_t time = 0;
        bool hit = false;
        char text[text_size]{};
    };

    void on_fire(const fired& shot, const weapon_context& ctx, std::uintptr_t local_pawn);
    void update();
    void reset();
    int snapshot(entry* out, int max);
}
