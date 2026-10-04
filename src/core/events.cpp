#include "events.h"
#include "log.h"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <atomic>
#include <memory>
#include <mutex>

namespace
{
    struct subscriber
    {
        const char* name = nullptr;
        events::handler fn = nullptr;
        int priority = 0;
        std::atomic<bool> faulted{ false };
        std::atomic<std::uint64_t> calls{ 0 };
        std::atomic<std::uint64_t> ticks{ 0 };
        std::atomic<std::uint64_t> peak{ 0 };
    };

    constexpr int type_count = static_cast<int>(events::type::count);

    using subscriber_list = std::vector<std::unique_ptr<subscriber>>;

    std::array<subscriber_list, type_count> g_lists{};
    std::mutex g_mutex;

    constexpr const char* g_names[type_count] = {
        "FrameStage",
        "CreateMove",
        "CreateMovePost",
        "OverrideView",
        "Present",
        "LevelInit",
        "LevelShutdown",
        "LocalPawnChanged",
        "MenuToggle",
        "Unload"
    };

    double microseconds_per_tick()
    {
        static const double value = [] {
            LARGE_INTEGER frequency{};
            QueryPerformanceFrequency(&frequency);
            return frequency.QuadPart > 0 ? 1000000.0 / static_cast<double>(frequency.QuadPart) : 0.0;
        }();
        return value;
    }

    bool invoke(events::handler fn, void* args)
    {
        __try
        {
            fn(args);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool valid_type(int t)
    {
        return t >= 0 && t < type_count;
    }
}

void events::subscribe(type t, const char* name, handler h, int priority)
{
    const int index = static_cast<int>(t);
    if (!valid_type(index) || !h)
        return;

    std::lock_guard lock(g_mutex);
    auto& list = g_lists[index];
    const char* label = name ? name : "?";
    for (const auto& s : list)
        if (s->fn == h && !std::strcmp(s->name, label))
            return;

    auto entry = std::make_unique<subscriber>();
    entry->name = label;
    entry->fn = h;
    entry->priority = priority;

    const auto position = std::upper_bound(list.begin(), list.end(), priority, [](int p, const std::unique_ptr<subscriber>& s) { return p < s->priority; });
    list.insert(position, std::move(entry));
}

void events::publish(type t, void* args)
{
    const int index = static_cast<int>(t);
    if (!valid_type(index))
        return;

    for (const auto& s : g_lists[index])
    {
        if (s->faulted.load(std::memory_order_relaxed))
            continue;

        LARGE_INTEGER start{}, end{};
        QueryPerformanceCounter(&start);
        const bool ok = invoke(s->fn, args);
        QueryPerformanceCounter(&end);

        const auto elapsed = static_cast<std::uint64_t>(end.QuadPart > start.QuadPart ? end.QuadPart - start.QuadPart : 0);
        s->calls.fetch_add(1, std::memory_order_relaxed);
        s->ticks.fetch_add(elapsed, std::memory_order_relaxed);
        auto peak = s->peak.load(std::memory_order_relaxed);
        while (elapsed > peak && !s->peak.compare_exchange_weak(peak, elapsed, std::memory_order_relaxed))
        {
        }

        if (!ok)
        {
            s->faulted.store(true, std::memory_order_relaxed);
            logs::Add(logs::Error, "%s faulted in %s", s->name, name(index));
        }
    }
}

const char* events::name(int t)
{
    return valid_type(t) ? g_names[t] : "?";
}

std::vector<events::info> events::snapshot()
{
    std::vector<info> out;
    const double scale = microseconds_per_tick();

    std::lock_guard lock(g_mutex);
    for (int t = 0; t < type_count; ++t)
    {
        const auto& list = g_lists[t];
        for (int i = 0; i < static_cast<int>(list.size()); ++i)
        {
            const auto& s = list[i];
            const auto calls = s->calls.load(std::memory_order_relaxed);
            const auto ticks = s->ticks.load(std::memory_order_relaxed);
            const auto peak = s->peak.load(std::memory_order_relaxed);
            info entry{};
            entry.type = t;
            entry.index = i;
            entry.name = s->name;
            entry.priority = s->priority;
            entry.calls = calls;
            entry.avg_us = calls ? static_cast<double>(ticks) * scale / static_cast<double>(calls) : 0.0;
            entry.max_us = static_cast<double>(peak) * scale;
            entry.faulted = s->faulted.load(std::memory_order_relaxed);
            out.push_back(entry);
        }
    }
    return out;
}

void events::enable(int t, int index)
{
    if (!valid_type(t))
        return;

    std::lock_guard lock(g_mutex);
    auto& list = g_lists[t];
    if (index < 0 || index >= static_cast<int>(list.size()))
        return;

    auto& s = list[index];
    if (!s->faulted.exchange(false, std::memory_order_relaxed))
        return;
    logs::Add(logs::Info, "%s re-enabled in %s", s->name, name(t));
}

void events::reset_stats()
{
    std::lock_guard lock(g_mutex);
    for (auto& list : g_lists)
        for (auto& s : list)
        {
            s->calls.store(0, std::memory_order_relaxed);
            s->ticks.store(0, std::memory_order_relaxed);
            s->peak.store(0, std::memory_order_relaxed);
        }
}
