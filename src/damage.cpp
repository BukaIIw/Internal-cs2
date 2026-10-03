#include "damage.h"
#include "game.h"
#include "mem.h"
#include "features/hitbox.h"
#include "core/patterns.h"
#include <cmath>

namespace
{
    uint32_t vDamage = 0, vRange = 0, vRangeModifier = 0, vHeadshot = 0, vArmorRatio = 0;
    bool ready = false;

    bool Finite(float v, float lo, float hi)
    {
        return std::isfinite(v) && v >= lo && v <= hi;
    }

    bool Armored(int group, const damage::Victim& v)
    {
        if (v.armor <= 0)
            return false;
        switch (group)
        {
        case Hitboxes::Head: return v.helmet;
        case Hitboxes::LeftLeg:
        case Hitboxes::RightLeg: return false;
        default: return true;
        }
    }
}

bool damage::Init()
{
    mem::Module client = mem::Load("client.dll");
    if (!client)
        return false;
    auto field = [&](const char* name, const char* pattern) -> uint32_t
    {
        uint8_t* p = patterns::Find(client, name, pattern);
        return p ? *reinterpret_cast<uint32_t*>(p + 11) : 0;
    };
    vDamage = field("VData::m_nDamage", "C7 44 24 40 73 7F 05 57 4C 8D 86 ? ? ? ?");
    vHeadshot = field("VData::m_flHeadshotMultiplier", "C7 44 24 40 7C 13 92 AC 4C 8D 86 ? ? ? ?");
    vArmorRatio = field("VData::m_flArmorRatio", "C7 44 24 40 14 9E 31 FA 4C 8D 86 ? ? ? ?");
    vRange = field("VData::m_flRange", "C7 44 24 40 EB B4 BA 42 4C 8D 86 ? ? ? ?");
    vRangeModifier = field("VData::m_flRangeModifier", "C7 44 24 40 0C 6B F7 67 4C 8D 86 ? ? ? ?");
    ready = vDamage && vRangeModifier;
    return ready;
}

bool damage::Ready()
{
    return ready;
}

bool damage::Read(void* weapon, Weapon& out)
{
    out = Weapon{};
    if (!ready || !weapon)
        return false;
    void* vdata = game::CurrentVData(weapon);
    if (!vdata)
        return false;
    out.damage = static_cast<float>(mem::At<int>(vdata, vDamage));
    out.rangeModifier = mem::At<float>(vdata, vRangeModifier);
    if (vRange)
        out.range = mem::At<float>(vdata, vRange);
    if (vHeadshot)
        out.headshot = mem::At<float>(vdata, vHeadshot);
    if (vArmorRatio)
        out.armorRatio = mem::At<float>(vdata, vArmorRatio);
    if (!Finite(out.damage, 1.f, 1000.f) || !Finite(out.rangeModifier, 0.01f, 1.f))
        return false;
    if (!Finite(out.range, 1.f, 65536.f))
        out.range = 8192.f;
    if (!Finite(out.headshot, 0.f, 10.f))
        out.headshot = 4.f;
    if (!Finite(out.armorRatio, 0.f, 2.f))
        out.armorRatio = 1.f;
    return true;
}

damage::Victim damage::Read(void* pawn)
{
    Victim v;
    if (!pawn)
        return v;
    v.health = mem::At<int>(pawn, game::off.health);
    v.armor = game::off.armor ? mem::At<int>(pawn, game::off.armor) : 0;
    if (game::off.itemServices && game::off.hasHelmet)
        if (void* services = mem::At<void*>(pawn, game::off.itemServices))
            v.helmet = mem::At<bool>(services, game::off.hasHelmet);
    return v;
}

float damage::Scale(const Weapon& w, const Victim& v, int group, float distance)
{
    if (distance > w.range)
        return 0.f;
    float dmg = w.damage * std::pow(w.rangeModifier, distance / 500.f);
    switch (group)
    {
    case Hitboxes::Head: dmg *= w.headshot; break;
    case Hitboxes::Stomach: dmg *= 1.25f; break;
    case Hitboxes::LeftLeg:
    case Hitboxes::RightLeg: dmg *= 0.75f; break;
    default: break;
    }
    if (Armored(group, v))
    {
        constexpr float kBonus = 0.5f;
        const float ratio = w.armorRatio * 0.5f;
        float health = dmg * ratio;
        float armor = (dmg - health) * kBonus;
        if (armor > static_cast<float>(v.armor))
        {
            armor = static_cast<float>(v.armor);
            health = dmg - armor / kBonus;
        }
        dmg = health;
    }
    return dmg;
}

float damage::Required(const Victim& v, float minimum)
{
    const float min = minimum < 1.f ? 1.f : minimum;
    return v.health > 0 && static_cast<float>(v.health) < min ? static_cast<float>(v.health) : min;
}
