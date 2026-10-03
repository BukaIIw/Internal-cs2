#include "hitbox.h"
#include "schema.h"
#include "vecmath.h"
#include "../core/patterns.h"
#include "../core/settings.h"
#include "../mem.h"
#include <cmath>

namespace
{
    using namespace vm;

    using HitboxSetFn = void*(__fastcall*)(void*, int);
    using BoneIndexFn = int(__fastcall*)(void*, const void*, int);

    HitboxSetFn hitboxSet = nullptr;
    BoneIndexFn boneIndex = nullptr;
    uint32_t modelHandle = 0;
    uint32_t setList = 0;
    uint32_t minBounds = 0, maxBounds = 0, shapeRadius = 0, groupId = 0;
    int boxSize = 0;
    bool ready = false;

    uint32_t boneArray = 0;
    int boneStride = 0;
    constexpr int kMaxBone = 256;

    float SegmentDistance(const Vec3& p0, const Vec3& p1, const Vec3& q0, const Vec3& q1)
    {
        const Vec3 d1 = p1 - p0, d2 = q1 - q0, r = p0 - q0;
        const float a = Dot(d1, d1), e = Dot(d2, d2), f = Dot(d2, r);
        float s = 0.f, t = 0.f;
        if (a <= 1e-6f && e <= 1e-6f)
            return Length(p0 - q0);
        if (a <= 1e-6f)
            t = std::fmin(std::fmax(f / e, 0.f), 1.f);
        else
        {
            const float c = Dot(d1, r);
            if (e <= 1e-6f)
                s = std::fmin(std::fmax(-c / a, 0.f), 1.f);
            else
            {
                const float b = Dot(d1, d2), denom = a * e - b * b;
                s = denom != 0.f ? std::fmin(std::fmax((b * f - c * e) / denom, 0.f), 1.f) : 0.f;
                t = (b * s + f) / e;
                if (t < 0.f)
                {
                    t = 0.f;
                    s = std::fmin(std::fmax(-c / a, 0.f), 1.f);
                }
                else if (t > 1.f)
                {
                    t = 1.f;
                    s = std::fmin(std::fmax((b - c) / a, 0.f), 1.f);
                }
            }
        }
        return Length((p0 + d1 * s) - (q0 + d2 * t));
    }

    bool Slab(float origin, float dir, float lo, float hi, float& tmin, float& tmax)
    {
        if (std::fabs(dir) < 1e-6f)
            return origin >= lo && origin <= hi;
        float t0 = (lo - origin) / dir, t1 = (hi - origin) / dir;
        if (t0 > t1)
        {
            const float t = t0;
            t0 = t1;
            t1 = t;
        }
        tmin = std::fmax(tmin, t0);
        tmax = std::fmin(tmax, t1);
        return tmin <= tmax;
    }
}

bool Hitboxes::Init()
{
    mem::Module client = mem::Load("client.dll");
    if (!client)
        return false;
    hitboxSet = reinterpret_cast<HitboxSetFn>(patterns::Find(client, "GetHitboxSet", "48 89 5C 24 ? 48 89 74 24 ? 57 48 81 EC ? ? ? ? 8B DA 48 8B F9 E8 ? ? ? ? 48 8B F0 48 85 C0 0F 84"));
    boneIndex = reinterpret_cast<BoneIndexFn>(patterns::Find(client, "CModel::GetBoneIndexForHitbox", "48 89 5C 24 10 48 89 6C 24 18 56 48 83 EC 50 49 63 E8 48 8B F1 4C 8B C2 39 A9"));
    modelHandle = schema::Field("client.dll", "CModelState::m_hModel", "4D 8D BE ? ? ? ? C7 44 24 64 FF FF FF FF E8", 3);
    const char* anim = "animationsystem.dll";
    if (auto code = schema::Code(anim, "CHitBoxSet::m_HitBoxes", "33 F6 FF 47 ? 48 8D 15 ? ? ? ? 49 6B DC ? 48 03 5F ? 48 8B CB 48 89 33"))
    {
        setList = code[4];
        boxSize = code[15];
    }
    if (auto code = schema::Code(anim, "CHitBox::fields", "F2 43 0F 10 44 2E ? F2 0F 11 43 ? 43 8B 44 2E ? 89 43 ? F2 43 0F 10 44 2E ? F2 0F 11 43 ? 43 8B 44 2E ? 89 43 ? 43 8B 44 2E ? 89 43 ? 43 8B 44 2E ? 89 43 ? 43 8B 44 2E ? 89 43 ? 43 0F B6 44 2E ? 88 43 ?"))
    {
        minBounds = code[11];
        maxBounds = code[31];
        shapeRadius = code[47];
        groupId = code[63];
    }
    if (auto code = patterns::Find(client, "SkeletonBoneArray", "48 8B 97 ? ? ? ? 49 8B CE 4C 63 C3 49 C1 E0 05"))
    {
        const uint32_t disp = *reinterpret_cast<uint32_t*>(code + 3);
        boneStride = 1 << code[16];
        boneArray = disp > game::off.modelState ? disp - game::off.modelState : 0;
    }
    ready = hitboxSet && boneIndex && modelHandle && game::off.modelState && boneArray && boneStride && setList && minBounds && maxBounds && shapeRadius && groupId && boxSize > 0;
    return ready;
}

bool Hitboxes::Ready()
{
    return ready;
}

void Hitboxes::Register()
{
    settings::Bool("hitbox.head", &head);
    settings::Bool("hitbox.neck", &neck);
    settings::Bool("hitbox.chest", &chest);
    settings::Bool("hitbox.stomach", &stomach);
    settings::Bool("hitbox.arms", &arms);
    settings::Bool("hitbox.legs", &legs);
    settings::Bool("hitbox.multipoint", &multipoint);
    settings::Float("hitbox.head_scale", &headScale);
    settings::Float("hitbox.body_scale", &bodyScale);
    settings::Bool("hitbox.prefer_body", &preferBody);
}

bool Hitboxes::Enabled(int group)
{
    switch (group)
    {
    case Head: return head;
    case Neck: return neck;
    case Chest: return chest;
    case Stomach: return stomach;
    case LeftArm:
    case RightArm: return arms;
    case LeftLeg:
    case RightLeg: return legs;
    default: return chest;
    }
}

const char* Hitboxes::GroupName(int group)
{
    static const char* const names[] = { "Generic", "Head", "Chest", "Stomach", "Left arm", "Right arm", "Left leg", "Right leg", "Neck" };
    return group >= 0 && group < GroupCount ? names[group] : "?";
}

int Hitboxes::Collect(void* pawn, Box* out, int max)
{
    if (!ready || !pawn || !out || max <= 0)
        return 0;
    void* node = mem::At<void*>(pawn, game::off.sceneNode);
    if (!node)
        return 0;
    auto state = static_cast<uint8_t*>(node) + game::off.modelState;
    auto handle = mem::At<void**>(state, modelHandle);
    void* model = handle ? *handle : nullptr;
    auto bones = mem::At<uint8_t*>(state, boneArray);
    if (!model || !bones)
        return 0;
    void* set = hitboxSet(pawn, 0);
    if (!set)
        return 0;
    const int count = mem::At<int>(set, setList);
    auto data = mem::At<uint8_t*>(set, setList + 8);
    if (!data || count <= 0 || count > 64)
        return 0;

    int n = 0;
    for (int i = 0; i < count && n < max; ++i)
    {
        const uint8_t* hb = data + i * boxSize;
        const int bone = boneIndex(model, hb, 0);
        if (bone < 0 || bone >= kMaxBone)
            continue;
        const uint8_t* b = bones + bone * boneStride;
        const Vec3 pos = *reinterpret_cast<const Vec3*>(b);
        const float* q = reinterpret_cast<const float*>(b + 0x10);

        Box& box = out[n];
        box.index = i;
        box.bone = bone;
        box.group = *reinterpret_cast<const int*>(hb + groupId);
        box.radius = *reinterpret_cast<const float*>(hb + shapeRadius);
        box.capsule = box.radius > 0.f;
        box.mins = *reinterpret_cast<const Vec3*>(hb + minBounds);
        box.maxs = *reinterpret_cast<const Vec3*>(hb + maxBounds);
        box.origin = pos;
        box.axis[0] = Rotate(q, { 1.f, 0.f, 0.f });
        box.axis[1] = Rotate(q, { 0.f, 1.f, 0.f });
        box.axis[2] = Rotate(q, { 0.f, 0.f, 1.f });
        box.a = pos + Rotate(q, box.mins);
        box.b = pos + Rotate(q, box.maxs);
        box.center = (box.a + box.b) * 0.5f;
        ++n;
    }
    return n;
}

int Hitboxes::Points(const Box& box, const game::Vec3& eye, game::Vec3* out, int max)
{
    if (max <= 0)
        return 0;
    int n = 0;
    out[n++] = box.center;
    if (!multipoint)
        return n;
    const float scale = box.group == Head ? headScale : bodyScale;
    if (scale <= 0.f)
        return n;
    auto push = [&](const Vec3& p) {
        if (n < max)
            out[n++] = p;
    };
    if (box.capsule)
    {
        const Vec3 view = Normalized(box.center - eye);
        Vec3 axis = Normalized(box.b - box.a);
        if (Length(axis) < 0.5f)
            axis = { 0.f, 0.f, 1.f };
        Vec3 side = Normalized(Cross(view, axis));
        if (Length(side) < 0.5f)
            side = Normalized(Cross(view, Vec3{ 0.f, 0.f, 1.f }));
        const float r = box.radius * scale;
        push(box.center + side * r);
        push(box.center - side * r);
        if (box.group == Head)
        {
            const Vec3& top = box.a.z > box.b.z ? box.a : box.b;
            push(top + Vec3{ 0.f, 0.f, r });
            push(top + side * r);
            push(top - side * r);
        }
        else
        {
            push(box.a);
            push(box.b);
        }
        return n;
    }
    const Vec3 half = (box.maxs - box.mins) * 0.5f;
    const float extents[3] = { half.x, half.y, half.z };
    for (int i = 0; i < 3; ++i)
    {
        push(box.center + box.axis[i] * (extents[i] * scale));
        push(box.center - box.axis[i] * (extents[i] * scale));
    }
    return n;
}

bool Hitboxes::Hit(const Box& box, const game::Vec3& from, const game::Vec3& dir, float range)
{
    if (box.capsule)
        return SegmentDistance(from, from + dir * range, box.a, box.b) <= box.radius;
    const Vec3 rel = from - box.origin;
    const Vec3 o{ Dot(rel, box.axis[0]), Dot(rel, box.axis[1]), Dot(rel, box.axis[2]) };
    const Vec3 d{ Dot(dir, box.axis[0]), Dot(dir, box.axis[1]), Dot(dir, box.axis[2]) };
    float tmin = 0.f, tmax = range;
    return Slab(o.x, d.x, box.mins.x, box.maxs.x, tmin, tmax) && Slab(o.y, d.y, box.mins.y, box.maxs.y, tmin, tmax) && Slab(o.z, d.z, box.mins.z, box.maxs.z, tmin, tmax);
}

int Hitboxes::HitAny(const Box* boxes, int count, const game::Vec3& from, const game::Vec3& dir, float range)
{
    for (int i = 0; i < count; ++i)
        if (Hit(boxes[i], from, dir, range))
            return i;
    return -1;
}
