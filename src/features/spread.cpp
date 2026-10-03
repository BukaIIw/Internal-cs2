#include "spread.h"
#include "schema.h"
#include "vecmath.h"
#include "../core/events.h"
#include "../core/patterns.h"
#include "../core/settings.h"
#include "../mem.h"
#include <Windows.h>
#include <cmath>

namespace
{
    using namespace vm;

    using SeedFn = uint32_t(__fastcall*)(void*, const float*, int);
    using CalcFn = void(__fastcall*)(uint16_t, int, int, uint32_t, float, float, float, float*, float*);

    using InaccuracyFn = float(__fastcall*)(void*, float*, float*);
    using SpreadFn = float(__fastcall*)(void*);

    using ItemSchemaFn = void*(__fastcall*)();
    using ItemByNameFn = void*(__fastcall*)(void*, const char*);

    enum Special
    {
        Revolver,
        Negev,
        SpecialCount
    };

    ItemSchemaFn itemSchema = nullptr;
    uint32_t itemByName = 0;
    uint8_t itemDefIndex = 0;
    const char** specialName[SpecialCount]{};
    int specialDef[SpecialCount]{ -1, -1 };
    constexpr int kMaxBullets = 16;

    SeedFn seedFn = nullptr;
    CalcFn calcFn = nullptr;
    mem::Module client;
    uint32_t weaponMode = 0, recoilIndex = 0, numBullets = 0, weaponRange = 0;
    bool ready = false;

    constexpr int kMaxEntries = 256;
    uint32_t historyCount = 0, historyData = 0, entryStride = 0, playerTick = 0;
    uint32_t attackIndex[Attack::kCount]{};
    int inaccuracyIndex = -1, spreadIndex = -1;
    void* input = nullptr;

    uint32_t Disp(uint8_t* code, int at)
    {
        return code ? *reinterpret_cast<uint32_t*>(code + at) : 0;
    }

    uint8_t Disp8(uint8_t* code, int at)
    {
        return code ? code[at] : 0;
    }

    void OnCreateMove(int, void* data)
    {
        input = data;
    }

    int HistoryTick(AttackType attack)
    {
        __try
        {
            const int count = mem::At<int>(input, historyCount);
            auto data = mem::At<uint8_t*>(input, historyData);
            if (!data || count <= 0 || count > kMaxEntries)
                return -1;
            const int slot = Attack::Index(attack);
            int index = slot >= 0 && attackIndex[slot] ? mem::At<int>(input, attackIndex[slot]) : -1;
            if (index < 0 || index >= count)
                index = count - 1;
            return *reinterpret_cast<int*>(data + index * entryStride + playerTick);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return -1;
        }
    }

    struct Rng
    {
        uint32_t s;
        float Next()
        {
            s ^= s << 13;
            s ^= s >> 17;
            s ^= s << 5;
            return (s >> 8) * (1.f / 16777216.f);
        }
    };

    template <typename T>
    T Rel(uint8_t* code, int at, int end)
    {
        return reinterpret_cast<T>(code + end + *reinterpret_cast<int32_t*>(code + at));
    }

    int ItemDef(const char* name)
    {
        __try
        {
            void* schema = itemSchema();
            if (!schema)
                return -1;
            auto vt = *reinterpret_cast<uint8_t**>(schema);
            auto fn = *reinterpret_cast<ItemByNameFn*>(vt + itemByName);
            void* def = fn(schema, name);
            return def ? *reinterpret_cast<uint16_t*>(static_cast<uint8_t*>(def) + itemDefIndex) : -1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return -1;
        }
    }

    bool Is(int def, Special s)
    {
        if (specialDef[s] < 0 && itemSchema && specialName[s] && *specialName[s])
            specialDef[s] = ItemDef(*specialName[s]);
        return specialDef[s] >= 0 && def == specialDef[s];
    }

    float Shape(const Spread::Weapon& w, float r)
    {
        if (Is(w.def, Revolver) && w.mode == static_cast<int>(WeaponMode::Secondary))
            return 1.f - r * r;
        if (Is(w.def, Negev) && w.recoilIndex < 3.f)
        {
            int e = 3;
            do
            {
                --e;
                r *= r;
            } while (static_cast<float>(e) > w.recoilIndex);
            return 1.f - r;
        }
        return r;
    }

    bool InClient(void* object, int index)
    {
        if (!object || index < 0)
            return false;
        auto vt = *reinterpret_cast<void***>(object);
        return vt && client.Contains(vt) && client.Contains(vt[index]);
    }

    bool Finite(float v, float lo, float hi)
    {
        return std::isfinite(v) && v >= lo && v <= hi;
    }

}

bool Spread::Init()
{
    client = mem::Load("client.dll");
    if (!client)
        return false;
    seedFn = reinterpret_cast<SeedFn>(patterns::Find(client, "SpreadSeed", "48 89 5C 24 ? 57 48 81 EC ? ? ? ? F3 0F 10 0A"));
    calcFn = reinterpret_cast<CalcFn>(patterns::Find(client, "CalcSpread", "48 8B C4 48 89 58 18 48 89 68 20 89 50 10 56 57 41 54 41 56 41 57 48 81 EC ? ? ? ? 44 8B 15"));
    if (auto code = patterns::Find(client, "SpreadSpecialWeapons", "E8 ? ? ? ? 8B 58 34 39 1D ? ? ? ? 74 ? E8 ? ? ? ? 48 8B 15 ? ? ? ? 48 8B 08 4C 8B 81 ? ? ? ? 48 8B C8 41 FF D0 48 89 05 ? ? ? ? 89 1D ? ? ? ? EB ? 48 8B 05 ? ? ? ? 66 3B 70 ? 75 ? 83 FD 01 75 ? 0F 28 C7 F3 0F 59 C7 41 0F 28 FA F3 0F 5C F8 EB ? E8 ? ? ? ? 8B 58 34 39 1D ? ? ? ? 74 ? E8 ? ? ? ? 48 8B 15 ? ? ? ?"))
    {
        itemSchema = Rel<ItemSchemaFn>(code, 1, 5);
        itemByName = Disp(code, 34);
        itemDefIndex = Disp8(code, 69);
        specialName[Revolver] = Rel<const char**>(code, 24, 28);
        specialName[Negev] = Rel<const char**>(code, 118, 122);
    }
    weaponMode = Disp(patterns::Find(client, "C_CSWeaponBase::m_weaponMode", "89 B3 ? ? ? ? 89 B3 20 1A 00 00 40 38 B3 2C 1A 00 00"), 2);
    recoilIndex = Disp(patterns::Find(client, "C_CSWeaponBase::m_flRecoilIndex", "E8 ? ? ? ? 48 8D 8B 1C 1A 00 00 E8 ? ? ? ? 48 8D 8B ? ? ? ?"), 20);
    numBullets = Disp(patterns::Find(client, "CCSWeaponBaseVData::m_nNumBullets", "89 8B ? ? ? ? 88 8B 34 07 00 00 48 89 8B 38 07 00 00"), 2);
    weaponRange = Disp(patterns::Find(client, "CCSWeaponBaseVData::m_flRange", "C7 44 24 40 EB B4 BA 42 4C 8D 86 ? ? ? ?"), 11);
    if (auto code = patterns::Find(client, "WeaponSpreadCalls", "FF 90 ? ? ? ? 48 8B 06 45 33 C0 33 D2 48 8B CE 0F 28 F8 FF 90"))
    {
        spreadIndex = static_cast<int>(Disp(code, 2) / sizeof(void*));
        inaccuracyIndex = static_cast<int>(Disp(code, 22) / sizeof(void*));
    }
    historyCount = Disp(patterns::Find(client, "InputHistoryCount", "48 63 97 ? ? ? ? 41 BE 01 00 00 00"), 3);
    if (auto code = patterns::Find(client, "InputHistoryData", "48 8D 0C 40 48 C1 E1 ? 48 03 8F ? ? ? ? 39 69 50"))
    {
        entryStride = 3u << Disp8(code, 7);
        historyData = Disp(code, 11);
    }
    playerTick = Disp8(patterns::Find(client, "InputHistoryPlayerTick", "8B 47 ? 0F BA E9 0B 89 4A 10"), 2);
    attackIndex[Attack::Index(AttackType::Primary)] = Disp(patterns::Find(client, "InputAttack1Index", "49 63 87 ? ? ? ? 4D 8D 4D 20"), 3);
    attackIndex[Attack::Index(AttackType::Secondary)] = Disp(patterns::Find(client, "InputAttack2Index", "49 63 87 ? ? ? ? 83 F8 FF 0F 84"), 3);
    if (historyCount && historyData && entryStride && playerTick)
        events::Subscribe(events::CreateMove, "Spread tick", OnCreateMove);
    ready = inaccuracyIndex > 0 && spreadIndex > 0;
    return ready && SeedReady();
}

bool Spread::Ready()
{
    return ready;
}

bool Spread::SeedReady()
{
    return seedFn && calcFn;
}

void Spread::Register()
{
    settings::Int("spread.mode", &mode);
    settings::Float("spread.min_chance", &minChance);
    settings::Int("spread.samples", &samples);
}

bool Spread::Read(void* weapon, Weapon& out)
{
    if (!ready || !InClient(weapon, inaccuracyIndex) || !InClient(weapon, spreadIndex))
        return false;
    out.entity = weapon;
    out.def = mem::At<uint16_t>(static_cast<uint8_t*>(weapon) + game::off.attributeManager + game::off.item, game::off.defIndex);
    out.mode = weaponMode ? mem::At<int>(weapon, weaponMode) : static_cast<int>(WeaponMode::Primary);
    out.attack = AttackType::Primary;
    out.recoilIndex = recoilIndex ? mem::At<float>(weapon, recoilIndex) : 0.f;
    out.bullets = 1;
    out.range = 8192.f;
    if (void* vdata = game::CurrentVData(weapon))
    {
        if (numBullets)
            out.bullets = mem::At<int>(vdata, numBullets);
        if (weaponRange)
            out.range = mem::At<float>(vdata, weaponRange);
    }
    out.bullets = out.bullets < 1 ? 1 : out.bullets > kMaxBullets ? kMaxBullets : out.bullets;
    if (!Finite(out.range, 1.f, 65536.f))
        out.range = 8192.f;
    auto vt = *reinterpret_cast<void***>(weapon);
    out.inaccuracy = reinterpret_cast<InaccuracyFn>(vt[inaccuracyIndex])(weapon, nullptr, nullptr);
    out.spread = reinterpret_cast<SpreadFn>(vt[spreadIndex])(weapon);
    if (!Finite(out.inaccuracy, 0.f, 10.f) || !Finite(out.spread, 0.f, 10.f))
        return false;
    out.inaccuracy = out.inaccuracy > 1.f ? 1.f : out.inaccuracy;
    return true;
}

int Spread::Tick(int fallback, AttackType attack)
{
    const int tick = input && historyCount && historyData && entryStride && playerTick ? HistoryTick(attack) : -1;
    lastTick = tick > 0 ? tick : fallback;
    return lastTick;
}

uint32_t Spread::Seed(float pitch, float yaw, int tick)
{
    if (!seedFn)
        return 0;
    const float angles[3]{ pitch, yaw, 0.f };
    return seedFn(nullptr, angles, tick);
}

int Spread::Offsets(const Weapon& w, uint32_t seed, float* x, float* y, int max)
{
    if (!calcFn || max <= 0)
        return 0;
    float xs[kMaxBullets]{}, ys[kMaxBullets]{};
    const int bullets = w.bullets > kMaxBullets ? kMaxBullets : w.bullets;
    calcFn(static_cast<uint16_t>(w.def), bullets, w.mode, seed + 1, w.inaccuracy, w.spread, w.recoilIndex, xs, ys);
    const int n = bullets < max ? bullets : max;
    for (int i = 0; i < n; ++i)
    {
        x[i] = xs[i];
        y[i] = ys[i];
    }
    return n;
}

void Spread::Sample(const Weapon& w, int index, int count, float& x, float& y)
{
    Rng rng{ static_cast<uint32_t>(index) * 2654435761u + 0x9E3779B9u };
    rng.Next();
    const int side = static_cast<int>(std::sqrt(static_cast<float>(count < 1 ? 1 : count)));
    const int k = side < 1 ? 1 : side;
    const float u1 = ((index % k) + rng.Next()) / k;
    const float u2 = (((index / k) % k) + rng.Next()) / k;
    const float r1 = Shape(w, u1) * w.inaccuracy;
    const float a1 = u2 * 2.f * kPi;
    const float r3 = Shape(w, rng.Next()) * w.spread;
    const float a3 = rng.Next() * 2.f * kPi;
    x = std::cos(a1) * r1 + std::cos(a3) * r3;
    y = std::sin(a1) * r1 + std::sin(a3) * r3;
}

game::Vec3 Spread::Direction(float pitch, float yaw, float x, float y)
{
    Vec3 f, r, u;
    Basis(pitch, yaw, f, r, u);
    return Normalized(f + r * x + u * y);
}

float Spread::Chance(const game::Vec3& eye, const Shot& shot, const Weapon& w, const Hitboxes::Box* boxes, int count)
{
    if (count <= 0)
        return 0.f;
    const int n = samples < 16 ? 16 : samples > 512 ? 512 : samples;
    const float pitch = shot.pitch + shot.punchPitch, yaw = shot.yaw + shot.punchYaw;
    int hits = 0;
    for (int i = 0; i < n; ++i)
    {
        float x, y;
        Sample(w, i, n, x, y);
        if (Hitboxes::HitAny(boxes, count, eye, Direction(pitch, yaw, x, y), w.range) >= 0)
            ++hits;
    }
    lastChance = hits * 100.f / n;
    return lastChance;
}

int Spread::ExactHits(const game::Vec3& eye, const Shot& shot, const Weapon& w, const Hitboxes::Box* boxes, int count)
{
    if (!SeedReady() || count <= 0)
        return -1;
    float x[kMaxBullets], y[kMaxBullets];
    const int n = Offsets(w, Seed(shot.pitch, shot.yaw, Tick(shot.tick, w.attack)), x, y, kMaxBullets);
    const float pitch = shot.pitch + shot.punchPitch, yaw = shot.yaw + shot.punchYaw;
    int hits = 0;
    for (int i = 0; i < n; ++i)
        if (Hitboxes::HitAny(boxes, count, eye, Direction(pitch, yaw, x[i], y[i]), w.range) >= 0)
            ++hits;
    lastExact = hits;
    return hits;
}

bool Spread::Allow(const game::Vec3& eye, const Shot& shot, const Weapon& w, const Hitboxes::Box* boxes, int count)
{
    switch (mode)
    {
    case ModeChance:
        return Spread::Chance(eye, shot, w, boxes, count) >= minChance;
    case ModeExact:
        if (SeedReady())
            return ExactHits(eye, shot, w, boxes, count) > 0;
        return Spread::Chance(eye, shot, w, boxes, count) >= minChance;
    default:
        return true;
    }
}
