#pragma once
#include "../game.h"

class Hitboxes
{
public:
    enum Group
    {
        Generic,
        Head,
        Chest,
        Stomach,
        LeftArm,
        RightArm,
        LeftLeg,
        RightLeg,
        Neck,
        GroupCount
    };

    struct Box
    {
        int index = -1;
        int group = 0;
        int bone = -1;
        bool capsule = false;
        float radius = 0.f;
        game::Vec3 a{}, b{}, center{};
        game::Vec3 origin{};
        game::Vec3 axis[3]{};
        game::Vec3 mins{}, maxs{};
    };

    static constexpr int kMax = 32;
    static constexpr int kMaxPoints = 8;

    inline static bool head = true;
    inline static bool neck = false;
    inline static bool chest = true;
    inline static bool stomach = true;
    inline static bool arms = false;
    inline static bool legs = false;
    inline static bool multipoint = true;
    inline static float headScale = 0.7f;
    inline static float bodyScale = 0.6f;
    inline static bool preferBody = false;

    static bool Init();
    static bool Ready();
    static void Register();

    static int Collect(void* pawn, Box* out, int max);
    static bool Enabled(int group);
    static int Points(const Box& box, const game::Vec3& eye, game::Vec3* out, int max);
    static bool Hit(const Box& box, const game::Vec3& from, const game::Vec3& dir, float range);
    static int HitAny(const Box* boxes, int count, const game::Vec3& from, const game::Vec3& dir, float range);
    static const char* GroupName(int group);
};
