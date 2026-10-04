#include "ray.h"
#include "patterns.h"
#include "../mem.h"
#include <Windows.h>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstring>

namespace
{
    struct Filter
    {
        void* vtable;
        uint64_t mask;
        uint64_t unknown[2];
        uint32_t skip[4];
        uint16_t group;
        uint16_t layer;
        uint16_t objects;
        uint8_t ignore;
        uint8_t channel;
        uint8_t flags;
        uint8_t bits;
        uint8_t pad[6];
        uint8_t sorted;
        uint8_t tail[7];
    };
    static_assert(sizeof(Filter) == 0x48);
    static_assert(offsetof(Filter, skip) == 0x20 && offsetof(Filter, group) == 0x30 && offsetof(Filter, channel) == 0x37 && offsetof(Filter, sorted) == 0x40);

    constexpr uint32_t kHitEntity = 0x8;
    constexpr uint32_t kEnd = 0x84;
    constexpr uint32_t kFraction = 0xAC;

    using TraceShapeFn = bool(__fastcall*)(void*, void*, const game::Vec3*, const game::Vec3*, Filter*, void*);

    TraceShapeFn traceShape = nullptr;
    void** manager = nullptr;
    void* filterVtable = nullptr;
    std::atomic<uint64_t> traces{ 0 }, faults{ 0 };

    bool Run(const game::Vec3& from, const game::Vec3& to, uint32_t skipHandle, uint64_t mask, ray::Hit& out)
    {
        void* mgr = *manager;
        if (!mgr)
            return false;
        alignas(16) uint8_t shape[0x40]{};
        alignas(16) uint8_t trace[0x200]{};
        Filter filter{};
        filter.vtable = filterVtable;
        filter.mask = mask;
        filter.skip[0] = skipHandle;
        filter.skip[1] = filter.skip[2] = filter.skip[3] = 0xffffffff;
        filter.objects = 0xffff;
        filter.channel = 0x0f;
        filter.flags = 0x0b;
        filter.bits = 0x49;
        game::Vec3 a = from, b = to;
        traceShape(mgr, shape, &a, &b, &filter, trace);
        out.entity = mem::At<void*>(trace, kHitEntity);
        out.fraction = mem::At<float>(trace, kFraction);
        out.end = mem::At<game::Vec3>(trace, kEnd);
        return true;
    }
}

bool ray::Init()
{
    mem::Module client = mem::Load("client.dll");
    if (!client)
        return false;
    traceShape = reinterpret_cast<TraceShapeFn>(patterns::Find(client, "TraceShape", "48 89 54 24 10 48 89 4C 24 08 55 53 56 57 41 54 41 56 41 57 48 8D AC 24 ? ? ? ? B8 ? ? ? ? E8 ? ? ? ? 48 2B E0"));
    manager = reinterpret_cast<void**>(patterns::FindRef(client, "GameTraceManager", "4C 8B 3D ? ? ? ? 24 C9 0C 49 66 0F 7F 45", 3, 7));
    filterVtable = mem::VTable(client, ".?AVCTraceFilter@@");
    patterns::Note(client, "CTraceFilter::vftable", "vtable", filterVtable);
    return Ready();
}

bool ray::Ready()
{
    return traceShape && manager && filterVtable;
}

bool ray::Trace(const game::Vec3& from, const game::Vec3& to, uint32_t skipHandle, uint64_t mask, Hit& out)
{
    out = Hit{};
    if (!Ready())
        return false;
    ++traces;
    __try
    {
        return Run(from, to, skipHandle, mask, out);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        ++faults;
        return false;
    }
}

bool ray::Clear(const game::Vec3& from, const game::Vec3& to, uint32_t skipHandle, void* target)
{
    const float dx = to.x - from.x, dy = to.y - from.y, dz = to.z - from.z;
    const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (len < 1.f)
        return true;
    const float ux = dx / len, uy = dy / len, uz = dz / len;
    Hit world;
    if (!Trace(from, to, skipHandle, kMaskWorld, world))
        return false;
    if ((1.f - world.fraction) * len > 2.f)
        return false;
    if (!target)
        return true;
    constexpr float kPast = 16.f;
    const game::Vec3 end{ to.x + ux * kPast, to.y + uy * kPast, to.z + uz * kPast };
    Hit shot;
    if (!Trace(from, end, skipHandle, kMaskShot, shot))
        return false;
    if (shot.entity == target || shot.fraction >= 1.f)
        return true;
    return shot.fraction * (len + kPast) >= len - 8.f;
}

game::Vec3 ray::Forward(float pitch, float yaw, float length)
{
    constexpr float kRad = 3.14159265f / 180.f;
    const float cp = std::cos(pitch * kRad), sp = std::sin(pitch * kRad);
    const float cy = std::cos(yaw * kRad), sy = std::sin(yaw * kRad);
    return { cp * cy * length, cp * sy * length, -sp * length };
}

ray::Stats ray::Snapshot()
{
    return { traces.load(), faults.load() };
}
