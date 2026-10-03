#pragma once
#include "attack.h"
#include "hitbox.h"
#include <cstdint>

class Spread
{
public:
    struct Weapon
    {
        void* entity = nullptr;
        int def = 0;
        int bullets = 1;
        int mode = 0;
        AttackType attack = AttackType::Primary;
        float inaccuracy = 0.f;
        float spread = 0.f;
        float recoilIndex = 0.f;
        float range = 8192.f;
    };

    struct Shot
    {
        float pitch = 0.f, yaw = 0.f;
        float punchPitch = 0.f, punchYaw = 0.f;
        int tick = 0;
    };

    enum Mode
    {
        ModeOff,
        ModeChance,
        ModeExact
    };

    inline static int mode = ModeChance;
    inline static float minChance = 65.f;
    inline static int samples = 128;

    static bool Init();
    static bool Ready();
    static bool SeedReady();
    static void Register();

    static bool Read(void* weapon, Weapon& out);
    static int Tick(int fallback, AttackType attack = AttackType::Primary);
    static uint32_t Seed(float pitch, float yaw, int tick);
    static int Offsets(const Weapon& w, uint32_t seed, float* x, float* y, int max);
    static void Sample(const Weapon& w, int index, int count, float& x, float& y);
    static game::Vec3 Direction(float pitch, float yaw, float x, float y);

    static float Chance(const game::Vec3& eye, const Shot& shot, const Weapon& w, const Hitboxes::Box* boxes, int count);
    static int ExactHits(const game::Vec3& eye, const Shot& shot, const Weapon& w, const Hitboxes::Box* boxes, int count);
    static bool Allow(const game::Vec3& eye, const Shot& shot, const Weapon& w, const Hitboxes::Box* boxes, int count);

    inline static float lastChance = 0.f;
    inline static int lastExact = -1;
    inline static int lastTick = 0;
};
