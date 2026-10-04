#include "visuals.h"
#include "../../core/addresses.h"
#include "../../core/convar.h"
#include "../../core/cstypes.h"
#include "../../core/hash.h"
#include "../../core/schema.h"
#include "../../core/settings.h"
#include "../../systems/game_reads.h"
#include "../../systems/input.h"
#include "../../systems/local.h"
#include "../../systems/tracing.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>

namespace
{
    namespace reads = systems::reads;

    constexpr int flashbang = 43;
    constexpr int hegrenade = 44;
    constexpr int smoke = 45;
    constexpr int molotov = 46;
    constexpr int decoy = 47;
    constexpr int incendiary = 48;

    constexpr float default_throw_velocity = 750.f;
    constexpr float min_throw_velocity = 15.f;
    constexpr float max_throw_velocity = 2000.f;
    constexpr float throw_scale = 0.9f;
    constexpr float default_gravity = 800.f;
    constexpr float gravity_scale = 0.4f;
    constexpr float elasticity = 0.45f;
    constexpr float floor_normal = 0.7f;
    constexpr float rest_speed_sqr = 400.f;
    constexpr float fuse_time = 1.5f;
    constexpr float molotov_time = 2.f;
    constexpr float max_time = 20.f;
    constexpr float throw_offset = 22.f;
    constexpr float throw_back = 6.f;
    constexpr float surface_offset = 0.1f;
    constexpr float velocity_inherit = 1.25f;
    constexpr int max_bounces = 16;
    constexpr int max_points = 512;

    struct path
    {
        bool valid = false;
        int count = 0;
        math::vector3 points[max_points]{};
        int bounce_count = 0;
        math::vector3 bounces[max_bounces]{};
        math::view_matrix matrix{};
    };

    path g_building{};
    path g_path{};
    std::mutex g_mutex;

    bool is_grenade(int def)
    {
        return def == flashbang || def == hegrenade || def == smoke || def == molotov || def == decoy || def == incendiary;
    }

    bool read_view_matrix(math::view_matrix& out)
    {
        const std::uintptr_t matrix = addresses::globals::view_matrix();
        if (!reads::readable(matrix, sizeof(math::view_matrix)))
            return false;
        std::memcpy(&out, reinterpret_cast<const void*>(matrix), sizeof(math::view_matrix));
        for (const auto& row : out.m)
            for (const float v : row)
                if (!std::isfinite(v))
                    return false;
        return true;
    }

    float gravity()
    {
        const auto* cvar = CONVAR("sv_gravity");
        const float value = cvar->value ? cvar->get<float>() : default_gravity;
        return (std::isfinite(value) && value > 0.f ? value : default_gravity) * gravity_scale;
    }

    float detonate_time(int def)
    {
        if (def == molotov || def == incendiary)
        {
            const auto* cvar = CONVAR("molotov_throw_detonate_time");
            const float value = cvar->value ? cvar->get<float>() : molotov_time;
            return std::isfinite(value) && value > 0.f ? value : molotov_time;
        }
        if (def == flashbang || def == hegrenade)
            return fuse_time;
        return max_time;
    }

    void push(path& out, const math::vector3& p)
    {
        if (out.count < max_points)
            out.points[out.count++] = p;
    }

    void simulate(path& out, std::uintptr_t pawn, int def, const math::vector3& eye, const math::qangle& view, const math::vector3& player_velocity, float strength, float throw_velocity)
    {
        math::qangle angles = view;
        if (angles.x > 90.f)
            angles.x -= 360.f;
        else if (angles.x < -90.f)
            angles.x += 360.f;
        angles.x -= (90.f - std::fabs(angles.x)) * 10.f / 90.f;
        math::vector3 forward{};
        math::helpers::angle_vectors(angles, forward);

        strength = std::clamp(strength, 0.f, 1.f);
        const float speed = std::clamp(throw_velocity * throw_scale, min_throw_velocity, default_throw_velocity) * (strength * 0.7f + 0.3f);
        math::vector3 src = eye;
        src.z += strength * 12.f - 12.f;
        const systems::tracing::result start = systems::g_tracing.trace_line(src, src + forward * throw_offset, pawn, cstypes::masks::world);
        math::vector3 pos = (start.ok ? start.end : src + forward * throw_offset) - forward * throw_back;
        math::vector3 velocity = forward * speed + player_velocity * velocity_inherit;

        const float dt = cstypes::tick_interval;
        const float g = gravity();
        const float fuse = detonate_time(def);
        const bool rests = def == smoke || def == decoy;
        const bool fire = def == molotov || def == incendiary;
        push(out, pos);
        for (float time = 0.f; time < fuse && out.count < max_points; time += dt)
        {
            const float new_z = velocity.z - g * dt;
            const math::vector3 move{ velocity.x * dt, velocity.y * dt, (velocity.z + new_z) * 0.5f * dt };
            velocity.z = new_z;
            const systems::tracing::result tr = systems::g_tracing.trace_line(pos, pos + move, pawn, cstypes::masks::world);
            if (!tr.ok)
                break;
            if (tr.fraction >= 1.f)
            {
                pos = pos + move;
                push(out, pos);
                continue;
            }
            const math::vector3 normal = tr.normal;
            pos = tr.end + normal * surface_offset;
            push(out, pos);
            if (out.bounce_count < max_bounces)
                out.bounces[out.bounce_count++] = pos;
            const float backoff = velocity.dot(normal) * 2.f;
            velocity = (velocity - normal * backoff) * elasticity;
            if (normal.z <= floor_normal)
                continue;
            if (fire)
                return;
            if (velocity.length_sqr() < rest_speed_sqr)
            {
                if (rests)
                    return;
                velocity = {};
            }
        }
    }

    void build(path& out)
    {
        out.valid = false;
        out.count = 0;
        out.bounce_count = 0;
        if (!settings::g_visuals.grenade_prediction || !systems::g_tracing.ready())
            return;
        const systems::local_player::data local = systems::g_local.get();
        if (!local.is_alive || !local.pawn || !local.weapon || !is_grenade(local.weapon_def) || !local.eye.is_valid())
            return;
        if (!read_view_matrix(out.matrix))
            return;
        const float strength = reads::field<float>(local.weapon, SCHEMA("C_BaseCSGrenade", "m_flThrowStrength"_hash), 1.f);
        float velocity = default_throw_velocity;
        if (const std::uint32_t offset = SCHEMA("CCSWeaponBaseVData", "m_flThrowVelocity"_hash))
        {
            const float value = reads::field<float>(local.weapon_vdata, offset, default_throw_velocity);
            if (std::isfinite(value) && value > min_throw_velocity && value <= max_throw_velocity)
                velocity = value;
        }
        const math::qangle view = systems::g_input.get_view_angles();
        if (!view.is_valid())
            return;
        simulate(out, local.pawn, local.weapon_def, local.eye, view, local.velocity, std::isfinite(strength) ? strength : 1.f, velocity);
        out.valid = out.count > 1;
    }

    bool project(const math::view_matrix& matrix, const math::vector3& world, const ImVec2& screen, ImVec2& out)
    {
        math::vector2 p{};
        if (!math::helpers::world_to_screen(matrix, world, screen.x, screen.y, p))
            return false;
        out = ImVec2(p.x, p.y);
        return true;
    }

    void copy_path(path& to, const path& from)
    {
        to.valid = from.valid;
        to.count = from.count;
        to.bounce_count = from.bounce_count;
        to.matrix = from.matrix;
        std::copy_n(from.points, from.count, to.points);
        std::copy_n(from.bounces, from.bounce_count, to.bounces);
    }
}

namespace features::visuals
{
    void grenade_prediction::on_frame_stage(int stage)
    {
        if (stage != cstypes::frame_stage::update)
            return;
        build(g_building);
        std::lock_guard lock(g_mutex);
        copy_path(g_path, g_building);
    }

    void grenade_prediction::on_present(ImDrawList* draw)
    {
        if (!draw || !settings::g_visuals.grenade_prediction)
            return;
        static path view{};
        {
            std::lock_guard lock(g_mutex);
            if (!g_path.valid)
                return;
            copy_path(view, g_path);
        }
        const ImVec2 screen = ImGui::GetIO().DisplaySize;
        if (screen.x < 1.f || screen.y < 1.f)
            return;
        const ImU32 color = static_cast<ImU32>(settings::g_visuals.grenade_color.abgr());
        ImVec2 previous{};
        bool has_previous = false;
        for (int i = 0; i < view.count; ++i)
        {
            ImVec2 p{};
            if (!project(view.matrix, view.points[i], screen, p))
            {
                has_previous = false;
                continue;
            }
            if (has_previous)
                draw->AddLine(previous, p, color, 2.f);
            previous = p;
            has_previous = true;
        }
        for (int i = 0; i < view.bounce_count; ++i)
        {
            ImVec2 p{};
            if (project(view.matrix, view.bounces[i], screen, p))
                draw->AddCircleFilled(p, 3.f, color, 12);
        }
        ImVec2 end{};
        if (project(view.matrix, view.points[view.count - 1], screen, end))
        {
            draw->AddCircle(end, 7.f, IM_COL32(0, 0, 0, 200), 24, 3.f);
            draw->AddCircle(end, 7.f, color, 24, 1.5f);
        }
    }
}
