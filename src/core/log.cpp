#include "log.h"
#include <Windows.h>
#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace
{
    constexpr size_t kMax = 200;
    constexpr uint64_t kLifetime = 10 * 60 * 1000;
    std::mutex lock;
    std::vector<logs::Line> lines;

    void Trim(uint64_t now)
    {
        size_t drop = 0;
        while (drop < lines.size() && (lines.size() - drop > kMax || now - lines[drop].time > kLifetime))
            ++drop;
        if (drop)
            lines.erase(lines.begin(), lines.begin() + drop);
    }
}

void logs::Add(int level, const char* fmt, ...)
{
    Line line{};
    line.time = GetTickCount64();
    line.level = level;
    va_list args;
    va_start(args, fmt);
    vsnprintf(line.text, sizeof(line.text), fmt, args);
    va_end(args);
    std::lock_guard l(lock);
    lines.push_back(line);
    Trim(line.time);
}

std::vector<logs::Line> logs::Snapshot()
{
    std::lock_guard l(lock);
    Trim(GetTickCount64());
    return lines;
}

void logs::Clear()
{
    std::lock_guard l(lock);
    lines.clear();
}
