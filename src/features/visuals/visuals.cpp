#include "visuals.h"
#include "../../core/settings.h"

namespace features::visuals
{
    bool any()
    {
        return settings::g_visuals.esp || settings::g_visuals.fov_circle || settings::g_visuals.spread_circle || settings::g_visuals.grenade_prediction;
    }
}
