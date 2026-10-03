#include "settings.h"
#include "log.h"
#include "../misc.h"
#include <Windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{
    enum Kind
    {
        KBool,
        KInt,
        KFloat,
        KColor,
        KKey
    };

    struct Var
    {
        const char* name;
        Kind kind;
        void* ptr;
    };

    std::vector<Var>& Vars()
    {
        static std::vector<Var> vars;
        return vars;
    }

    void Add(const char* name, Kind kind, void* ptr)
    {
        for (auto& v : Vars())
            if (!strcmp(v.name, name))
            {
                v.ptr = ptr;
                v.kind = kind;
                return;
            }
        Vars().push_back({ name, kind, ptr });
    }

    std::string& PathString()
    {
        static std::string path;
        if (path.empty())
        {
            char buf[MAX_PATH]{};
            GetEnvironmentVariableA("APPDATA", buf, MAX_PATH);
            std::string dir = std::string(buf) + "\\Internal-cs2";
            CreateDirectoryA(dir.c_str(), nullptr);
            path = dir + "\\settings.ini";
        }
        return path;
    }
}

void settings::Bool(const char* name, bool* value) { Add(name, KBool, value); }
void settings::Int(const char* name, int* value) { Add(name, KInt, value); }
void settings::Float(const char* name, float* value) { Add(name, KFloat, value); }
void settings::Color(const char* name, float* rgba) { Add(name, KColor, rgba); }
void settings::Key(const char* name, misc::Bind* bind) { Add(name, KKey, bind); }

const char* settings::Path()
{
    return PathString().c_str();
}

bool settings::Save()
{
    FILE* f = nullptr;
    if (fopen_s(&f, Path(), "wb") || !f)
    {
        logs::Add(logs::Error, "Config: cannot write %s", Path());
        return false;
    }
    for (auto& v : Vars())
    {
        switch (v.kind)
        {
        case KBool: fprintf(f, "%s=%d\n", v.name, *static_cast<bool*>(v.ptr) ? 1 : 0); break;
        case KInt: fprintf(f, "%s=%d\n", v.name, *static_cast<int*>(v.ptr)); break;
        case KFloat: fprintf(f, "%s=%.4f\n", v.name, *static_cast<float*>(v.ptr)); break;
        case KColor:
        {
            const float* c = static_cast<float*>(v.ptr);
            fprintf(f, "%s=%.4f,%.4f,%.4f,%.4f\n", v.name, c[0], c[1], c[2], c[3]);
            break;
        }
        case KKey:
        {
            auto b = static_cast<misc::Bind*>(v.ptr);
            fprintf(f, "%s=%d,%d\n", v.name, b->key, b->mode);
            break;
        }
        }
    }
    fclose(f);
    logs::Add(logs::Success, "Config saved");
    return true;
}

bool settings::Load()
{
    FILE* f = nullptr;
    if (fopen_s(&f, Path(), "rb") || !f)
        return false;
    char line[256];
    while (fgets(line, sizeof(line), f))
    {
        char* eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = 0;
        const char* value = eq + 1;
        for (auto& v : Vars())
        {
            if (strcmp(v.name, line))
                continue;
            switch (v.kind)
            {
            case KBool: *static_cast<bool*>(v.ptr) = atoi(value) != 0; break;
            case KInt: *static_cast<int*>(v.ptr) = atoi(value); break;
            case KFloat: *static_cast<float*>(v.ptr) = static_cast<float>(atof(value)); break;
            case KColor:
            {
                float* c = static_cast<float*>(v.ptr);
                sscanf_s(value, "%f,%f,%f,%f", &c[0], &c[1], &c[2], &c[3]);
                break;
            }
            case KKey:
            {
                auto b = static_cast<misc::Bind*>(v.ptr);
                int key = 0, mode = 0;
                if (sscanf_s(value, "%d,%d", &key, &mode) == 2)
                {
                    b->key = key;
                    b->mode = mode < 0 || mode > 2 ? 0 : mode;
                    b->toggled = false;
                }
                break;
            }
            }
            break;
        }
    }
    fclose(f);
    logs::Add(logs::Info, "Config loaded");
    return true;
}
