#pragma once
#include "../core/math.h"
#include <cstdint>

namespace systems
{
    namespace prediction
    {
        struct state
        {
            bool valid = false;
            std::uint32_t flags = 0;
            std::uint8_t move_type = 0;
            math::vector3 networked_velocity{};
            math::vector3 networked_origin{};
            math::vector3 eye{};
            math::vector2 last_movement_impulses{};
            float surface_friction = 1.f;
            float stamina = 0.f;
            float max_speed = 250.f;
            float gravity_scale = 1.f;
            float duck_amount = 0.f;
            int tick_base = 0;
            int tick_count = 0;
        };
    }

    class prediction_system
    {
    public:
        void update(std::uintptr_t controller, std::uintptr_t pawn, const math::vector2& last_impulses);
        const prediction::state& pre() const { return m_pre; }
        void invalidate() { m_pre = {}; }

    private:
        prediction::state m_pre{};
    };

    inline prediction_system g_prediction{};
}
