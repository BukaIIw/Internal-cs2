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
    FILE* file = nullptr;
    bool file_tried = false;

    void Write(const logs::Line& line)
    {
        if (!file_tried)
        {
            file_tried = true;
            char dir[MAX_PATH]{};
            if (GetEnvironmentVariableA("APPDATA", dir, MAX_PATH))
            {
                char path[MAX_PATH]{};
                snprintf(path, sizeof(path), "%s\\Internal-cs2", dir);
                CreateDirectoryA(path, nullptr);
                snprintf(path, sizeof(path), "%s\\Internal-cs2\\log.txt", dir);
                file = fopen(path, "w");
            }
        }
        if (!file)
            return;
        static const char* names[] = {"INFO", "OK", "WARN", "ERROR"};
        const char* name = line.level >= 0 && line.level < 4 ? names[line.level] : "?";
        fprintf(file, "[%llu] %s %s\n", static_cast<unsigned long long>(line.time), name, line.text);
        fflush(file);
    }

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
    Write(line);
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
