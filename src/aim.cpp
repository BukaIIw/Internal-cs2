#include "aim.h"
#include "damage.h"
#include "game.h"
#include "items.h"
#include "mem.h"
#include "visibility.h"
#include "core/ray.h"
#include "features/hitbox.h"
#include <Windows.h>
#include <cmath>
#include <cstring>
#include <utility>

namespace
{
    using namespace subtick;

    constexpr uint64_t kAttack = 1ull << 0;
    constexpr float kRad = 3.14159265f / 180.f;

    ULONGLONG triggerSeen = 0;

    struct Candidate
    {
        void* pawn = nullptr;
        float pitch = 0.f, yaw = 0.f;
        float fov = 1e9f;
        float dist = 0.f;
    };

    float Normalize(float a)
    {
        a = std::fmod(a + 180.f, 360.f);
        return (a < 0.f ? a + 360.f : a) - 180.f;
    }

    void AnglesTo(const game::Vec3& from, const game::Vec3& to, float& pitch, float& yaw)
    {
        const float dx = to.x - from.x, dy = to.y - from.y, dz = to.z - from.z;
        const float hyp = std::sqrt(dx * dx + dy * dy);
        pitch = -std::atan2(dz, hyp) / kRad;
        yaw = std::atan2(dy, dx) / kRad;
    }

    float Fov(float p0, float y0, float p1, float y1)
    {
        const float dp = Normalize(p1 - p0), dy = Normalize(y1 - y0);
        return std::sqrt(dp * dp + dy * dy);
    }

    int Bones(int hitbox, int* out)
    {
        static const int nearest[] = { 6, 5, 4, 2, 0 };
        switch (hitbox)
        {
        case 0: out[0] = 6; return 1;
        case 1: out[0] = 5; return 1;
        case 2: out[0] = 4; out[1] = 2; return 2;
        default:
            memcpy(out, nearest, sizeof(nearest));
            return 5;
        }
    }

    void* LocalPawn(void* controller)
    {
        return controller ? game::Handle(mem::At<uint32_t>(controller, game::off.playerPawn)) : nullptr;
    }

    bool Valid(void* pawn, int localTeam)
    {
        if (!pawn || mem::At<int>(pawn, game::off.health) <= 0)
            return false;
        if (game::off.lifeState && mem::At<uint8_t>(pawn, game::off.lifeState) != 0)
            return false;
        if (!aim::teammates && mem::At<uint8_t>(pawn, game::off.teamNum) == localTeam)
            return false;
        void* node = mem::At<void*>(pawn, game::off.sceneNode);
        if (!node || (game::off.dormant && mem::At<bool>(node, game::off.dormant)))
            return false;
        if (game::off.immunity && mem::At<bool>(pawn, game::off.immunity))
            return false;
        return true;
    }

    constexpr int kMaxPoints = 64;

    bool Wanted(int hitbox, int group)
    {
        switch (hitbox)
        {
        case 0: return group == Hitboxes::Head;
        case 1: return group == Hitboxes::Neck;
        case 2: return group == Hitboxes::Chest || group == Hitboxes::Stomach;
        default: return group != Hitboxes::Generic;
        }
    }

    int Points(void* pawn, const game::Vec3& eye, int hitbox, game::Vec3* out)
    {
        Hitboxes::Box boxes[Hitboxes::kMax];
        const int count = Hitboxes::Collect(pawn, boxes, Hitboxes::kMax);
        int n = 0;
        for (int i = 0; i < count; ++i)
        {
            if (!Wanted(hitbox, boxes[i].group))
                continue;
            game::Vec3 pts[Hitboxes::kMaxPoints];
            const int k = Hitboxes::Points(boxes[i], eye, pts, Hitboxes::kMaxPoints);
            for (int j = 0; j < k && n < kMaxPoints; ++j)
                out[n++] = pts[j];
        }
        if (n)
            return n;
        int bones[5];
        const int b = Bones(hitbox, bones);
        for (int i = 0; i < b; ++i)
            if (game::Bone(pawn, bones[i], out[n]))
                ++n;
        return n;
    }

    Candidate Best(void* local, const game::Vec3& eye, float viewPitch, float viewYaw, int hitbox, float maxFov)
    {
        Candidate best;
        const int localTeam = mem::At<uint8_t>(local, game::off.teamNum);
        const int localIndex = game::LocalIndex();
        for (int i = 1; i <= 64; ++i)
        {
            const char* designer = nullptr;
            void* controller = game::EntityAt(i, &designer);
            if (!controller || !designer || strcmp(designer, "cs_player_controller") || i == localIndex)
                continue;
            void* pawn = game::Handle(mem::At<uint32_t>(controller, game::off.playerPawn));
            if (pawn == local || !Valid(pawn, localTeam))
                continue;
            game::Vec3 points[kMaxPoints];
            const int n = Points(pawn, eye, hitbox, points);
            for (int b = 0; b < n; ++b)
            {
                const game::Vec3& point = points[b];
                float p, y;
                AnglesTo(eye, point, p, y);
                const float f = Fov(viewPitch, viewYaw, p, y);
                if (f > maxFov || f >= best.fov || !visibility::Point(pawn, eye, point))
                    continue;
                const float dx = point.x - eye.x, dy = point.y - eye.y, dz = point.z - eye.z;
                best.pawn = pawn;
                best.pitch = p;
                best.yaw = y;
                best.fov = f;
                best.dist = std::sqrt(dx * dx + dy * dy + dz * dz);
            }
        }
        return best;
    }

    bool CanFire(void* controller, void* local)
    {
        auto services = mem::At<uint8_t*>(local, game::off.weaponServices);
        void* weapon = services ? game::Handle(mem::At<uint32_t>(services, game::off.activeWeapon)) : nullptr;
        if (!weapon)
            return false;
        const int def = mem::At<uint16_t>(static_cast<uint8_t*>(weapon) + game::off.attributeManager + game::off.item, game::off.defIndex);
        const items::Item* item = items::Find(def);
        if (!item || item->category > items::Heavy)
            return false;
        if (game::off.clip1 && mem::At<int>(weapon, game::off.clip1) <= 0)
            return false;
        if (game::off.nextAttackTick && game::off.tickBase)
        {
            const int next = mem::At<int>(weapon, game::off.nextAttackTick);
            const int now = mem::At<int>(controller, game::off.tickBase);
            if (next > now)
                return false;
        }
        return true;
    }

    void* PawnOf(void* entity, int localTeam)
    {
        if (!entity)
            return nullptr;
        for (int i = 1; i <= 64; ++i)
        {
            const char* designer = nullptr;
            void* controller = game::EntityAt(i, &designer);
            if (!controller || !designer || strcmp(designer, "cs_player_controller"))
                continue;
            void* pawn = game::Handle(mem::At<uint32_t>(controller, game::off.playerPawn));
            if (pawn != entity)
                continue;
            return Valid(pawn, localTeam) ? pawn : nullptr;
        }
        return nullptr;
    }

    float Dot3(const game::Vec3& a, const game::Vec3& b)
    {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    game::Vec3 Sub3(const game::Vec3& a, const game::Vec3& b)
    {
        return { a.x - b.x, a.y - b.y, a.z - b.z };
    }

    float SphereEntry(const game::Vec3& from, const game::Vec3& dir, const game::Vec3& c, float r)
    {
        const game::Vec3 m = Sub3(from, c);
        const float b = Dot3(m, dir), q = Dot3(m, m) - r * r;
        if (q > 0.f && b > 0.f)
            return -1.f;
        const float disc = b * b - q;
        if (disc < 0.f)
            return -1.f;
        const float t = -b - std::sqrt(disc);
        return t < 0.f ? 0.f : t;
    }

    float CapsuleEntry(const Hitboxes::Box& box, const game::Vec3& from, const game::Vec3& dir)
    {
        float best = -1.f;
        auto take = [&](float t) { if (t >= 0.f && (best < 0.f || t < best)) best = t; };
        take(SphereEntry(from, dir, box.a, box.radius));
        take(SphereEntry(from, dir, box.b, box.radius));
        const game::Vec3 axis = Sub3(box.b, box.a);
        const float len2 = Dot3(axis, axis);
        if (len2 < 1e-6f)
            return best;
        const game::Vec3 m = Sub3(from, box.a);
        const float md = Dot3(m, axis), nd = Dot3(dir, axis);
        const float a = len2 - nd * nd;
        const float k = Dot3(m, m) - box.radius * box.radius;
        const float b = len2 * Dot3(m, dir) - nd * md;
        const float c = len2 * k - md * md;
        if (std::fabs(a) < 1e-6f)
            return best;
        const float disc = b * b - a * c;
        if (disc < 0.f)
            return best;
        float t = (-b - std::sqrt(disc)) / a;
        if (t < 0.f && c <= 0.f)
            t = 0.f;
        const float s = md + t * nd;
        if (t >= 0.f && s >= 0.f && s <= len2)
            take(t);
        return best;
    }

    float BoxEntry(const Hitboxes::Box& box, const game::Vec3& from, const game::Vec3& dir)
    {
        const game::Vec3 rel = Sub3(from, box.origin);
        const float o[3]{ Dot3(rel, box.axis[0]), Dot3(rel, box.axis[1]), Dot3(rel, box.axis[2]) };
        const float d[3]{ Dot3(dir, box.axis[0]), Dot3(dir, box.axis[1]), Dot3(dir, box.axis[2]) };
        const float lo[3]{ box.mins.x, box.mins.y, box.mins.z }, hi[3]{ box.maxs.x, box.maxs.y, box.maxs.z };
        float tmin = 0.f, tmax = 1e9f;
        for (int i = 0; i < 3; ++i)
        {
            if (std::fabs(d[i]) < 1e-8f)
            {
                if (o[i] < lo[i] || o[i] > hi[i])
                    return -1.f;
                continue;
            }
            float t1 = (lo[i] - o[i]) / d[i], t2 = (hi[i] - o[i]) / d[i];
            if (t1 > t2)
                std::swap(t1, t2);
            tmin = t1 > tmin ? t1 : tmin;
            tmax = t2 < tmax ? t2 : tmax;
            if (tmin > tmax)
                return -1.f;
        }
        return tmin;
    }

    bool TriggerGroup(int group)
    {
        switch (group)
        {
        case Hitboxes::Head: return aim::triggerHead;
        case Hitboxes::Neck: return aim::triggerNeck;
        case Hitboxes::Chest: return aim::triggerChest;
        case Hitboxes::Stomach: return aim::triggerStomach;
        case Hitboxes::LeftArm:
        case Hitboxes::RightArm: return aim::triggerArms;
        case Hitboxes::LeftLeg:
        case Hitboxes::RightLeg: return aim::triggerLegs;
        default: return aim::triggerChest || aim::triggerStomach;
        }
    }

    bool TriggerHit(void* local, void* pawn, const game::Vec3& eye, float pitch, float yaw, float range)
    {
        Hitboxes::Box boxes[Hitboxes::kMax];
        const int count = Hitboxes::Collect(pawn, boxes, Hitboxes::kMax);
        if (count <= 0)
            return TriggerGroup(Hitboxes::Chest);
        const game::Vec3 dir = ray::Forward(pitch, yaw, 1.f);
        int nearest = -1;
        float entry = range;
        for (int i = 0; i < count; ++i)
        {
            const float t = boxes[i].capsule ? CapsuleEntry(boxes[i], eye, dir) : BoxEntry(boxes[i], eye, dir);
            if (t >= 0.f && t <= entry)
            {
                entry = t;
                nearest = i;
            }
        }
        if (nearest < 0 || !TriggerGroup(boxes[nearest].group))
            return false;
        auto services = mem::At<uint8_t*>(local, game::off.weaponServices);
        void* weapon = services ? game::Handle(mem::At<uint32_t>(services, game::off.activeWeapon)) : nullptr;
        damage::Weapon w;
        if (!damage::Read(weapon, w))
            return true;
        const damage::Victim v = damage::Read(pawn);
        return damage::Scale(w, v, boxes[nearest].group, entry) >= damage::Required(v, static_cast<float>(aim::triggerMinDamage));
    }

    bool TriggerTarget(void* controller, void* local, const game::Vec3& eye, float pitch, float yaw)
    {
        const int localTeam = mem::At<uint8_t>(local, game::off.teamNum);
        if (ray::Ready())
        {
            const game::Vec3 dir = ray::Forward(pitch, yaw, 8192.f);
            const game::Vec3 end{ eye.x + dir.x, eye.y + dir.y, eye.z + dir.z };
            ray::Hit hit;
            if (!ray::Trace(eye, end, mem::At<uint32_t>(controller, game::off.playerPawn), ray::kMaskShot, hit))
                return false;
            void* pawn = PawnOf(hit.entity, localTeam);
            if (!pawn)
                return false;
            const float range = 8192.f * hit.fraction + 64.f;
            return TriggerHit(local, pawn, eye, pitch, yaw, range);
        }
        Candidate c = Best(local, eye, pitch, yaw, 3, 30.f);
        return c.pawn && c.dist > 1.f && c.fov <= std::atan2(4.f, c.dist) / kRad;
    }

    void Click(Input* in)
    {
        if (in->down & kAttack)
            return;
        Remove(in, [](const Event& e) { return e.button == kAttack; });
        Button(in, kAttack, true, 0.f);
        Button(in, kAttack, false, 0.5f);
        in->pressed |= kAttack;
        in->released |= kAttack;
    }

    void Rotate(Input* in, float dPitch, float dYaw)
    {
        auto clamp = [](float p) { return p > 89.f ? 89.f : p < -89.f ? -89.f : p; };
        in->pitch = clamp(in->pitch + dPitch);
        in->yaw = Normalize(in->yaw + dYaw);
        for (int i = 0; i < in->count && i < kMaxEvents; ++i)
        {
            in->events[i].pitch = clamp(in->events[i].pitch + dPitch);
            in->events[i].yaw = Normalize(in->events[i].yaw + dYaw);
        }
    }

    void Run(void* input, Input* in, const float* punch)
    {
        void* controller = game::LocalController();
        void* local = LocalPawn(controller);
        if (!local || mem::At<int>(local, game::off.health) <= 0)
            return;
        const game::Vec3 eye = game::EyePosition(local);
        const float viewPitch = in->pitch, viewYaw = in->yaw;
        const float bulletPitch = viewPitch + punch[0], bulletYaw = viewYaw + punch[1];

        if (aim::legit && aim::Held(aim::legitKey))
        {
            Candidate c = Best(local, eye, aim::legitRcs ? bulletPitch : viewPitch, aim::legitRcs ? bulletYaw : viewYaw, aim::legitHitbox, aim::legitFov);
            if (!c.pawn && aim::legitHitbox < 3)
                c = Best(local, eye, aim::legitRcs ? bulletPitch : viewPitch, aim::legitRcs ? bulletYaw : viewYaw, 3, aim::legitFov);
            if (c.pawn)
            {
                const float p = aim::legitRcs ? c.pitch - punch[0] : c.pitch;
                const float y = aim::legitRcs ? Normalize(c.yaw - punch[1]) : c.yaw;
                const float smooth = aim::legitSmooth < 1.f ? 1.f : aim::legitSmooth;
                Rotate(in, (p - viewPitch) / smooth, Normalize(y - viewYaw) / smooth);
                game::SetViewAngles(input, in->pitch, in->yaw);
            }
        }

        if (aim::trigger && aim::Held(aim::triggerKey))
        {
            const bool hit = TriggerTarget(controller, local, eye, in->pitch + punch[0], in->yaw + punch[1]);
            const ULONGLONG now = GetTickCount64();
            if (!hit)
                triggerSeen = 0;
            else if (!triggerSeen)
                triggerSeen = now;
            if (hit && now - triggerSeen >= static_cast<ULONGLONG>(aim::triggerDelay) && CanFire(controller, local))
                Click(in);
        }
    }
}

bool aim::Any()
{
    return legit || trigger;
}

bool aim::Held(const misc::Bind& bind)
{
    return misc::Pressed(bind);
}

void aim::OnInput(void* input, subtick::Input* in, const float* punch)
{
    __try
    {
        Run(input, in, punch);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
}
