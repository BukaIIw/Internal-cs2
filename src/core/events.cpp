#include "events.h"
#include "log.h"
#include <Windows.h>

namespace
{
    struct Subscriber
    {
        const char* name;
        events::Handler handler;
        bool faulted;
        uint64_t calls;
        uint64_t ticks;
        uint64_t peak;
    };

    constexpr int kMax = 16;
    Subscriber table[events::Count][kMax]{};
    int counts[events::Count]{};

    double Frequency()
    {
        static double us = [] {
            LARGE_INTEGER f{};
            QueryPerformanceFrequency(&f);
            return 1000000.0 / static_cast<double>(f.QuadPart);
        }();
        return us;
    }

    bool Invoke(events::Handler handler, int arg, void* data)
    {
        __try
        {
            handler(arg, data);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }
}

void events::Subscribe(Type type, const char* name, Handler handler)
{
    if (type < 0 || type >= Count || counts[type] >= kMax)
        return;
    for (int i = 0; i < counts[type]; ++i)
        if (table[type][i].handler == handler)
            return;
    table[type][counts[type]++] = { name, handler, false, 0, 0, 0 };
}

void events::Publish(Type type, int arg, void* data)
{
    if (type < 0 || type >= Count)
        return;
    for (int i = 0; i < counts[type]; ++i)
    {
        Subscriber& s = table[type][i];
        if (s.faulted)
            continue;
        LARGE_INTEGER a{}, b{};
        QueryPerformanceCounter(&a);
        const bool ok = Invoke(s.handler, arg, data);
        QueryPerformanceCounter(&b);
        const uint64_t dt = static_cast<uint64_t>(b.QuadPart - a.QuadPart);
        ++s.calls;
        s.ticks += dt;
        if (dt > s.peak)
            s.peak = dt;
        if (!ok)
        {
            s.faulted = true;
            logs::Add(logs::Error, "%s stopped after an exception in %s", s.name, Name(type));
        }
    }
}

const char* events::Name(int type)
{
    static const char* kNames[] = { "FrameStage", "CreateMove", "Present", "Unload", "LevelInit", "LevelShutdown", "PawnChanged", "MenuToggle" };
    return type >= 0 && type < Count ? kNames[type] : "?";
}

std::vector<events::Info> events::Snapshot()
{
    std::vector<Info> out;
    const double us = Frequency();
    for (int t = 0; t < Count; ++t)
        for (int i = 0; i < counts[t]; ++i)
        {
            const Subscriber& s = table[t][i];
            out.push_back({ t, i, s.name, s.calls, s.calls ? static_cast<double>(s.ticks) * us / static_cast<double>(s.calls) : 0.0, static_cast<double>(s.peak) * us, s.faulted });
        }
    return out;
}

void events::Enable(int type, int index)
{
    if (type < 0 || type >= Count || index < 0 || index >= counts[type])
        return;
    table[type][index].faulted = false;
    logs::Add(logs::Info, "%s re-enabled", table[type][index].name);
}

void events::ResetStats()
{
    for (int t = 0; t < Count; ++t)
        for (int i = 0; i < counts[t]; ++i)
        {
            table[t][i].calls = 0;
            table[t][i].ticks = 0;
            table[t][i].peak = 0;
        }
}
