#include "movement.h"
#include "movement_detail.h"
#include "../../core/convar.h"
#include "../../core/cstypes.h"
#include "../../core/keys.h"
#include "../../core/memory.h"
#include "../../core/schema.h"
#include "../../core/settings.h"
#include "../../systems/game_reads.h"
#include "../../systems/local.h"
#include "../../systems/prediction.h"
#include "../../systems/tracing.h"
#include <algorithm>
#include <cmath>

namespace
{
    constexpr std::uintptr_t services_owner = 0x38;
    constexpr std::uintptr_t pawn_trace_mask = 0xD48;
    constexpr std::uintptr_t pawn_trace_flags = 0x3F8;
    constexpr std::uint32_t trace_flags_bit = 0x10;
    constexpr std::uint64_t trace_mask_extra = 0x20;
    constexpr int movement_trace_layer = 11;
    constexpr float standing_height = 72.f;
    constexpr float ground_probe = 2.f;
    constexpr float default_gravity = 800.f;
    constexpr float default_standable_normal = 0.7f;
    constexpr float min_when = 0.001f;
    constexpr float max_when = 0.99f;
    constexpr int jumpbug_steps = 4;
    constexpr float hull_limit = 512.f;

    bool read_hull(std::uintptr_t pawn, math::vector3& mins, math::vector3& maxs)
    {
        const std::uint32_t collision = SCHEMA("C_BaseModelEntity", "m_Collision"_hash);
        const std::uint32_t mins_offset = SCHEMA("CCollisionProperty", "m_vecMins"_hash);
        const std::uint32_t maxs_offset = SCHEMA("CCollisionProperty", "m_vecMaxs"_hash);
        if (!pawn || !collision || !mins_offset || !maxs_offset)
            return false;
        const std::uintptr_t base = pawn + collision;
        if (!systems::reads::readable(base + mins_offset, sizeof(math::vector3)) || !systems::reads::readable(base + maxs_offset, sizeof(math::vector3)))
            return false;
        mins = memory::read<math::vector3>(base + mins_offset);
        maxs = memory::read<math::vector3>(base + maxs_offset);
        return systems::reads::sane(mins, hull_limit) && systems::reads::sane(maxs, hull_limit) && maxs.z > mins.z;
    }

    std::uint64_t movement_trace_mask(std::uintptr_t pawn, std::uintptr_t movement_services)
    {
        const std::uintptr_t owner = systems::reads::pointer(movement_services + services_owner);
        const std::uintptr_t source = owner ? owner : pawn;
        if (!systems::reads::readable(source + pawn_trace_mask, sizeof(std::uint64_t)))
            return 0;
        std::uint64_t mask = memory::read<std::uint64_t>(source + pawn_trace_mask);
        if (!owner || (systems::reads::value<std::uint32_t>(source + pawn_trace_flags) & trace_flags_bit))
            mask |= trace_mask_extra;
        return mask;
    }
}

namespace features::movement
{
    std::optional<detail::landing> detail::trace_landing(std::uintptr_t pawn, std::uintptr_t movement_services, const systems::prediction::state& prestate, bool holding_duck)
    {
        if (!pawn || !movement_services || !prestate.valid || !systems::g_tracing.ready())
            return std::nullopt;
        if (!prestate.networked_velocity.is_valid() || !prestate.networked_origin.is_valid())
            return std::nullopt;
        if (prestate.networked_velocity.z > 0.f)
            return std::nullopt;

        math::vector3 mins{};
        math::vector3 maxs{};
        if (!read_hull(pawn, mins, maxs))
            return std::nullopt;

        math::vector3 trace_origin = prestate.networked_origin;
        if (holding_duck && prestate.duck_amount > 0.f)
        {
            trace_origin.z -= (standing_height - maxs.z) * 0.5f;
            maxs.z = standing_height;
        }

        const std::uint64_t mask = movement_trace_mask(pawn, movement_services);
        if (!mask)
            return std::nullopt;

        const systems::tracing::filter filter = systems::g_tracing.make_player_movement_filter(pawn, mask, movement_trace_layer);
        if (!filter.valid)
            return std::nullopt;

        const float gravity = convar_positive(CONVAR("sv_gravity"), default_gravity) * prestate.gravity_scale;
        float standable = convar_float(CONVAR("sv_standable_normal"), default_standable_normal);
        if (standable <= 0.f || standable > 1.f)
            standable = default_standable_normal;

        math::vector3 velocity = prestate.networked_velocity;
        velocity.z -= gravity * cstypes::tick_interval * 0.5f;

        math::vector3 trace_end = trace_origin + velocity * cstypes::tick_interval;
        trace_end.z -= ground_probe;

        const systems::tracing::result result = systems::g_tracing.trace_player_bbox(trace_origin, trace_end, { mins, maxs }, filter, movement_services);
        if (!result.ok || !std::isfinite(result.fraction) || result.fraction <= 0.f || result.fraction >= 1.f)
            return std::nullopt;
        if (!result.normal.is_valid() || result.normal.z < standable)
            return std::nullopt;

        return landing{ result.fraction, result.normal, velocity };
    }

    void jumpbug::on_create_move(systems::input::frame& frame)
    {
        using namespace cstypes::command_buttons;

        m_active_this_tick = false;

        if (!frame.valid() || !keys::active(settings::g_movement.jumpbug, settings::g_movement.jumpbug_key))
        {
            m_ducking = false;
            m_release_jump = false;
            return;
        }

        if (m_release_jump)
        {
            m_release_jump = false;
            if (!frame.really_held(in_jump) && detail::jump_down(frame))
                frame.release(in_jump, 0.f);
        }

        const auto local = systems::g_local.get();
        if (!local.pawn || !local.is_alive)
            return;

        const auto& prestate = systems::g_prediction.pre();
        if (!prestate.valid)
            return;

        if (detail::ladder_or_noclip(detail::move_type(local.pawn, prestate)))
        {
            m_ducking = false;
            return;
        }

        if (detail::on_ground(prestate))
        {
            if (m_ducking && !frame.really_held(in_duck))
                frame.release(in_duck, 0.f);
            m_ducking = false;
            return;
        }

        if (prestate.networked_velocity.z > 0.f)
            return;

        const std::uintptr_t services = detail::movement_services(local.pawn);
        if (!services)
            return;

        if (systems::input::max_steps - frame.step_count() < jumpbug_steps)
            return;

        if (!frame.held(in_duck))
            frame.press(in_duck, 0.f);
        m_ducking = true;

        const auto landing = detail::trace_landing(local.pawn, services, prestate, true);
        if (!landing || landing->normal.z < default_standable_normal)
            return;

        const float when = std::clamp(landing->fraction, min_when, max_when);
        m_landing_fraction = when;
        m_active_this_tick = true;

        frame.release(in_duck, when);
        if (detail::jump_down(frame))
            frame.release(in_jump, std::max(min_when, when - detail::subtick * 0.5f));
        frame.press(in_jump, when);
        m_release_jump = true;
        m_ducking = false;
    }
}
