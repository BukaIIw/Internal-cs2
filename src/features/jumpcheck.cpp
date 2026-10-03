#include "jumpcheck.h"
#include "schema.h"
#include "../core/ray.h"
#include "../core/settings.h"
#include "../game.h"
#include "../mem.h"
#include <cmath>

namespace
{
    using namespace subtick;

    constexpr uint64_t kJump = 1ull << 1;
    constexpr float kTick = 1.f / 64.f;
    constexpr float kHull = 15.f;
    constexpr float kProbe = 256.f;
    constexpr float kMinFrac = 1.f / 64.f;

    uint8_t* gravity = nullptr;
    uint8_t* precision = nullptr;
    uint32_t modernJump = 0;
    uint32_t landedTick = 0, landedFrac = 0, pressTick = 0, pressFrac = 0;
    float memoryZ = 0.f;
    bool hasMemory = false;
    bool sent = false;
    bool ready = false;

    bool GroundBelow(const game::Vec3& origin, uint32_t skip, float& z)
    {
        if (!ray::Ready())
            return false;
        static const float offsets[5][2] = { { 0.f, 0.f }, { kHull, kHull }, { kHull, -kHull }, { -kHull, kHull }, { -kHull, -kHull } };
        bool any = false;
        for (const auto& o : offsets)
        {
            const game::Vec3 from{ origin.x + o[0], origin.y + o[1], origin.z + 2.f };
            const game::Vec3 to{ from.x, from.y, origin.z - kProbe };
            ray::Hit hit;
            if (!ray::Trace(from, to, skip, ray::kMaskWorld, hit) || hit.fraction >= 1.f)
                continue;
            if (!any || hit.end.z > z)
                z = hit.end.z;
            any = true;
        }
        return any;
    }
}

bool JumpCheck::Init()
{
    gravity = game::FindConVar("sv_gravity");
    precision = game::FindConVar("sv_jump_precision_enable");
    const char* client = "client.dll";
    modernJump = schema::Field(client, "CCSPlayer_MovementServices::m_ModernJump", "48 89 AB F4 06 00 00 48 89 83 ? ? ? ? 33 C0", 10);
    landedTick = schema::Field(client, "CCSPlayerModernJump::m_nLastLandedTick", "89 41 ? 8B 42 50 89 41 24 8B 42 30 89 41 08", 2, 1);
    landedFrac = schema::Field(client, "CCSPlayerModernJump::m_flLastLandedFrac", "89 41 ? 8B 42 30 89 41 08 8B 42 34 89 41 0C", 2, 1);
    pressTick = schema::Field(client, "CCSPlayerModernJump::m_nLastActualJumpPressTick", "89 41 ? 8B 42 3C 89 41 14 8B 42 40 89 41 04", 2, 1);
    pressFrac = schema::Field(client, "CCSPlayerModernJump::m_flLastActualJumpPressFrac", "89 41 ? 8B 42 40 89 41 04 8B 42 7C 89 41 7C", 2, 1);
    ready = modernJump && landedTick && landedFrac && pressTick && pressFrac;
    return ready;
}

bool JumpCheck::Ready()
{
    return ready;
}

void JumpCheck::Register()
{
    settings::Bool("jump.enabled", &enabled);
    settings::Bool("jump.trace_ground", &traceGround);
    settings::Float("jump.lead", &lead);
}

float JumpCheck::LandingFraction(void* pawn, Info& info)
{
    info = Info{};
    if (!pawn || mem::At<int>(pawn, game::off.health) <= 0 || mem::At<uint8_t>(pawn, game::off.moveType) != 2)
        return -1.f;
    void* node = mem::At<void*>(pawn, game::off.sceneNode);
    if (!node)
        return -1.f;
    info.valid = true;
    info.precision = !precision || game::ConVarFloat(precision, 1.f) != 0.f;
    if (void* ms = mem::At<void*>(pawn, game::off.movementServices))
        if (modernJump)
        {
            auto jump = static_cast<uint8_t*>(ms) + modernJump;
            info.landedTick = mem::At<int>(jump, landedTick);
            info.landedFrac = mem::At<float>(jump, landedFrac);
            info.pressTick = mem::At<int>(jump, pressTick);
            info.pressFrac = mem::At<float>(jump, pressFrac);
        }

    const game::Vec3 origin{ mem::At<float>(node, game::off.absOrigin), mem::At<float>(node, game::off.absOrigin + 4), mem::At<float>(node, game::off.absOrigin + 8) };
    const float vz = mem::At<float>(pawn, game::off.absVelocity + 8);
    info.ground = mem::At<uint32_t>(pawn, game::off.flags) & 1;
    if (info.ground)
    {
        memoryZ = origin.z;
        hasMemory = true;
        info.groundZ = origin.z;
        info.fraction = 0.f;
        return 0.f;
    }
    if (vz > 0.f)
        return -1.f;

    float groundZ = 0.f;
    const uint32_t skip = mem::At<uint32_t>(game::LocalController(), game::off.playerPawn);
    if (!(traceGround && GroundBelow(origin, skip, groundZ)))
    {
        if (!hasMemory)
            return -1.f;
        groundZ = memoryZ;
    }
    info.groundZ = groundZ;

    float scale = game::off.gravityScale ? mem::At<float>(pawn, game::off.gravityScale) : 1.f;
    scale = scale <= 0.f || scale > 10.f ? 1.f : scale;
    const float g = game::ConVarFloat(gravity, 800.f) * scale;
    const float h = origin.z - groundZ;
    if (h < 0.f)
        return -1.f;
    const float a = 0.5f * g, b = -vz;
    const float disc = b * b + 4.f * a * h;
    float t = a > 0.f ? (-b + std::sqrt(disc)) / (2.f * a) : (b > 0.f ? h / b : 1e9f);
    t = t < 0.f ? 0.f : t;
    float frac = t / kTick - lead;
    if (frac > 1.f)
        return -1.f;
    frac = frac < 0.f ? 0.f : frac;
    info.fraction = frac;
    return frac;
}

bool JumpCheck::Run(Input* in, bool held)
{
    if (!enabled || !in)
        return false;
    if (!held)
    {
        sent = (in->down & kJump) != 0;
        return false;
    }
    void* controller = game::LocalController();
    void* pawn = controller ? game::Handle(mem::At<uint32_t>(controller, game::off.playerPawn)) : nullptr;
    const float frac = LandingFraction(pawn, last);
    if (!last.valid)
        return false;

    Remove(in, [](const Event& e) { return e.button == kJump; });
    in->pressed &= ~kJump;
    in->released &= ~kJump;
    if (frac >= 0.f)
    {
        float when = frac;
        if (sent)
        {
            Button(in, kJump, false, 0.f);
            in->released |= kJump;
            when = when < kMinFrac ? kMinFrac : when;
        }
        Button(in, kJump, true, when);
        in->pressed |= kJump;
        in->down |= kJump;
        sent = true;
        return true;
    }
    if (sent)
    {
        Button(in, kJump, false, 0.f);
        in->released |= kJump;
    }
    in->down &= ~kJump;
    sent = false;
    return true;
}
