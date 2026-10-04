#include "visuals.h"
#include "../../core/settings.h"

namespace features::visuals
{
    bool any()
    {
        return settings::g_visuals.esp || settings::g_visuals.fov_circle;
    }
}
