#include "core/patterns.h"
#include "movement.h"
#include "game.h"
#include "mem.h"
#include "misc.h"
#include "input.h"
#include "aim.h"
#include "ragebot.h"
#include "core/usercmd.h"
#include "features/jumpcheck.h"
#include "core/events.h"
#include <Windows.h>
#include <MinHook.h>
#include <cmath>
#include <cstddef>
#include <cstring>

namespace
{
    using namespace subtick;
    constexpr uint64_t kAttack = 1ull << 0;
    constexpr uint64_t kJump = 1ull << 1;
    constexpr uint64_t kMove = (1ull << 3) | (1ull << 4) | (1ull << 9) | (1ull << 10);
    constexpr uint64_t kForwardKey = 1ull << 3;
    constexpr uint64_t kSpeed = 1ull << 16;
    constexpr float kTick = 1.f / 64.f;
    constexpr float kMinPress = 1.f / 64.f;
    constexpr float kLandPad = 0.02f;
    constexpr float kMaxPress = 0.98f;
    constexpr float kRad = 3.14159265f / 180.f;

    using CreateMoveFn = void* (__fastcall*)(void*, int, bool);
    using GetUserCmdFn = void* (__fastcall*)(void*, int);
    using MergeFn = void(__fastcall*)(void*, int);

    CreateMoveFn oCreateMove = nullptr;
    GetUserCmdFn oGetUserCmd = nullptr;
    MergeFn oMerge = nullptr;
    void* (*aimPunch)(void*, float*, bool) = nullptr;

    DWORD activeThread = 0;
    void* captured = nullptr;
    uint8_t* airAccelerate = nullptr;
    uint8_t* airMaxWishSpeed = nullptr;
    uint8_t* gravity = nullptr;
    uint8_t* maxSpeedVar = nullptr;
    uint8_t* subtickAngles = nullptr;
    bool jumpSent = false;
    bool landingFault = false;
    bool realSpeed = false;

    struct State
    {
        bool valid = false;
        bool ground = false;
        float vx = 0.f, vy = 0.f;
        float maxSpeed = 250.f;
        float punch[3]{};
        float z = 0.f, vz = 0.f;
        float gravityScale = 1.f;
    };

    State current;
    bool realHeld = false;
    uint64_t realKeys = 0;
    bool strafeSide = false;

    float ConVar(uint8_t* object, float fallback)
    {
        return game::ConVarFloat(object, fallback);
    }

    float Normalize(float a)
    {
        a = std::fmod(a + 180.f, 360.f);
        return (a < 0.f ? a + 360.f : a) - 180.f;
    }

    State Sample()
    {
        State s;
        void* controller = game::LocalController();
        void* pawn = controller ? game::Handle(mem::At<uint32_t>(controller, game::off.playerPawn)) : nullptr;
        if (!pawn || mem::At<int>(pawn, game::off.health) <= 0 || mem::At<uint8_t>(pawn, game::off.moveType) != 2)
            return s;
        s.valid = true;
        s.ground = mem::At<uint32_t>(pawn, game::off.flags) & 1;
        s.vx = mem::At<float>(pawn, game::off.absVelocity);
        s.vy = mem::At<float>(pawn, game::off.absVelocity + 4);
        s.vz = mem::At<float>(pawn, game::off.absVelocity + 8);
        if (void* node = mem::At<void*>(pawn, game::off.sceneNode))
            s.z = mem::At<float>(node, game::off.absOrigin + 8);
        s.gravityScale = mem::At<float>(pawn, game::off.gravityScale);
        if (s.gravityScale <= 0.f || s.gravityScale > 10.f)
            s.gravityScale = 1.f;
        if (void* ms = mem::At<void*>(pawn, game::off.movementServices))
        {
            const float v = mem::At<float>(ms, game::off.maxSpeed);
            if (v > 1.f && v < 1000.f)
                s.maxSpeed = v;
        }
        if (aimPunch)
            if (void* services = mem::At<void*>(pawn, game::off.aimPunchServices))
                aimPunch(services, s.punch, true);
        return s;
    }

    void Press(Input* in, float when)
    {
        if (jumpSent)
        {
            Button(in, kJump, false, 0.f);
            in->released |= kJump;
            when = when < kMinPress ? kMinPress : when;
        }
        Button(in, kJump, true, when);
        in->pressed |= kJump;
        in->down |= kJump;
        jumpSent = true;
    }

    float Landing()
    {
        movement::debug.stage = 4;
        void* controller = game::LocalController();
        void* pawn = controller ? game::Handle(mem::At<uint32_t>(controller, game::off.playerPawn)) : nullptr;
        const float frac = JumpCheck::Ready() && !landingFault ? JumpCheck::LandingFraction(pawn, JumpCheck::last) : -1.f;
        movement::debug.stage = 1;
        return frac;
    }

    bool Bunnyhop(Input* in, const State& st, bool held)
    {
        if (!misc::Active(movement::bhop, misc::bhopBind) || !st.valid || !held)
        {
            jumpSent = (in->down & kJump) != 0;
            return false;
        }
        Remove(in, [](const Event& e) { return e.button == kJump; });
        in->pressed &= ~kJump;
        in->released &= ~kJump;
        const float landing = Landing();
        if (st.ground)
        {
            Press(in, 0.f);
            return true;
        }
        if (landing >= 0.f && landing < 1.f && st.vz <= 0.f)
        {
            float when = landing + kLandPad;
            when = when > kMaxPress ? kMaxPress : when;
            Press(in, when);
            return true;
        }
        if (jumpSent)
        {
            Button(in, kJump, false, 0.f);
            in->released |= kJump;
        }
        in->down &= ~kJump;
        jumpSent = false;
        return true;
    }

    float DirectionOffset(uint64_t keys)
    {
        constexpr uint64_t kForward = 1ull << 3, kBack = 1ull << 4, kLeft = 1ull << 9, kRight = 1ull << 10;
        float side = 0.f;
        if (keys & kLeft)
            side += 90.f;
        if (keys & kRight)
            side -= 90.f;
        if (keys & kForward)
            return side * 0.5f;
        if (keys & kBack)
            return 180.f - side * 0.5f;
        return side;
    }

    void Autostrafe(Input* in, const State& st, bool held, float lastForward, float lastLeft)
    {
        if (!misc::Active(movement::autostrafe, misc::strafeBind) || !st.valid || st.ground || !held)
            return;
        constexpr int kSteps = 16;
        const float cap = ConVar(airMaxWishSpeed, 30.f);
        const float dt = kTick / kSteps;
        const float half = ConVar(airAccelerate, 12.f) * st.maxSpeed * dt * 0.5f;
        const float offset = DirectionOffset(realKeys);

        in->down &= ~kMove;
        in->pressed &= ~kMove;
        in->released &= ~kMove;
        Remove(in, [](const Event& e) { return (e.button & kMove) != 0; });
        for (int i = 0; i < in->count; ++i)
            if (!in->events[i].button)
                in->events[i].analog.forward = in->events[i].analog.left = 0.f;

        float vx = st.vx, vy = st.vy;
        float f = lastForward, l = lastLeft;
        for (int i = 0; i < kSteps; ++i)
        {
            Event* e = Insert(in, static_cast<float>(i) / kSteps);
            const float view = e ? e->yaw : in->yaw;
            const float target = Normalize(view + offset);
            const float speed = std::sqrt(vx * vx + vy * vy);
            float wish = target;
            if (speed >= 10.f)
            {
                const float velYaw = std::atan2(vy, vx) / kRad;
                float c = half >= cap ? cap / (2.f * speed) : (cap - half) / speed;
                c = c < -1.f ? -1.f : c > 1.f ? 1.f : c;
                const float theta = std::acos(c) / kRad;
                const float delta = Normalize(target - velYaw);
                if (std::fabs(delta) > 2.f)
                    wish = velYaw + (delta > 0.f ? theta : -theta);
                else
                    wish = velYaw + (strafeSide ? theta : -theta);
                strafeSide = !strafeSide;
            }
            const float rel = Normalize(wish - view) * kRad;
            const float nf = std::cos(rel);
            const float nl = -std::sin(rel);
            if (e)
            {
                e->analog.forward = nf - f;
                e->analog.left = nl - l;
            }
            f = nf;
            l = nl;

            const float wx = std::cos(wish * kRad), wy = std::sin(wish * kRad);
            const float add = cap - (vx * wx + vy * wy);
            if (add > 0.f)
            {
                const float gain = half < add ? half : add;
                vx += wx * gain;
                vy += wy * gain;
            }
        }
        in->forward = f;
        in->left = l;
    }

    void SubtickStrafe(Input* in, const State& st, bool held)
    {
        if (!misc::Active(movement::autostrafe, misc::strafeBind) || !st.valid || st.ground || !held || realSpeed)
            return;
        const float speed0 = std::sqrt(st.vx * st.vx + st.vy * st.vy);
        if (speed0 <= 0.f || st.maxSpeed <= 0.f)
            return;

        in->down = (in->down & ~kMove) | kForwardKey;
        in->pressed &= ~kMove;
        in->released &= ~kMove;
        Remove(in, [](const Event& e) { return (e.button & kMove) != 0; });
        for (int i = 0; i < in->count; ++i)
            if (!in->events[i].button)
                in->events[i].analog.forward = in->events[i].analog.left = 0.f;
        in->forward = 1.f;
        in->left = 0.f;

        constexpr int kSubticks = 32;
        int ticks = kSubticks - in->count;
        ticks = ticks < 1 ? 1 : ticks > kSubticks ? kSubticks : ticks;
        if (in->count >= kMaxEvents)
            return;
        const float dt = kTick / ticks;
        const float maxSpeed = st.maxSpeed;
        const float accel = ConVar(airAccelerate, 12.f);
        const float airCap = ConVar(airMaxWishSpeed, 30.f);
        const float cap = airCap < maxSpeed ? airCap : maxSpeed;
        const float gain = accel * maxSpeed * dt;
        const float view = in->yaw;
        const float target = Normalize(view + DirectionOffset(realKeys));

        float whens[kSubticks]{}, yaws[kSubticks]{};
        int n = 0;
        float vx = st.vx, vy = st.vy;
        for (int i = 1; i <= ticks; ++i)
        {
            const float speed = std::sqrt(vx * vx + vy * vy);
            if (speed <= 0.f)
                break;
            float velYaw = std::atan2(vy, vx) / kRad;
            float c = (cap - gain) / speed;
            c = c < -1.f ? -1.f : c > 1.f ? 1.f : c;
            float ideal = std::acos(c) / kRad;
            ideal = ideal > 90.f ? 90.f : ideal;
            const float delta = Normalize(target - velYaw);
            const float yaw = Normalize((std::fabs(delta) > 170.f && speed > 80.f) || delta > 0.f ? velYaw + ideal : velYaw - ideal);
            whens[n] = static_cast<float>(i * 64 / ticks) / 64.f;
            yaws[n] = i == ticks ? view : yaw;
            ++n;
            if (i == ticks)
                break;
            const float wx = std::cos(yaw * kRad), wy = std::sin(yaw * kRad);
            const float add = cap - (vx * wx + vy * wy);
            if (add <= 0.f)
                continue;
            const float half = gain * 0.5f;
            float applied = add, rest = 0.f;
            if (half <= add)
            {
                applied = half;
                rest = gain <= add ? half : add - half;
            }
            vx += wx * (applied + rest);
            vy += wy * (applied + rest);
        }

        for (int i = 0; i < n; ++i)
        {
            Event* e = Insert(in, whens[i]);
            if (!e)
                break;
            e->pitch = in->pitch;
            e->yaw = yaws[i];
        }
        if (!n)
            return;
        for (int i = 0; i < in->count; ++i)
        {
            Event& e = in->events[i];
            int k = 0;
            while (k + 1 < n && whens[k + 1] <= e.when)
                ++k;
            e.yaw = yaws[k];
        }
    }

    bool SubtickAllowed()
    {
        uint8_t* v = game::ConVarValue(subtickAngles);
        return !v || *v != 0;
    }

    void Edit(Input* in, float lastForward, float lastLeft)
    {
        const State& st = current;
        auto& d = movement::debug;
        const bool held = realHeld;
        d.valid = st.valid;
        d.ground = st.ground;
        d.held = held;
        d.speed = std::sqrt(st.vx * st.vx + st.vy * st.vy);
        d.punch = st.punch[0];
        d.steps = in->count;
        d.stage = 1;
        if (!Bunnyhop(in, st, held))
        {
            d.stage = 4;
            JumpCheck::Run(in, held);
        }
        d.stage = 2;
        if (movement::strafeMode == 1 && SubtickAllowed())
            SubtickStrafe(in, st, held);
        else
            Autostrafe(in, st, held, lastForward, lastLeft);
        d.stage = 0;
        ++d.edits;
    }

    void SafeEdit(Input* in, float lastForward, float lastLeft)
    {
        __try
        {
            Edit(in, lastForward, lastLeft);
        }
        __except (movement::debug.fault = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER)
        {
            movement::debug.faultStage = movement::debug.stage;
            if (movement::debug.stage == 4)
            {
                JumpCheck::enabled = false;
                landingFault = true;
            }
            else
                movement::bhop = movement::autostrafe = false;
        }
    }

    void SafeCmd(void* input, void* raw)
    {
        __try
        {
            movement::debug.stage = 3;
            if (ragebot::OnCmd(static_cast<usercmd::Cmd*>(raw)))
                mem::Call<void, 6>(input, raw);
            movement::debug.stage = 0;
        }
        __except (movement::debug.fault = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER)
        {
            movement::debug.faultStage = movement::debug.stage;
            ragebot::enabled = false;
        }
    }

    bool SafeSample(State* out)
    {
        __try
        {
            *out = Sample();
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            *out = State{};
            return false;
        }
    }

    void __fastcall hkMerge(void* input, int slot)
    {
        if (slot != 0 || activeThread != GetCurrentThreadId())
            return oMerge(input, slot);
        auto in = reinterpret_cast<Input*>(static_cast<uint8_t*>(input) + 0x228);
        const float lastForward = in->forward;
        const float lastLeft = in->left;
        const int frames = mem::At<int>(input, 0xb50);
        oMerge(input, slot);
        if (frames > 0)
        {
            realHeld = (in->down | in->pressed) & kJump;
            realKeys = (in->down | in->pressed) & kMove;
            realSpeed = (in->down & kSpeed) != 0;
        }
        SafeSample(&current);
        SafeEdit(in, lastForward, lastLeft);
        if (aim::Any() && current.valid)
            aim::OnInput(input, in, current.punch);
        if (ragebot::enabled && current.valid)
            ragebot::OnInput(input, in, current.punch);
    }

    void* __fastcall hkGetUserCmd(void* controller, int sequence)
    {
        void* cmd = oGetUserCmd(controller, sequence);
        if (activeThread == GetCurrentThreadId() && !captured)
            captured = cmd;
        return cmd;
    }

    void SafeMisc(void* input)
    {
        __try
        {
            events::Publish(events::CreateMove, 0, input);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            misc::thirdperson = false;
        }
    }

    void* __fastcall hkCreateMove(void* input, int slot, bool active)
    {
        if (slot != 0)
            return oCreateMove(input, slot, active);
        SafeMisc(input);
        if (!movement::bhop && !movement::autostrafe && !ragebot::enabled && !aim::Any() && !JumpCheck::enabled)
            return oCreateMove(input, slot, active);
        SafeSample(&current);
        captured = nullptr;
        activeThread = GetCurrentThreadId();
        void* result = oCreateMove(input, slot, active);
        activeThread = 0;
        ++movement::debug.calls;
        if (captured)
        {
            ++movement::debug.cmds;
            SafeCmd(input, captured);
        }
        return result;
    }
}

void movement::Install()
{
    mem::Module client = mem::Load("client.dll");
    if (!client)
        return;
    uint8_t* vtable = mem::VTable(client, ".?AVCCSGOInput@@");
    patterns::Note(client, "CCSGOInput::vftable", "vtable", vtable);
    void* createMove = vtable ? reinterpret_cast<void**>(vtable)[5] : nullptr;
    void* getUserCmd = patterns::Find(client, "GetUserCmd", "48 89 5C 24 08 57 48 83 EC 20 8B FA E8 ? ? ? ? 48 8B D8 44 8B C7 B8");
    void* merge = patterns::Find(client, "MergeSubtick", "89 54 24 10 48 89 4C 24 08 53 56 57 48 83 EC 70 48 63 DA 48 8D B9 28 02 00 00 48 69 C3 28 09 00 00");
    aimPunch = reinterpret_cast<decltype(aimPunch)>(patterns::Find(client, "GetAimPunch", "48 8B C4 48 89 58 10 48 89 68 18 48 89 70 20 57 48 83 EC 70 48 8B EA 41 0F B6 F0"));
    airAccelerate = game::FindConVar("sv_airaccelerate");
    airMaxWishSpeed = game::FindConVar("sv_air_max_wishspeed");
    gravity = game::FindConVar("sv_gravity");
    maxSpeedVar = game::FindConVar("sv_maxspeed");
    subtickAngles = game::FindConVar("sv_subtick_movement_view_angles");
    if (!createMove || !getUserCmd || !merge || !client.Contains(createMove))
        return;
    if (MH_CreateHook(getUserCmd, &hkGetUserCmd, reinterpret_cast<void**>(&oGetUserCmd)) != MH_OK)
        return;
    if (MH_CreateHook(merge, &hkMerge, reinterpret_cast<void**>(&oMerge)) != MH_OK)
        return;
    if (MH_CreateHook(createMove, &hkCreateMove, reinterpret_cast<void**>(&oCreateMove)) != MH_OK)
        return;
    installed = true;
}
