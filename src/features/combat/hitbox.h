#pragma once
#include "../../core/math.h"
#include <cstdint>

namespace features::combat::hitbox
{
    constexpr int max_boxes = 32;
    constexpr int max_points = 8;

    struct box
    {
        int index = -1;
        int group = 0;
        std::uint32_t bit = 0;
        bool capsule = false;
        float radius = 0.f;
        math::vector3 a{};
        math::vector3 b{};
        math::vector3 center{};
        math::vector3 origin{};
        math::vector3 axis[3]{};
        math::vector3 mins{};
        math::vector3 maxs{};
    };

    struct set
    {
        box boxes[max_boxes]{};
        int count = 0;
        bool fallback = false;
    };

    std::uint32_t bit_for(int index, int group);
    bool collect(std::uintptr_t pawn, set& out);
    float entry(const box& target, const math::vector3& from, const math::vector3& direction);
    int nearest(const set& boxes, const math::vector3& from, const math::vector3& direction, float range, float& distance);
    int points(const box& target, const math::vector3& eye, bool multipoint, float head_scale, float body_scale, math::vector3* out, int max);
    box scaled(const box& target, float scale);
    void shrink(const set& in, float scale, set& out);
    math::vector3 describe(const box& target, const math::vector3& world);
}
