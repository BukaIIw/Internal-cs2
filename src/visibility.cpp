#include "visibility.h"
#include "core/ray.h"
#include "features/hitbox.h"
#include "mem.h"
#include <atomic>
#include <cstring>

namespace
{
    struct Slot
    {
        std::atomic<uintptr_t> pawn{ 0 };
        std::atomic<bool> visible{ false };
    };

    constexpr int kBones[] = { 6, 5, 4, 2, 0 };

    Slot slots[65];
    uint32_t localHandle = 0xffffffff;

    bool IsPlayer(void* entity)
    {
        for (int i = 1; i <= 64; ++i)
        {
            const char* designer = nullptr;
            void* c = game::EntityAt(i, &designer);
            if (c && designer && !strcmp(designer, "cs_player_controller") && game::Handle(mem::At<uint32_t>(c, game::off.playerPawn)) == entity)
                return mem::At<int>(entity, game::off.health) > 0;
        }
        return false;
    }

    bool Check(void* pawn, const game::Vec3& eye)
    {
        Hitboxes::Box boxes[Hitboxes::kMax];
        const int count = Hitboxes::Collect(pawn, boxes, Hitboxes::kMax);
        for (int i = 0; i < count; ++i)
            if (ray::Clear(eye, boxes[i].center, localHandle, pawn, IsPlayer))
                return true;
        if (count > 0)
            return false;
        for (int bone : kBones)
        {
            game::Vec3 p;
            if (game::Bone(pawn, bone, p) && ray::Clear(eye, p, localHandle, pawn, IsPlayer))
                return true;
        }
        return false;
    }
}

void visibility::OnFrameStage(int stage)
{
    if (stage != 7 || !game::Ready())
        return;
    const auto& o = game::off;
    void* controller = game::LocalController();
    localHandle = controller ? mem::At<uint32_t>(controller, o.playerPawn) : 0xffffffff;
    void* local = controller ? game::Handle(localHandle) : nullptr;
    if (!local)
    {
        Cleanup();
        return;
    }
    const game::Vec3 eye = game::EyePosition(local);
    const int localIndex = game::LocalIndex();
    for (int i = 1; i <= 64; ++i)
    {
        Slot& s = slots[i];
        const char* designer = nullptr;
        void* c = game::EntityAt(i, &designer);
        void* pawn = c && designer && !strcmp(designer, "cs_player_controller") ? game::Handle(mem::At<uint32_t>(c, o.playerPawn)) : nullptr;
        void* node = pawn ? mem::At<void*>(pawn, o.sceneNode) : nullptr;
        if (!pawn || pawn == local || mem::At<int>(pawn, o.health) <= 0 || !node || (o.dormant && mem::At<bool>(node, o.dormant)))
        {
            s.pawn = 0;
            s.visible = false;
            continue;
        }
        bool seen;
        if (ray::Ready())
            seen = Check(pawn, eye);
        else
            seen = game::SpottedBy(pawn, localIndex);
        s.pawn = reinterpret_cast<uintptr_t>(pawn);
        s.visible = seen;
    }
}

bool visibility::Visible(void* pawn)
{
    const auto p = reinterpret_cast<uintptr_t>(pawn);
    for (int i = 1; i <= 64; ++i)
        if (slots[i].pawn == p)
            return slots[i].visible;
    return false;
}

bool visibility::Point(void* pawn, const game::Vec3& eye, const game::Vec3& point)
{
    if (!ray::Ready())
        return game::SpottedBy(pawn, game::LocalIndex());
    return ray::Clear(eye, point, localHandle, pawn, IsPlayer);
}

void visibility::Cleanup()
{
    for (Slot& s : slots)
    {
        s.pawn = 0;
        s.visible = false;
    }
}
