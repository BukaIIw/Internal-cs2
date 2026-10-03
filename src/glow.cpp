#include "glow.h"
#include "game.h"
#include "mem.h"
#include "visibility.h"
#include <Windows.h>
#include <cstring>

namespace
{
    struct Saved
    {
        void* pawn = nullptr;
        uint32_t handle = 0xffffffff;
        int type = 0;
        int range = 0;
        int rangeMin = 0;
        uint32_t color = 0;
        bool flashing = false;
        bool glowing = false;
        bool wanted = false;
    };

    Saved saved[65];

    uint32_t Pack(const float* c)
    {
        auto ch = [](float v) { return static_cast<uint32_t>((v < 0.f ? 0.f : v > 1.f ? 1.f : v) * 255.f + 0.5f); };
        return ch(c[0]) | ch(c[1]) << 8 | ch(c[2]) << 16 | ch(c[3]) << 24;
    }

    uint8_t* Property(void* pawn)
    {
        return static_cast<uint8_t*>(pawn) + game::off.glow;
    }

    void Save(Saved& s, void* pawn, uint32_t handle)
    {
        const auto& o = game::off;
        uint8_t* g = Property(pawn);
        s.pawn = pawn;
        s.handle = handle;
        s.type = mem::At<int>(g, o.glowType);
        s.range = o.glowRange ? mem::At<int>(g, o.glowRange) : 0;
        s.rangeMin = o.glowRangeMin ? mem::At<int>(g, o.glowRangeMin) : 0;
        s.color = mem::At<uint32_t>(g, o.glowOverride);
        s.flashing = o.glowFlashing ? mem::At<bool>(g, o.glowFlashing) : false;
        s.glowing = mem::At<bool>(g, o.glowing);
    }

    void Restore(Saved& s)
    {
        const auto& o = game::off;
        if (s.pawn && game::Handle(s.handle) == s.pawn)
        {
            uint8_t* g = Property(s.pawn);
            mem::At<int>(g, o.glowType) = s.type;
            if (o.glowRange)
                mem::At<int>(g, o.glowRange) = s.range;
            if (o.glowRangeMin)
                mem::At<int>(g, o.glowRangeMin) = s.rangeMin;
            mem::At<uint32_t>(g, o.glowOverride) = s.color;
            if (o.glowFlashing)
                mem::At<bool>(g, o.glowFlashing) = s.flashing;
            mem::At<bool>(g, o.glowing) = s.glowing;
        }
        s = Saved{};
    }

    void Write(void* pawn, uint32_t color)
    {
        const auto& o = game::off;
        uint8_t* g = Property(pawn);
        mem::At<uint32_t>(g, o.glowOverride) = color;
        mem::At<int>(g, o.glowType) = 3;
        if (o.glowRange)
            mem::At<int>(g, o.glowRange) = 0;
        if (o.glowRangeMin)
            mem::At<int>(g, o.glowRangeMin) = 0;
        if (o.glowFlashing)
            mem::At<bool>(g, o.glowFlashing) = false;
        mem::At<bool>(g, o.glowing) = true;
    }

    void Update()
    {
        const auto& o = game::off;
        if (!o.glow || !o.glowing || !o.glowType || !o.glowOverride)
            return;
        void* localController = game::LocalController();
        void* local = localController ? game::Handle(mem::At<uint32_t>(localController, o.playerPawn)) : nullptr;
        const int localTeam = local ? mem::At<uint8_t>(local, o.teamNum) : 0;
        for (int i = 1; i <= 64; ++i)
        {
            Saved& s = saved[i];
            const char* designer = nullptr;
            void* controller = glow::enabled && local ? game::EntityAt(i, &designer) : nullptr;
            const bool player = controller && designer && !strcmp(designer, "cs_player_controller");
            const uint32_t handle = player ? mem::At<uint32_t>(controller, o.playerPawn) : 0xffffffff;
            void* pawn = player ? game::Handle(handle) : nullptr;
            void* node = pawn ? mem::At<void*>(pawn, o.sceneNode) : nullptr;
            bool want = pawn && pawn != local && node && mem::At<int>(pawn, o.health) > 0;
            if (want && o.dormant && mem::At<bool>(node, o.dormant))
                want = false;
            if (want && o.lifeState && mem::At<uint8_t>(pawn, o.lifeState) != 0)
                want = false;
            const bool team = want && mem::At<uint8_t>(pawn, o.teamNum) == localTeam;
            if (team && !glow::teammates)
                want = false;
            if (s.pawn && (!want || s.pawn != pawn))
                Restore(s);
            if (!want)
                continue;
            if (!s.pawn)
                Save(s, pawn, handle);
            const float* color = team ? glow::teamColor : !glow::byVisibility || visibility::Visible(pawn) ? glow::enemyColor : glow::hiddenColor;
            Write(pawn, Pack(color));
        }
    }

    void RestoreAll()
    {
        for (Saved& s : saved)
            if (s.pawn)
                Restore(s);
    }
}

void glow::OnFrameStage(int stage)
{
    if (stage != 7 || !game::Ready())
        return;
    __try
    {
        Update();
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        enabled = false;
        for (Saved& s : saved)
            s = Saved{};
    }
}

void glow::Cleanup()
{
    enabled = false;
    if (!game::Ready())
        return;
    __try
    {
        RestoreAll();
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
}
