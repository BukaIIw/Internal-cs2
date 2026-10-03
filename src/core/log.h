#pragma once
#include <cstdint>
#include <vector>

namespace logs
{
    enum Level
    {
        Info,
        Success,
        Warning,
        Error
    };

    struct Line
    {
        uint64_t time;
        int level;
        char text[160];
    };

    void Add(int level, const char* fmt, ...);
    std::vector<Line> Snapshot();
    void Clear();
}
