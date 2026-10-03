#include "core/patterns.h"
#include "hands.h"
#include "game.h"
#include "mem.h"
#include "MinHook.h"
#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>

namespace
{
    constexpr uint32_t kInvalid = 0xffffffff;
    constexpr int kStride = 0x70;
    constexpr int kObject = 0x18;
    constexpr int kColor = 0x50;
    constexpr int kScan = 0x400;
    constexpr int kConfirm = 8;

    using DrawArrayFn = void(__fastcall*)(void*, void*, uint8_t*, int, void*, void*, void*, void*);
    DrawArrayFn oDrawArray = nullptr;

    struct Target
    {
        std::atomic<uint32_t> handle{ kInvalid };
        std::atomic<uintptr_t> entity{ 0 };
        std::atomic<uint32_t> altHandle{ kInvalid };
        std::atomic<uintptr_t> altEntity{ 0 };
        std::atomic<uint32_t> tint{ 0 };
    };

    Target armsTarget, weaponTarget;
    std::atomic<int> ownerOffset{ -1 };
    std::atomic<int> ownerKind{ 0 };
    std::atomic<int> candidateOffset{ -1 };
    std::atomic<int> candidateKind{ 0 };
    std::atomic<int> candidateHits{ 0 };
    std::atomic<unsigned> tinted{ 0 }, calls{ 0 };

    struct Restore
    {
        uint32_t handle = kInvalid;
        uint32_t color = 0xffffffff;
    };
    Restore armsRestore, weaponRestore;

    uint32_t Pack(const float* c)
    {
        auto ch = [](float v) { return static_cast<uint32_t>((v < 0.f ? 0.f : v > 1.f ? 1.f : v) * 255.f + 0.5f); };
        return ch(c[0]) | ch(c[1]) << 8 | ch(c[2]) << 16 | ch(c[3]) << 24;
    }

    bool Matches(const uint8_t* object, int off, int kind, const Target& t)
    {
        if (kind == 1)
        {
            const uint32_t v = *reinterpret_cast<const uint32_t*>(object + off);
            return v != kInvalid && (v == t.handle || v == t.altHandle);
        }
        const uintptr_t v = *reinterpret_cast<const uintptr_t*>(object + off);
        return v && (v == t.entity || v == t.altEntity);
    }

    void Calibrate(const uint8_t* object)
    {
        const uint32_t h = armsTarget.handle;
        const uintptr_t e = armsTarget.entity;
        if (h == kInvalid || !e)
            return;
        int found = -1, kind = 0;
        __try
        {
            for (int off = 0; off + 8 <= kScan && found < 0; off += 4)
            {
                if (!(off & 7) && *reinterpret_cast<const uintptr_t*>(object + off) == e)
                {
                    found = off;
                    kind = 2;
                }
                else if (*reinterpret_cast<const uint32_t*>(object + off) == h)
                {
                    found = off;
                    kind = 1;
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return;
        }
        if (found < 0)
            return;
        if (candidateOffset == found && candidateKind == kind)
        {
            if (++candidateHits >= kConfirm)
            {
                ownerKind = kind;
                ownerOffset = found;
            }
        }
        else
        {
            candidateOffset = found;
            candidateKind = kind;
            candidateHits = 1;
        }
    }

    void Tint(uint8_t* meshes, int count)
    {
        const uint32_t at = armsTarget.tint, wt = weaponTarget.tint;
        if (!at && !wt)
            return;
        const int off = ownerOffset, kind = ownerKind;
        unsigned n = 0;
        for (int i = 0; i < count; ++i)
        {
            uint8_t* mesh = meshes + i * kStride;
            const uint8_t* object = *reinterpret_cast<uint8_t**>(mesh + kObject);
            if (!object)
                continue;
            if (off < 0)
            {
                Calibrate(object);
                continue;
            }
            if (at && Matches(object, off, kind, armsTarget))
                *reinterpret_cast<uint32_t*>(mesh + kColor) = at;
            else if (wt && Matches(object, off, kind, weaponTarget))
                *reinterpret_cast<uint32_t*>(mesh + kColor) = wt;
            else
                continue;
            ++n;
        }
        if (n)
            tinted += n;
    }

    void __fastcall hkDrawArray(void* desc, void* ctx, uint8_t* meshes, int count, void* view, void* layer, void* a7, void* a8)
    {
        ++calls;
        if (meshes && count > 0)
        {
            __try
            {
                Tint(meshes, count);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
            }
        }
        oDrawArray(desc, ctx, meshes, count, view, layer, a7, a8);
    }

    uint32_t FindViewmodel(uint32_t pawnHandle)
    {
        static uint32_t cached = kInvalid;
        const uint32_t ownerOff = game::off.ownerEntity;
        if (!ownerOff || pawnHandle == kInvalid)
            return kInvalid;
        if (cached != kInvalid)
            if (void* e = game::Handle(cached))
                if (mem::At<uint32_t>(e, ownerOff) == pawnHandle)
                    return cached;
        cached = kInvalid;
        for (int i = 65; i < 8192; ++i)
        {
            const char* designer = nullptr;
            void* e = game::EntityAt(i, &designer);
            if (!e || !designer || !strstr(designer, "viewmodel"))
                continue;
            if (mem::At<uint32_t>(e, ownerOff) != pawnHandle)
                continue;
            const uint32_t self = game::HandleAt(i);
            if (game::Handle(self) == e)
                cached = self;
            break;
        }
        return cached;
    }

    void SetRenderColor(Restore& r, uint32_t handle, uint32_t tint)
    {
        const uint32_t off = game::off.clrRender;
        if (!off)
            return;
        if (r.handle != kInvalid && (r.handle != handle || !tint))
        {
            if (void* old = game::Handle(r.handle))
                mem::At<uint32_t>(old, off) = r.color;
            r.handle = kInvalid;
        }
        void* e = handle != kInvalid ? game::Handle(handle) : nullptr;
        if (!e || !tint)
            return;
        if (r.handle != handle)
        {
            r.handle = handle;
            r.color = mem::At<uint32_t>(e, off);
        }
        mem::At<uint32_t>(e, off) = tint;
    }
}

void hands::Install()
{
    mem::Module scene = mem::Load("scenesystem.dll");
    if (!scene)
        return;
    auto vtable = reinterpret_cast<void**>(mem::VTable(scene, ".?AVCAnimatableSceneObjectDesc@@"));
    patterns::Note(scene, "CAnimatableSceneObjectDesc::vftable", "vtable", vtable);
    void* draw = vtable ? vtable[1] : nullptr;
    if (draw && scene.Contains(draw) && MH_CreateHook(draw, &hkDrawArray, reinterpret_cast<void**>(&oDrawArray)) == MH_OK)
        debug.hooked = true;
}

void hands::OnFrameStage(int stage)
{
    const auto& o = game::off;
    if (stage != 7 || !game::Ready())
        return;
    void* controller = game::LocalController();
    const uint32_t pawnHandle = controller ? mem::At<uint32_t>(controller, o.playerPawn) : kInvalid;
    void* pawn = controller ? game::Handle(pawnHandle) : nullptr;
    uint32_t vm = kInvalid, att = kInvalid, wep = kInvalid;
    debug.source = 0;
    if (pawn)
    {
        if (o.viewModelServices && o.viewModel)
            if (auto services = mem::At<uint8_t*>(pawn, o.viewModelServices))
            {
                vm = mem::At<uint32_t>(services, o.viewModel);
                if (vm != kInvalid && game::Handle(vm))
                    debug.source = 1;
            }
        if (!debug.source)
        {
            vm = FindViewmodel(pawnHandle);
            if (vm != kInvalid)
                debug.source = 2;
        }
        if (auto services = mem::At<uint8_t*>(pawn, o.weaponServices))
        {
            wep = mem::At<uint32_t>(services, o.activeWeapon);
            void* weaponEntity = game::Handle(wep);
            if (weaponEntity && o.viewmodelAttachment)
                att = mem::At<uint32_t>(weaponEntity, o.viewmodelAttachment);
        }
    }
    void* vmEntity = vm != kInvalid ? game::Handle(vm) : nullptr;
    void* attEntity = att != kInvalid ? game::Handle(att) : nullptr;
    void* wepEntity = wep != kInvalid ? game::Handle(wep) : nullptr;

    armsTarget.handle = vmEntity ? vm : kInvalid;
    armsTarget.entity = reinterpret_cast<uintptr_t>(vmEntity);
    weaponTarget.handle = attEntity ? att : kInvalid;
    weaponTarget.entity = reinterpret_cast<uintptr_t>(attEntity);
    weaponTarget.altHandle = wepEntity ? wep : kInvalid;
    weaponTarget.altEntity = reinterpret_cast<uintptr_t>(wepEntity);

    const uint32_t at = arms ? Pack(armsColor) : 0;
    const uint32_t wt = weapon ? Pack(weaponColor) : 0;
    armsTarget.tint = at;
    weaponTarget.tint = wt;
    SetRenderColor(armsRestore, armsTarget.handle, at);
    SetRenderColor(weaponRestore, weaponTarget.handle, wt);

    debug.viewmodel = vmEntity != nullptr;
    debug.attachment = attEntity != nullptr;
    debug.ownerOffset = ownerOffset;
    debug.ownerKind = ownerKind;
    debug.candidate = candidateOffset;
    debug.tinted = tinted.exchange(0);
    debug.calls = calls.exchange(0);
}

void hands::Cleanup()
{
    if (!game::Ready())
        return;
    SetRenderColor(armsRestore, kInvalid, 0);
    SetRenderColor(weaponRestore, kInvalid, 0);
}
