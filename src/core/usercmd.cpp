#include "usercmd.h"
#include <cmath>

namespace
{
    constexpr float kRad = 3.14159265f / 180.f;
    constexpr uint32_t kHasAttack1 = 1u << 5;
    constexpr uint32_t kBaseHasButtons = 1u << 1;

    float Normalize(float a)
    {
        a = std::fmod(a + 180.f, 360.f);
        return (a < 0.f ? a + 360.f : a) - 180.f;
    }

    void Write(usercmd::QAngle* a, float pitch, float yaw)
    {
        if (!a)
            return;
        a->x = pitch > 89.f ? 89.f : pitch < -89.f ? -89.f : pitch;
        a->y = Normalize(yaw);
        a->z = 0.f;
        a->has |= 1 | 2;
    }
}

bool usercmd::Attacking(const Cmd* cmd)
{
    if (!cmd)
        return false;
    if ((cmd->buttons.value | cmd->buttons.changed) & kAttack)
        return true;
    return cmd->base && cmd->base->buttons && (cmd->base->buttons->state1 & kAttack);
}

void usercmd::Attack(Cmd* cmd)
{
    if (!cmd)
        return;
    cmd->buttons.value |= kAttack;
    cmd->buttons.changed |= kAttack;
    if (cmd->base && cmd->base->buttons)
    {
        ButtonsPB* b = cmd->base->buttons;
        b->state1 |= kAttack;
        b->state2 |= kAttack;
        b->has |= 1 | 2;
        cmd->base->has |= kBaseHasButtons;
    }
    if (cmd->attack1 < 0 && cmd->history.size > 0)
    {
        cmd->attack1 = 0;
        cmd->has |= kHasAttack1;
    }
}

bool usercmd::ViewAngles(const Cmd* cmd, float& pitch, float& yaw)
{
    if (!cmd || !cmd->base || !cmd->base->angles)
        return false;
    pitch = cmd->base->angles->x;
    yaw = cmd->base->angles->y;
    return true;
}

bool usercmd::SetAngles(Cmd* cmd, float pitch, float yaw)
{
    if (!cmd || !cmd->base || !cmd->base->angles)
        return false;
    Base* base = cmd->base;
    const float viewYaw = base->angles->y;
    Write(base->angles, pitch, yaw);
    const HistoryField& h = cmd->history;
    if (h.rep)
        for (int i = 0; i < h.size && i < h.rep->allocated; ++i)
            if (History* e = h.rep->items[i])
                if (e->has & 1)
                    Write(e->angles, pitch, yaw);
    const float d = Normalize(viewYaw - yaw) * kRad;
    const float f = base->forward, l = base->left;
    if (f != 0.f || l != 0.f)
    {
        base->forward = f * std::cos(d) - l * std::sin(d);
        base->left = f * std::sin(d) + l * std::cos(d);
    }
    return true;
}
