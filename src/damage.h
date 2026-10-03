#pragma once

namespace damage
{
    struct Weapon
    {
        float damage = 0.f;
        float range = 8192.f;
        float rangeModifier = 1.f;
        float headshot = 4.f;
        float armorRatio = 1.f;
    };

    struct Victim
    {
        int health = 0;
        int armor = 0;
        bool helmet = false;
    };

    bool Init();
    bool Ready();
    bool Read(void* weapon, Weapon& out);
    Victim Read(void* pawn);
    float Scale(const Weapon& w, const Victim& v, int group, float distance);
    float Required(const Victim& v, float minimum);
}
