#include "ragebot.h"
#include "aim.h"
#include "damage.h"
#include "game.h"
#include "items.h"
#include "mem.h"
#include "visibility.h"
#include "core/settings.h"
#include "features/hitbox.h"
#include "features/nospread.h"
#include "features/spread.h"
#include "features/vecmath.h"
#include <Windows.h>
#include <cstring>
#include <utility>

namespace
{
    using namespace subtick;
    using namespace vm;

    constexpr uint64_t kAttack = 1ull << 0;
    constexpr float kTick = 1.f / 64.f;
    constexpr int kMaxPoints = 32;
    constexpr int kMaxScan = 4;

    damage::Weapon gun{};
    bool gunKnown = false;

    struct Track
    {
        void* pawn = nullptr;
        Vec3 origin{};
        Vec3 velocity{};
        double time = 0.0;
    };

    struct Aim
    {
        void* pawn = nullptr;
        Vec3 point{};
        float pitch = 0.f, yaw = 0.f;
        int group = -1;
        int points = 0;
        int count = 0;
        Hitboxes::Box boxes[Hitboxes::kMax]{};
    };

    Track tracks[65];
    double frameTime = 0.0;
    uint32_t lockHandle = 0;
    int lostTicks = 0;

    bool pending = false;
    bool pendingFire = false;
    float pendingPitch = 0.f, pendingYaw = 0.f;

    double Now()
    {
        static LARGE_INTEGER freq{};
        if (!freq.QuadPart)
            QueryPerformanceFrequency(&freq);
        LARGE_INTEGER t;
        QueryPerformanceCounter(&t);
        return static_cast<double>(t.QuadPart) / static_cast<double>(freq.QuadPart);
    }

    float Clamp(float v, float lo, float hi)
    {
        return v < lo ? lo : v > hi ? hi : v;
    }

    float Fov(float p0, float y0, float p1, float y1)
    {
        const float dp = p1 - p0, dy = NormalizeYaw(y1 - y0);
        return std::sqrt(dp * dp + dy * dy);
    }

    void* PawnOf(void* controller)
    {
        return controller ? game::Handle(mem::At<uint32_t>(controller, game::off.playerPawn)) : nullptr;
    }

    bool Valid(void* pawn, int localTeam)
    {
        const auto& o = game::off;
        if (!pawn || mem::At<int>(pawn, o.health) <= 0)
            return false;
        if (o.lifeState && mem::At<uint8_t>(pawn, o.lifeState) != 0)
            return false;
        if (!aim::teammates && mem::At<uint8_t>(pawn, o.teamNum) == localTeam)
            return false;
        void* node = mem::At<void*>(pawn, o.sceneNode);
        if (!node || (o.dormant && mem::At<bool>(node, o.dormant)))
            return false;
        if (o.immunity && mem::At<bool>(pawn, o.immunity))
            return false;
        return true;
    }

    void* ActiveWeapon(void* local)
    {
        auto services = mem::At<uint8_t*>(local, game::off.weaponServices);
        return services ? game::Handle(mem::At<uint32_t>(services, game::off.activeWeapon)) : nullptr;
    }

    bool Gun(void* weapon)
    {
        if (!weapon)
            return false;
        const int def = mem::At<uint16_t>(static_cast<uint8_t*>(weapon) + game::off.attributeManager + game::off.item, game::off.defIndex);
        const items::Item* item = items::Find(def);
        return item && item->category <= items::Heavy;
    }

    bool CanFire(void* controller, void* weapon)
    {
        if (!Gun(weapon))
            return false;
        if (game::off.clip1 && mem::At<int>(weapon, game::off.clip1) <= 0)
            return false;
        if (game::off.nextAttackTick && game::off.tickBase)
            if (mem::At<int>(weapon, game::off.nextAttackTick) > mem::At<int>(controller, game::off.tickBase))
                return false;
        return true;
    }

    Vec3 Velocity(void* pawn)
    {
        for (int i = 1; i <= 64; ++i)
            if (tracks[i].pawn == pawn)
            {
                const Vec3& v = tracks[i].velocity;
                if (Dot(v, v) > 1.f)
                    return v;
                break;
            }
        return game::off.absVelocity ? mem::At<Vec3>(pawn, game::off.absVelocity) : Vec3{};
    }

    void Shift(Hitboxes::Box& box, const Vec3& d)
    {
        box.a = box.a + d;
        box.b = box.b + d;
        box.center = box.center + d;
        box.origin = box.origin + d;
    }

    int Priority(int group)
    {
        const bool body = group == Hitboxes::Chest || group == Hitboxes::Stomach;
        if (Hitboxes::preferBody)
            return body ? 0 : group == Hitboxes::Head ? 1 : group == Hitboxes::Neck ? 2 : 3;
        return group == Hitboxes::Head ? 0 : group == Hitboxes::Neck ? 1 : body ? 2 : 3;
    }

    int CapsulePoints(const Hitboxes::Box& box, const Vec3& eye, float scale, Vec3* out)
    {
        int n = 0;
        auto push = [&](const Vec3& p) {
            if (n < kMaxPoints)
                out[n++] = p;
        };
        push(box.center);
        if (!Hitboxes::multipoint || scale <= 0.f)
            return n;
        const Vec3 view = Normalized(box.center - eye);
        Vec3 axis = Normalized(box.b - box.a);
        if (Length(axis) < 0.5f)
            axis = { 0.f, 0.f, 1.f };
        Vec3 side = Normalized(Cross(view, axis));
        if (Length(side) < 0.5f)
            side = Normalized(Cross(view, Vec3{ 0.f, 0.f, 1.f }));
        if (Length(side) < 0.5f)
            side = { 1.f, 0.f, 0.f };
        Vec3 up = Normalized(Cross(side, view));
        const float r = box.radius * scale;
        const int segments = ragebot::density < 1 ? 1 : ragebot::density > 4 ? 4 : ragebot::density;
        const Vec3 len = box.b - box.a;
        for (int s = 0; s <= segments * 2; ++s)
        {
            const float t = 0.5f + ((s & 1) ? -1.f : 1.f) * static_cast<float>((s + 1) / 2) / static_cast<float>(segments * 2);
            const Vec3 c = box.a + len * t;
            if (s)
                push(c);
            push(c + side * r);
            push(c - side * r);
            push(c + up * r);
            push(c - up * r);
        }
        push(box.a - axis * r);
        push(box.b + axis * r);
        if (box.group == Hitboxes::Head)
        {
            const Vec3& top = box.a.z > box.b.z ? box.a : box.b;
            push(top + Vec3{ 0.f, 0.f, r });
        }
        return n;
    }

    int BoxPoints(const Hitboxes::Box& box, float scale, Vec3* out)
    {
        int n = 0;
        auto local = [&](float x, float y, float z) {
            return box.origin + box.axis[0] * x + box.axis[1] * y + box.axis[2] * z;
        };
        auto push = [&](const Vec3& p) {
            if (n < kMaxPoints)
                out[n++] = p;
        };
        push(box.center);
        if (!Hitboxes::multipoint || scale <= 0.f)
            return n;
        const Vec3 mid = (box.mins + box.maxs) * 0.5f;
        const Vec3 half = (box.maxs - box.mins) * (0.5f * scale);
        const float h[3] = { half.x, half.y, half.z };
        const float m[3] = { mid.x, mid.y, mid.z };
        for (int i = 0; i < 3; ++i)
            for (int sgn = -1; sgn <= 1; sgn += 2)
            {
                float c[3] = { m[0], m[1], m[2] };
                c[i] += sgn * h[i];
                push(local(c[0], c[1], c[2]));
            }
        if (ragebot::density >= 2)
            for (int i = 0; i < 8; ++i)
                push(local(m[0] + (i & 1 ? h[0] : -h[0]), m[1] + (i & 2 ? h[1] : -h[1]), m[2] + (i & 4 ? h[2] : -h[2])));
        if (ragebot::density >= 3)
            for (int i = 0; i < 3; ++i)
                for (int k = 0; k < 4; ++k)
                {
                    float c[3] = { m[0], m[1], m[2] };
                    const int j = (i + 1) % 3, l = (i + 2) % 3;
                    c[j] += k & 1 ? h[j] : -h[j];
                    c[l] += k & 2 ? h[l] : -h[l];
                    push(local(c[0], c[1], c[2]));
                }
        return n;
    }

    int BoneBoxes(void* pawn, Hitboxes::Box* out)
    {
        struct Def
        {
            int from, to, group;
            float radius;
        };
        static const Def defs[] = {
            { 6, 6, Hitboxes::Head, 4.5f },
            { 5, 6, Hitboxes::Neck, 3.5f },
            { 4, 5, Hitboxes::Chest, 6.f },
            { 2, 4, Hitboxes::Stomach, 6.f },
            { 0, 2, Hitboxes::Stomach, 6.f },
        };
        int n = 0;
        for (const Def& d : defs)
        {
            game::Vec3 a, b;
            if (!game::Bone(pawn, d.from, a) || !game::Bone(pawn, d.to, b))
                continue;
            Hitboxes::Box& box = out[n++];
            box = Hitboxes::Box{};
            box.group = d.group;
            box.bone = d.from;
            box.capsule = true;
            box.radius = d.radius;
            box.a = a;
            box.b = b;
            box.center = (a + b) * 0.5f;
            box.origin = a;
        }
        return n;
    }

    bool Scan(void* pawn, const Vec3& eye, float lead, Aim& out)
    {
        out.count = Hitboxes::Collect(pawn, out.boxes, Hitboxes::kMax);
        if (out.count <= 0)
            out.count = BoneBoxes(pawn, out.boxes);
        if (out.count <= 0)
            return false;
        const Vec3 v = Velocity(pawn);
        ragebot::debug.speed = Length(v);
        if (lead > 0.f)
        {
            const Vec3 d = v * lead;
            for (int i = 0; i < out.count; ++i)
                Shift(out.boxes[i], d);
        }

        int order[Hitboxes::kMax];
        int n = 0;
        for (int i = 0; i < out.count; ++i)
            if (Hitboxes::Enabled(out.boxes[i].group))
                order[n++] = i;
        for (int i = 1; i < n; ++i)
            for (int j = i; j > 0 && Priority(out.boxes[order[j]].group) < Priority(out.boxes[order[j - 1]].group); --j)
                std::swap(order[j], order[j - 1]);

        const damage::Victim victim = damage::Read(pawn);
        const float required = damage::Required(victim, static_cast<float>(ragebot::minDamage));
        Vec3 points[kMaxPoints];
        for (int i = 0; i < n; ++i)
        {
            const Hitboxes::Box& box = out.boxes[order[i]];
            if (gunKnown && damage::Scale(gun, victim, box.group, Length(box.center - eye) - box.radius) < required)
                continue;
            const float scale = (box.group == Hitboxes::Head ? Hitboxes::headScale : Hitboxes::bodyScale) * ragebot::pointScale / 0.8f;
            const float s = Clamp(scale, 0.f, 0.95f);
            const int count = box.capsule ? CapsulePoints(box, eye, s, points) : BoxPoints(box, s, points);
            for (int k = 0; k < count; ++k)
            {
                const float dealt = gunKnown ? damage::Scale(gun, victim, box.group, Length(points[k] - eye)) : 0.f;
                if (gunKnown && dealt < required)
                    continue;
                ++ragebot::debug.traces;
                if (!visibility::Point(pawn, eye, points[k]))
                    continue;
                ragebot::debug.damage = dealt;
                out.pawn = pawn;
                out.point = points[k];
                out.group = box.group;
                out.points = count;
                Angles(points[k] - eye, out.pitch, out.yaw);
                return true;
            }
        }
        return false;
    }

    float Lead()
    {
        if (!ragebot::prediction)
            return 0.f;
        const float frame = frameTime > 0.0 ? Clamp(static_cast<float>(Now() - frameTime), 0.f, 0.1f) : 0.f;
        return frame + Clamp(static_cast<float>(ragebot::extraTicks), 0.f, 8.f) * kTick;
    }

    bool Locked(const Vec3& eye, int localTeam, float lead, Aim& out)
    {
        if (!lockHandle)
            return false;
        void* pawn = game::Handle(lockHandle);
        if (!Valid(pawn, localTeam))
        {
            lockHandle = 0;
            return false;
        }
        if (Scan(pawn, eye, lead, out))
        {
            lostTicks = 0;
            return true;
        }
        if (++lostTicks > ragebot::lockTicks)
            lockHandle = 0;
        return false;
    }

    bool Acquire(void* local, const Vec3& eye, float viewPitch, float viewYaw, int localTeam, float lead, Aim& out)
    {
        struct Candidate
        {
            void* pawn;
            uint32_t handle;
            float fov;
        };
        Candidate list[64];
        int n = 0;
        const int localIndex = game::LocalIndex();
        for (int i = 1; i <= 64; ++i)
        {
            const char* designer = nullptr;
            void* controller = game::EntityAt(i, &designer);
            if (!controller || !designer || strcmp(designer, "cs_player_controller") || i == localIndex)
                continue;
            const uint32_t handle = mem::At<uint32_t>(controller, game::off.playerPawn);
            void* pawn = game::Handle(handle);
            if (pawn == local || !Valid(pawn, localTeam))
                continue;
            game::Vec3 head;
            if (!game::Bone(pawn, 6, head) && !game::Origin(pawn, head))
                continue;
            float p, y;
            Angles(head - eye, p, y);
            const float f = Fov(viewPitch, viewYaw, p, y);
            if (f > ragebot::fov)
                continue;
            list[n++] = { pawn, handle, f };
        }
        for (int i = 1; i < n; ++i)
            for (int j = i; j > 0 && list[j].fov < list[j - 1].fov; --j)
                std::swap(list[j], list[j - 1]);
        for (int i = 0; i < n && i < kMaxScan; ++i)
            if (Scan(list[i].pawn, eye, lead, out))
            {
                lockHandle = list[i].handle;
                lostTicks = 0;
                return true;
            }
        return false;
    }

    bool Accurate(void* controller, void* weapon, const Vec3& eye, const float* punch, float& pitch, float& yaw, const Aim& target)
    {
        Spread::Weapon w;
        if (!Spread::Ready() || !Spread::Read(weapon, w))
            return true;
        Spread::Shot shot;
        shot.pitch = pitch;
        shot.yaw = yaw;
        shot.punchPitch = punch[0];
        shot.punchYaw = punch[1];
        shot.tick = game::off.tickBase ? mem::At<int>(controller, game::off.tickBase) : 0;
        if (NoSpread::enabled && ragebot::silent)
        {
            float p = pitch, y = yaw;
            if (NoSpread::Apply(w, shot, p, y))
            {
                shot.pitch = pitch = p;
                shot.yaw = yaw = y;
            }
        }
        return Spread::Allow(eye, shot, w, target.boxes, target.count);
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

    void Rotate(Input* in, float pitch, float yaw)
    {
        const float dp = pitch - in->pitch, dy = NormalizeYaw(yaw - in->yaw);
        in->pitch = Clamp(pitch, -89.f, 89.f);
        in->yaw = NormalizeYaw(yaw);
        for (int i = 0; i < in->count && i < kMaxEvents; ++i)
        {
            in->events[i].pitch = Clamp(in->events[i].pitch + dp, -89.f, 89.f);
            in->events[i].yaw = NormalizeYaw(in->events[i].yaw + dy);
        }
    }

    void Run(void* input, Input* in, const float* punch)
    {
        auto& d = ragebot::debug;
        d.fired = false;
        d.traces = 0;
        if (!ragebot::Active())
        {
            lockHandle = 0;
            d.locked = false;
            return;
        }
        void* controller = game::LocalController();
        void* local = PawnOf(controller);
        if (!local || mem::At<int>(local, game::off.health) <= 0)
            return;
        void* weapon = ActiveWeapon(local);
        if (!Gun(weapon))
            return;
        gunKnown = damage::Read(weapon, gun);
        const Vec3 eye = game::EyePosition(local);
        const int localTeam = mem::At<uint8_t>(local, game::off.teamNum);
        const float bulletPitch = in->pitch + punch[0], bulletYaw = in->yaw + punch[1];
        const float lead = Lead();
        d.lead = lead;

        static Aim target;
        target = Aim{};
        const bool found = Locked(eye, localTeam, lead, target) || (!lockHandle && Acquire(local, eye, bulletPitch, bulletYaw, localTeam, lead, target));
        d.locked = lockHandle != 0;
        if (!found)
            return;
        d.group = target.group;
        d.points = target.points;

        float p = Clamp(target.pitch - punch[0], -89.f, 89.f);
        float y = NormalizeYaw(target.yaw - punch[1]);
        bool fire = ragebot::autofire && CanFire(controller, weapon);
        if (fire)
            fire = Accurate(controller, weapon, eye, punch, p, y, target);
        const bool manual = ((in->down | in->pressed) & kAttack) != 0;
        if (fire)
            Click(in);
        d.fired = fire;

        if (!ragebot::silent)
        {
            Rotate(in, p, y);
            game::SetViewAngles(input, in->pitch, in->yaw);
            return;
        }
        if (fire || manual)
        {
            pending = true;
            pendingFire = fire;
            pendingPitch = p;
            pendingYaw = y;
        }
    }
}

void ragebot::Register()
{
    settings::Bool("rage.enabled", &enabled);
    settings::Key("bind.rage", &key);
    settings::Bool("rage.silent", &silent);
    settings::Bool("rage.autofire", &autofire);
    settings::Float("rage.fov", &fov);
    settings::Bool("rage.prediction", &prediction);
    settings::Int("rage.extra_ticks", &extraTicks);
    settings::Int("rage.lock_ticks", &lockTicks);
    settings::Float("rage.point_scale", &pointScale);
    settings::Int("rage.density", &density);
    settings::Int("rage.min_damage", &minDamage);
}

bool ragebot::Active()
{
    return enabled && misc::Pressed(key);
}

void ragebot::OnFrameStage(int stage)
{
    if (stage != 7 || !game::Ready())
        return;
    __try
    {
        const double now = Now();
        for (int i = 1; i <= 64; ++i)
        {
            Track& t = tracks[i];
            const char* designer = nullptr;
            void* controller = enabled ? game::EntityAt(i, &designer) : nullptr;
            void* pawn = controller && designer && !strcmp(designer, "cs_player_controller") ? PawnOf(controller) : nullptr;
            Vec3 origin;
            if (!pawn || !game::Origin(pawn, origin))
            {
                t = Track{};
                continue;
            }
            const double dt = now - t.time;
            if (t.pawn == pawn && dt > 1e-4 && dt < 0.25)
            {
                const Vec3 delta = origin - t.origin;
                const float dist = Length(delta);
                if (dist > 1024.f)
                    t.velocity = {};
                else
                    t.velocity = t.velocity * 0.5f + delta * static_cast<float>(0.5 / dt);
            }
            else
                t.velocity = {};
            t.pawn = pawn;
            t.origin = origin;
            t.time = now;
        }
        frameTime = now;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        for (Track& t : tracks)
            t = Track{};
    }
}

void ragebot::OnInput(void* input, subtick::Input* in, const float* punch)
{
    pending = false;
    __try
    {
        Run(input, in, punch);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        pending = false;
        lockHandle = 0;
    }
}

bool ragebot::OnCmd(usercmd::Cmd* cmd)
{
    if (!pending || !cmd)
        return false;
    pending = false;
    if (pendingFire)
        usercmd::Attack(cmd);
    if (silent && (pendingFire || usercmd::Attacking(cmd)))
        return usercmd::SetAngles(cmd, pendingPitch, pendingYaw);
    return pendingFire;
}
