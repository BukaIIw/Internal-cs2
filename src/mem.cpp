#include "mem.h"
#include <Windows.h>
#include <cstring>
#include <cstdlib>

namespace
{
    std::vector<int> Parse(const char* p)
    {
        std::vector<int> out;
        while (*p)
        {
            if (*p == ' ')
            {
                ++p;
                continue;
            }
            if (*p == '?')
            {
                out.push_back(-1);
                while (*p == '?')
                    ++p;
                continue;
            }
            char* next = nullptr;
            out.push_back(static_cast<int>(strtoul(p, &next, 16)));
            p = next;
        }
        return out;
    }

    uint8_t* FindBytes(uint8_t* from, uint8_t* to, const void* bytes, size_t len)
    {
        auto b = static_cast<const uint8_t*>(bytes);
        while (from + len <= to)
        {
            from = static_cast<uint8_t*>(memchr(from, b[0], to - from - len + 1));
            if (!from)
                return nullptr;
            if (!memcmp(from, b, len))
                return from;
            ++from;
        }
        return nullptr;
    }
}

mem::Module mem::Load(const char* name)
{
    Module m;
    auto base = reinterpret_cast<uint8_t*>(GetModuleHandleA(name));
    if (!base)
        return m;
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    m.name = name;
    m.base = base;
    m.size = nt->OptionalHeader.SizeOfImage;
    auto sec = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec)
    {
        char n[9]{};
        memcpy(n, sec->Name, 8);
        Section s{ base + sec->VirtualAddress, sec->Misc.VirtualSize };
        if (!strcmp(n, ".text"))
            m.text = s;
        else if (!strcmp(n, ".rdata"))
            m.rdata = s;
        else if (!strcmp(n, ".data"))
            m.data = s;
    }
    return m;
}

std::vector<uint8_t*> mem::FindAll(const Section& s, const char* pattern, size_t limit)
{
    std::vector<uint8_t*> out;
    auto pat = Parse(pattern);
    if (pat.empty() || !s.start || s.size < pat.size())
        return out;
    const size_t n = pat.size();
    uint8_t* last = s.start + s.size - n;
    for (uint8_t* p = s.start; p <= last; ++p)
    {
        if (pat[0] >= 0)
        {
            p = static_cast<uint8_t*>(memchr(p, pat[0], last - p + 1));
            if (!p)
                break;
        }
        size_t i = 1;
        while (i < n && (pat[i] < 0 || p[i] == pat[i]))
            ++i;
        if (i == n)
        {
            out.push_back(p);
            if (out.size() >= limit)
                break;
        }
    }
    return out;
}

uint8_t* mem::Find(const Section& s, const char* pattern)
{
    auto r = FindAll(s, pattern, 1);
    return r.empty() ? nullptr : r[0];
}

uint8_t* mem::Rel(uint8_t* insn, int dispOffset, int length)
{
    if (!insn)
        return nullptr;
    return insn + length + *reinterpret_cast<int32_t*>(insn + dispOffset);
}

uint8_t* mem::VTable(const Module& m, const char* typeName)
{
    if (!m)
        return nullptr;
    const size_t len = strlen(typeName) + 1;
    uint8_t* dataEnd = m.data.start + m.data.size;
    uint8_t* rdataEnd = m.rdata.start + m.rdata.size;
    for (uint8_t* name = FindBytes(m.data.start, dataEnd, typeName, len); name; name = FindBytes(name + 1, dataEnd, typeName, len))
    {
        uint8_t* td = name - 0x10;
        if (reinterpret_cast<uintptr_t>(td) & 7)
            continue;
        const uint32_t tdRva = static_cast<uint32_t>(td - m.base);
        for (uint8_t* p = m.rdata.start; p + 24 <= rdataEnd; p += 4)
        {
            auto col = reinterpret_cast<const uint32_t*>(p);
            if (col[0] != 1 || col[1] != 0 || col[3] != tdRva || col[5] != static_cast<uint32_t>(p - m.base))
                continue;
            const uint64_t va = reinterpret_cast<uint64_t>(p);
            for (uint8_t* q = m.rdata.start; q + 16 <= rdataEnd; q += 8)
                if (*reinterpret_cast<uint64_t*>(q) == va)
                    return q + 8;
        }
    }
    return nullptr;
}

uint8_t* mem::StaticInstance(const Module& m, const uint8_t* vtable)
{
    if (!vtable)
        return nullptr;
    const uint64_t va = reinterpret_cast<uint64_t>(vtable);
    uint8_t* end = m.data.start + m.data.size;
    for (uint8_t* p = m.data.start; p + 8 <= end; p += 8)
        if (*reinterpret_cast<uint64_t*>(p) == va)
            return p;
    return nullptr;
}

void* mem::Interface(const char* module, const char* name)
{
    auto h = GetModuleHandleA(module);
    if (!h)
        return nullptr;
    using Fn = void* (*)(const char*, int*);
    auto fn = reinterpret_cast<Fn>(GetProcAddress(h, "CreateInterface"));
    return fn ? fn(name, nullptr) : nullptr;
}
