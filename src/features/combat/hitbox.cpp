#include "hitbox.h"
#include "../../core/addresses.h"
#include "../../core/cstypes.h"
#include "../../core/hash.h"
#include "../../core/memory.h"
#include "../../core/patterns.h"
#include "../../core/schema.h"
#include "../../core/settings.h"
#include "../../systems/game_reads.h"
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <utility>

namespace
{
    constexpr std::uintptr_t skeleton_bone_disp = 3;
    constexpr std::int32_t max_skeleton_bone_disp = 0x4000;
    constexpr std::uintptr_t model_state_bone_array = 0x80;
    constexpr std::uintptr_t utl_vector_data = 0x8;
    constexpr std::uint32_t hitbox_min_bounds = 0x18;
    constexpr std::uint32_t hitbox_max_bounds = 0x24;
    constexpr std::uint32_t hitbox_shape_radius = 0x30;
    constexpr std::uint32_t hitbox_group_id = 0x38;

    constexpr int max_bone = 256;
    constexpr int max_set_count = 64;
    constexpr int max_hitbox_size = 0x200;
    constexpr float max_bone_distance = 200.f;
    constexpr float max_radius = 32.f;
    constexpr float max_extent = 64.f;
    constexpr float epsilon = 1e-6f;
    constexpr float axis_epsilon = 1e-8f;
    constexpr float far_entry = 1e9f;
    constexpr float min_axis_length = 0.5f;

    constexpr int pelvis_index = 2;
    constexpr int right_foot_index = 11;
    constexpr int left_foot_index = 12;

    struct bone_capsule
    {
        int index;
        int group;
        std::uint32_t bit;
        int first;
        int second;
        float radius;
    };

    constexpr bone_capsule fallback_capsules[] = {
        { 0, cstypes::hitgroup::head, settings::combat::hb_head, 6, 6, 4.5f },
        { 1, cstypes::hitgroup::neck, settings::combat::hb_neck, 5, 6, 3.5f },
        { pelvis_index, cstypes::hitgroup::stomach, settings::combat::hb_pelvis, 0, 2, 6.f },
        { 3, cstypes::hitgroup::stomach, settings::combat::hb_stomach, 2, 4, 6.f },
        { 5, cstypes::hitgroup::chest, settings::combat::hb_chest, 4, 5, 6.f },
    };

    bool call_hitbox_set(std::uintptr_t function, std::uintptr_t pawn, std::uintptr_t& out)
    {
        __try
        {
            out = memory::call<std::uintptr_t>(function, pawn, 0);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool call_bone_index(std::uintptr_t function, std::uintptr_t model, std::uintptr_t hitbox, int& out)
    {
        __try
        {
            out = memory::call<int>(function, model, hitbox, 0);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::uint32_t field_or(std::uint32_t value, std::uint32_t fallback)
    {
        return value ? value : fallback;
    }

    std::uintptr_t bone_array(std::uintptr_t node)
    {
        if (!node)
            return 0;
        std::uintptr_t offset = 0;
        if (const std::uintptr_t code = PATTERN(patterns::skeleton_bone_array))
        {
            const auto disp = systems::reads::value<std::int32_t>(code + skeleton_bone_disp, 0);
            if (disp > 0 && disp < max_skeleton_bone_disp)
                offset = static_cast<std::uintptr_t>(disp);
        }
        if (!offset)
        {
            const std::uint32_t state = SCHEMA("CSkeletonInstance", "m_modelState"_hash);
            if (!state)
                return 0;
            offset = state + model_state_bone_array;
        }
        return systems::reads::pointer(node + offset);
    }

    bool read_bone(std::uintptr_t bones, int index, const math::vector3& origin, math::bone& out)
    {
        if (index < 0 || index >= max_bone)
            return false;
        const std::uintptr_t address = bones + static_cast<std::uintptr_t>(index) * sizeof(math::bone);
        if (!systems::reads::readable(address, sizeof(math::bone)))
            return false;
        out = memory::read<math::bone>(address);
        if (!systems::reads::sane(out.position, systems::reads::world_limit) || out.position.distance(origin) > max_bone_distance)
            return false;
        const float length = std::sqrt(out.rotation[0] * out.rotation[0] + out.rotation[1] * out.rotation[1] + out.rotation[2] * out.rotation[2] + out.rotation[3] * out.rotation[3]);
        if (!std::isfinite(length) || length < epsilon)
        {
            out.rotation[0] = 0.f;
            out.rotation[1] = 0.f;
            out.rotation[2] = 0.f;
            out.rotation[3] = 1.f;
        }
        return true;
    }

    bool sane_extent(const math::vector3& v)
    {
        return systems::reads::sane(v, max_extent);
    }

    bool collect_hitboxes(std::uintptr_t pawn, std::uintptr_t node, std::uintptr_t bones, const math::vector3& origin, features::combat::hitbox::set& out)
    {
        const std::uintptr_t set_function = PATTERN(patterns::get_hitbox_set);
        const std::uintptr_t bone_function = PATTERN(patterns::get_bone_index_for_hitbox);
        const std::uint32_t state = SCHEMA("CSkeletonInstance", "m_modelState"_hash);
        const std::uint32_t model_handle = SCHEMA("CModelState", "m_hModel"_hash);
        const std::uint32_t list = SCHEMA("CHitBoxSet", "m_HitBoxes"_hash);
        const int size = schema::class_size("CHitBox");
        if (!set_function || !bone_function || !state || !model_handle || !list || size <= 0 || size > max_hitbox_size)
            return false;

        const std::uintptr_t model = systems::reads::pointer(systems::reads::pointer(node + state + model_handle));
        if (!model)
            return false;
        std::uintptr_t hitbox_set = 0;
        if (!call_hitbox_set(set_function, pawn, hitbox_set) || !systems::reads::readable(hitbox_set + list, utl_vector_data + sizeof(std::uintptr_t)))
            return false;
        const int count = systems::reads::value<int>(hitbox_set + list, 0);
        const std::uintptr_t data = systems::reads::pointer(hitbox_set + list + utl_vector_data);
        if (count <= 0 || count > max_set_count || !data || !systems::reads::readable(data, static_cast<std::size_t>(count) * static_cast<std::size_t>(size)))
            return false;

        const std::uint32_t min_offset = field_or(SCHEMA("CHitBox", "m_vMinBounds"_hash), hitbox_min_bounds);
        const std::uint32_t max_offset = field_or(SCHEMA("CHitBox", "m_vMaxBounds"_hash), hitbox_max_bounds);
        const std::uint32_t radius_offset = field_or(SCHEMA("CHitBox", "m_flShapeRadius"_hash), hitbox_shape_radius);
        const std::uint32_t group_offset = field_or(SCHEMA("CHitBox", "m_nGroupId"_hash), hitbox_group_id);
        const std::uint32_t limit = static_cast<std::uint32_t>(size);
        if (min_offset + sizeof(math::vector3) > limit || max_offset + sizeof(math::vector3) > limit || radius_offset + sizeof(float) > limit || group_offset + sizeof(int) > limit)
            return false;

        for (int i = 0; i < count && out.count < features::combat::hitbox::max_boxes; ++i)
        {
            const std::uintptr_t entry = data + static_cast<std::uintptr_t>(i) * static_cast<std::uintptr_t>(size);
            int bone_index = -1;
            if (!call_bone_index(bone_function, model, entry, bone_index))
                return false;
            math::bone bone{};
            if (!read_bone(bones, bone_index, origin, bone))
                continue;

            features::combat::hitbox::box& box = out.boxes[out.count];
            box = {};
            box.index = i;
            box.group = memory::read<int>(entry + group_offset);
            box.radius = memory::read<float>(entry + radius_offset);
            box.mins = memory::read<math::vector3>(entry + min_offset);
            box.maxs = memory::read<math::vector3>(entry + max_offset);
            if (!std::isfinite(box.radius) || box.radius < 0.f || box.radius > max_radius || !sane_extent(box.mins) || !sane_extent(box.maxs))
                continue;
            box.bit = features::combat::hitbox::bit_for(i, box.group);
            box.capsule = box.radius > 0.f;
            box.origin = bone.position;
            box.axis[0] = bone.rotate({ 1.f, 0.f, 0.f });
            box.axis[1] = bone.rotate({ 0.f, 1.f, 0.f });
            box.axis[2] = bone.rotate({ 0.f, 0.f, 1.f });
            box.a = bone.position + bone.rotate(box.mins);
            box.b = bone.position + bone.rotate(box.maxs);
            box.center = (box.a + box.b) * 0.5f;
            if (!box.center.is_valid())
                continue;
            ++out.count;
        }
        return out.count > 0;
    }

    bool collect_fallback(std::uintptr_t bones, const math::vector3& origin, features::combat::hitbox::set& out)
    {
        for (const bone_capsule& capsule : fallback_capsules)
        {
            math::bone first{};
            math::bone second{};
            if (!read_bone(bones, capsule.first, origin, first) || !read_bone(bones, capsule.second, origin, second))
                continue;
            features::combat::hitbox::box& box = out.boxes[out.count];
            box = {};
            box.index = capsule.index;
            box.group = capsule.group;
            box.bit = capsule.bit;
            box.capsule = true;
            box.radius = capsule.radius;
            box.a = first.position;
            box.b = second.position;
            box.origin = first.position;
            box.center = (box.a + box.b) * 0.5f;
            ++out.count;
        }
        out.fallback = out.count > 0;
        return out.fallback;
    }

    float sphere_entry(const math::vector3& from, const math::vector3& direction, const math::vector3& center, float radius)
    {
        const math::vector3 m = from - center;
        const float b = m.dot(direction);
        const float q = m.dot(m) - radius * radius;
        if (q > 0.f && b > 0.f)
            return -1.f;
        const float discriminant = b * b - q;
        if (discriminant < 0.f)
            return -1.f;
        const float t = -b - std::sqrt(discriminant);
        return t < 0.f ? 0.f : t;
    }

    float capsule_entry(const features::combat::hitbox::box& box, const math::vector3& from, const math::vector3& direction)
    {
        float best = -1.f;
        const auto take = [&best](float t)
        {
            if (t >= 0.f && (best < 0.f || t < best))
                best = t;
        };
        take(sphere_entry(from, direction, box.a, box.radius));
        take(sphere_entry(from, direction, box.b, box.radius));
        const math::vector3 axis = box.b - box.a;
        const float length_sqr = axis.dot(axis);
        if (length_sqr < epsilon)
            return best;
        const math::vector3 m = from - box.a;
        const float md = m.dot(axis);
        const float nd = direction.dot(axis);
        const float a = length_sqr - nd * nd;
        const float k = m.dot(m) - box.radius * box.radius;
        const float b = length_sqr * m.dot(direction) - nd * md;
        const float c = length_sqr * k - md * md;
        if (std::fabs(a) < epsilon)
            return best;
        const float discriminant = b * b - a * c;
        if (discriminant < 0.f)
            return best;
        float t = (-b - std::sqrt(discriminant)) / a;
        if (t < 0.f && c <= 0.f)
            t = 0.f;
        const float s = md + t * nd;
        if (t >= 0.f && s >= 0.f && s <= length_sqr)
            take(t);
        return best;
    }

    float box_entry(const features::combat::hitbox::box& box, const math::vector3& from, const math::vector3& direction)
    {
        const math::vector3 relative = from - box.origin;
        const float o[3]{ relative.dot(box.axis[0]), relative.dot(box.axis[1]), relative.dot(box.axis[2]) };
        const float d[3]{ direction.dot(box.axis[0]), direction.dot(box.axis[1]), direction.dot(box.axis[2]) };
        const float lo[3]{ box.mins.x, box.mins.y, box.mins.z };
        const float hi[3]{ box.maxs.x, box.maxs.y, box.maxs.z };
        float t_min = 0.f;
        float t_max = far_entry;
        for (int i = 0; i < 3; ++i)
        {
            if (std::fabs(d[i]) < axis_epsilon)
            {
                if (o[i] < lo[i] || o[i] > hi[i])
                    return -1.f;
                continue;
            }
            float t1 = (lo[i] - o[i]) / d[i];
            float t2 = (hi[i] - o[i]) / d[i];
            if (t1 > t2)
                std::swap(t1, t2);
            t_min = std::max(t_min, t1);
            t_max = std::min(t_max, t2);
            if (t_min > t_max)
                return -1.f;
        }
        return t_min;
    }

    math::vector3 side_vector(const features::combat::hitbox::box& box, const math::vector3& eye)
    {
        const math::vector3 view = (box.center - eye).normalized();
        math::vector3 axis = (box.b - box.a).normalized();
        if (axis.length() < min_axis_length)
            axis = { 0.f, 0.f, 1.f };
        math::vector3 side = view.cross(axis).normalized();
        if (side.length() < min_axis_length)
            side = view.cross({ 0.f, 0.f, 1.f }).normalized();
        if (side.length() < min_axis_length)
            side = { 0.f, 1.f, 0.f };
        return side;
    }
}

namespace features::combat::hitbox
{
    std::uint32_t bit_for(int index, int group)
    {
        if (index == pelvis_index)
            return settings::combat::hb_pelvis;
        if (index == right_foot_index || index == left_foot_index)
            return settings::combat::hb_feet;
        switch (group)
        {
        case cstypes::hitgroup::head:
            return settings::combat::hb_head;
        case cstypes::hitgroup::neck:
            return settings::combat::hb_neck;
        case cstypes::hitgroup::chest:
            return settings::combat::hb_chest;
        case cstypes::hitgroup::stomach:
            return settings::combat::hb_stomach;
        case cstypes::hitgroup::left_arm:
        case cstypes::hitgroup::right_arm:
            return settings::combat::hb_arms;
        case cstypes::hitgroup::left_leg:
        case cstypes::hitgroup::right_leg:
            return settings::combat::hb_legs;
        default:
            return 0;
        }
    }

    bool collect(std::uintptr_t pawn, set& out)
    {
        out.count = 0;
        out.fallback = false;
        math::vector3 origin{};
        if (!pawn || !systems::reads::abs_origin(pawn, origin))
            return false;
        const std::uintptr_t node = systems::reads::scene_node(pawn);
        const std::uintptr_t bones = bone_array(node);
        if (!bones)
            return false;
        if (collect_hitboxes(pawn, node, bones, origin, out))
            return true;
        out.count = 0;
        return collect_fallback(bones, origin, out);
    }

    float entry(const box& target, const math::vector3& from, const math::vector3& direction)
    {
        return target.capsule ? capsule_entry(target, from, direction) : box_entry(target, from, direction);
    }

    int nearest(const set& boxes, const math::vector3& from, const math::vector3& direction, float range, float& distance)
    {
        int best = -1;
        distance = range;
        for (int i = 0; i < boxes.count; ++i)
        {
            const float t = entry(boxes.boxes[i], from, direction);
            if (t >= 0.f && t <= distance)
            {
                distance = t;
                best = i;
            }
        }
        return best;
    }

    int points(const box& target, const math::vector3& eye, bool multipoint, float head_scale, float body_scale, math::vector3* out, int max)
    {
        if (!out || max <= 0)
            return 0;
        int count = 0;
        const auto push = [&](const math::vector3& p)
        {
            if (count < max && p.is_valid())
                out[count++] = p;
        };
        push(target.center);
        if (!multipoint)
            return count;
        const bool head = target.group == cstypes::hitgroup::head;
        const float scale = std::clamp(head ? head_scale : body_scale, 0.f, 1.f);
        if (scale <= 0.f)
            return count;

        if (target.capsule)
        {
            const math::vector3 side = side_vector(target, eye);
            const float r = target.radius * scale;
            if (head)
            {
                const math::vector3& top = target.a.z > target.b.z ? target.a : target.b;
                push(top + math::vector3{ 0.f, 0.f, r });
                push(target.center + side * r);
                push(target.center - side * r);
                push(top + side * r);
                push(top - side * r);
                return count;
            }
            push(target.center + side * r);
            push(target.center - side * r);
            push(target.center + (target.a - target.center) * scale);
            push(target.center + (target.b - target.center) * scale);
            return count;
        }

        const math::vector3 half = (target.maxs - target.mins) * 0.5f;
        const float extents[3]{ half.x, half.y, half.z };
        for (int i = 0; i < 3; ++i)
        {
            push(target.center + target.axis[i] * (extents[i] * scale));
            push(target.center - target.axis[i] * (extents[i] * scale));
        }
        return count;
    }
}
