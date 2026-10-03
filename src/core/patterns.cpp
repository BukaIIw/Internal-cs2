#include "patterns.h"
#include "log.h"
#include "settings.h"
#include "../../deps/minhook/src/hde/hde64.h"
#include <Windows.h>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <thread>

namespace
{
    struct Record
    {
        patterns::Entry entry;
    };

    struct Image
    {
        std::vector<uint8_t> text;
        uint32_t rva = 0;
        bool loaded = false;
    };

    struct Job
    {
        enum Kind { Verify, Generate, Custom } kind;
        int index;
        std::string module;
        uint32_t rva;
    };

    struct Pattern
    {
        std::vector<uint8_t> bytes;
        std::vector<uint8_t> mask;
    };

    std::mutex lock;
    std::vector<Record> records;
    std::map<std::string, Image> images;
    std::deque<Job> jobs;
    std::condition_variable wake;
    std::thread worker;
    std::atomic<bool> stop{ false };
    std::atomic<int> running{ 0 };
    std::string custom;
    bool customPending = false;

    Pattern Parse(const std::string& s)
    {
        Pattern p;
        const char* c = s.c_str();
        while (*c)
        {
            if (*c == ' ')
            {
                ++c;
                continue;
            }
            if (*c == '?')
            {
                while (*c == '?')
                    ++c;
                p.bytes.push_back(0);
                p.mask.push_back(0);
                continue;
            }
            char* next = nullptr;
            p.bytes.push_back(static_cast<uint8_t>(strtoul(c, &next, 16)));
            p.mask.push_back(1);
            if (next == c)
                break;
            c = next;
        }
        return p;
    }

    std::string Format(const Pattern& p)
    {
        size_t n = p.bytes.size();
        while (n && !p.mask[n - 1])
            --n;
        std::string out;
        char hex[4];
        for (size_t i = 0; i < n; ++i)
        {
            if (i)
                out += ' ';
            if (p.mask[i])
            {
                snprintf(hex, sizeof(hex), "%02X", p.bytes[i]);
                out += hex;
            }
            else
                out += '?';
        }
        return out;
    }

    int Count(const std::vector<uint8_t>& text, const Pattern& p, int limit)
    {
        const size_t n = p.bytes.size();
        if (!n || text.size() < n)
            return 0;
        size_t anchor = 0;
        while (anchor < n && !p.mask[anchor])
            ++anchor;
        if (anchor == n)
            return limit;
        const uint8_t* data = text.data();
        const uint8_t* last = data + text.size() - n;
        int found = 0;
        for (const uint8_t* q = data + anchor; q <= last + anchor; ++q)
        {
            q = static_cast<const uint8_t*>(memchr(q, p.bytes[anchor], last + anchor - q + 1));
            if (!q)
                break;
            const uint8_t* s = q - anchor;
            size_t i = 0;
            while (i < n && (!p.mask[i] || s[i] == p.bytes[i]))
                ++i;
            if (i == n && ++found >= limit)
                break;
        }
        return found;
    }

    Image* LoadImage(const std::string& module)
    {
        Image& img = images[module];
        if (img.loaded)
            return img.text.empty() ? nullptr : &img;
        img.loaded = true;
        HMODULE h = GetModuleHandleA(module.c_str());
        char path[MAX_PATH]{};
        if (!h || !GetModuleFileNameA(h, path, MAX_PATH))
            return nullptr;
        HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return nullptr;
        auto base = reinterpret_cast<uint8_t*>(h);
        auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
        auto sec = IMAGE_FIRST_SECTION(nt);
        for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec)
        {
            if (memcmp(sec->Name, ".text", 6))
                continue;
            const DWORD size = sec->SizeOfRawData < sec->Misc.VirtualSize ? sec->SizeOfRawData : sec->Misc.VirtualSize;
            img.text.resize(size);
            img.rva = sec->VirtualAddress;
            LARGE_INTEGER at{};
            at.QuadPart = sec->PointerToRawData;
            DWORD read = 0;
            if (!SetFilePointerEx(file, at, nullptr, FILE_BEGIN) || !ReadFile(file, img.text.data(), size, &read, nullptr) || read != size)
                img.text.clear();
            break;
        }
        CloseHandle(file);
        return img.text.empty() ? nullptr : &img;
    }

    std::string Build(const Image& img, uint32_t rva, bool loose)
    {
        if (rva < img.rva || rva >= img.rva + img.text.size())
            return {};
        const size_t start = rva - img.rva;
        const uint8_t* code = img.text.data() + start;
        const size_t avail = img.text.size() - start;
        Pattern p;
        size_t at = 0;
        for (int insn = 0; insn < 40 && at < 96 && at + 16 <= avail && !stop; ++insn)
        {
            hde64s hs{};
            const unsigned len = hde64_disasm(code + at, &hs);
            if (!len || (hs.flags & F_ERROR))
                break;
            const size_t first = p.bytes.size();
            for (unsigned i = 0; i < len; ++i)
            {
                p.bytes.push_back(code[at + i]);
                p.mask.push_back(1);
            }
            unsigned imm = 0;
            if (hs.flags & F_IMM8)
                imm += 1;
            if (hs.flags & F_IMM16)
                imm += 2;
            if (hs.flags & F_IMM32)
                imm += 4;
            if (hs.flags & F_IMM64)
                imm += 8;
            if (hs.flags & F_RELATIVE)
                for (unsigned i = len - imm; i < len; ++i)
                    p.mask[first + i] = 0;
            if (hs.flags & F_DISP32)
            {
                const bool rip = hs.modrm_mod == 0 && hs.modrm_rm == 5;
                if (rip || loose)
                    for (unsigned i = len - imm - 4; i < len - imm; ++i)
                        p.mask[first + i] = 0;
            }
            at += len;
            if (at >= 6 && Count(img.text, p, 2) == 1)
                return Format(p);
            if (hs.opcode == 0xC3 || hs.opcode == 0xCC || (hs.opcode == 0xE9 && !(hs.flags & F_PREFIX_ANY)))
                break;
        }
        return {};
    }

    void Run(const Job& job)
    {
        if (job.kind == Job::Custom)
        {
            std::string out;
            if (Image* img = LoadImage(job.module))
                out = Build(*img, job.rva, patterns::looseOffsets);
            std::lock_guard<std::mutex> g(lock);
            custom = out.empty() ? "no unique pattern" : out;
            customPending = false;
            return;
        }

        std::string module, pattern;
        uint32_t rva = 0;
        bool found = false;
        int expect = 1;
        {
            std::lock_guard<std::mutex> g(lock);
            if (job.index < 0 || job.index >= static_cast<int>(records.size()))
                return;
            const patterns::Entry& e = records[job.index].entry;
            module = e.module;
            pattern = e.pattern;
            rva = e.rva;
            found = e.status != patterns::Missing;
            expect = e.expect;
            if (e.kind != "sig" && e.kind != "ref")
            {
                records[job.index].entry.pending = false;
                return;
            }
        }
        Image* img = LoadImage(module);
        int matches = -1;
        std::string generated;
        if (img)
        {
            if (job.kind == Job::Verify)
                matches = Count(img->text, Parse(pattern), expect + 1);
            else if (found)
                generated = Build(*img, rva, patterns::looseOffsets);
        }
        std::lock_guard<std::mutex> g(lock);
        patterns::Entry& e = records[job.index].entry;
        e.pending = false;
        if (job.kind == Job::Verify)
        {
            e.matches = matches;
            if (e.status != patterns::Missing && matches >= 0)
                e.status = matches == 0 ? patterns::Missing : matches <= e.expect ? patterns::Unique : patterns::Ambiguous;
        }
        else
            e.generated = generated.empty() ? "-" : generated;
    }

    void Loop()
    {
        for (;;)
        {
            Job job;
            {
                std::unique_lock<std::mutex> g(lock);
                wake.wait(g, [] { return stop || !jobs.empty(); });
                if (stop)
                    return;
                job = jobs.front();
                jobs.pop_front();
                ++running;
            }
            Run(job);
            --running;
        }
    }

    void Push(const Job& job)
    {
        std::lock_guard<std::mutex> g(lock);
        if (stop)
            return;
        if (!worker.joinable())
            worker = std::thread(Loop);
        jobs.push_back(job);
        if (job.kind != Job::Custom && job.index >= 0 && job.index < static_cast<int>(records.size()))
            records[job.index].entry.pending = true;
        wake.notify_one();
    }

    void Register(const mem::Module& m, const char* name, const char* pattern, uint8_t* hit, int matches, int expect = 1, const char* kind = "sig")
    {
        Record r;
        r.entry.name = name;
        r.entry.module = m.name ? m.name : "?";
        r.entry.pattern = pattern;
        r.entry.kind = kind;
        r.entry.expect = expect;
        r.entry.rva = hit && m.Contains(hit) ? static_cast<uint32_t>(hit - m.base) : 0;
        r.entry.status = !hit ? patterns::Missing : matches > expect ? patterns::Ambiguous : patterns::Found;
        r.entry.matches = matches;
        std::lock_guard<std::mutex> g(lock);
        for (Record& old : records)
            if (old.entry.name == r.entry.name)
            {
                old = r;
                return;
            }
        records.push_back(r);
        if (!hit)
            logs::Add(logs::Warning, "Pattern not found: %s", name);
    }
}

uint8_t* patterns::Find(const mem::Module& m, const char* name, const char* pattern)
{
    uint8_t* hit = mem::Find(m.text, pattern);
    Register(m, name, pattern, hit, hit ? -1 : 0);
    return hit;
}

std::vector<uint8_t*> patterns::FindAll(const mem::Module& m, const char* name, const char* pattern, size_t limit, int expect)
{
    std::vector<uint8_t*> hits = mem::FindAll(m.text, pattern, limit);
    Register(m, name, pattern, hits.empty() ? nullptr : hits[0], static_cast<int>(hits.size()), expect);
    return hits;
}

uint8_t* patterns::FindRef(const mem::Module& m, const char* name, const char* pattern, int dispOffset, int length)
{
    std::vector<uint8_t*> hits = mem::FindAll(m.text, pattern, 64);
    uint8_t* target = hits.empty() ? nullptr : mem::Rel(hits[0], dispOffset, length);
    bool same = true;
    for (uint8_t* h : hits)
        same = same && mem::Rel(h, dispOffset, length) == target;
    const int n = static_cast<int>(hits.size());
    Register(m, name, pattern, hits.empty() ? nullptr : hits[0], n, same ? n : 1, "ref");
    return same ? target : nullptr;
}

void patterns::Note(const mem::Module& m, const char* name, const char* kind, const void* address)
{
    auto p = static_cast<uint8_t*>(const_cast<void*>(address));
    Register(m, name, "-", p, p ? 1 : 0, 1, kind);
    std::lock_guard<std::mutex> g(lock);
    for (Record& r : records)
        if (r.entry.name == name && p)
        {
            r.entry.status = patterns::Unique;
            if (!m.Contains(p))
                r.entry.rva = 0;
        }
}

std::vector<patterns::Entry> patterns::Snapshot()
{
    std::lock_guard<std::mutex> g(lock);
    std::vector<Entry> out;
    out.reserve(records.size());
    for (const Record& r : records)
        out.push_back(r.entry);
    return out;
}

void patterns::Verify()
{
    const int n = static_cast<int>(Snapshot().size());
    for (int i = 0; i < n; ++i)
        Push({ Job::Verify, i, {}, 0 });
}

void patterns::Generate(int index)
{
    Push({ Job::Generate, index, {}, 0 });
}

void patterns::GenerateAll()
{
    const int n = static_cast<int>(Snapshot().size());
    for (int i = 0; i < n; ++i)
        Push({ Job::Generate, i, {}, 0 });
}

void patterns::GenerateAt(const char* module, uint32_t rva)
{
    {
        std::lock_guard<std::mutex> g(lock);
        customPending = true;
        custom.clear();
    }
    Push({ Job::Custom, -1, module, rva });
}

std::string patterns::Custom(bool* pending)
{
    std::lock_guard<std::mutex> g(lock);
    if (pending)
        *pending = customPending;
    return custom;
}

bool patterns::Busy()
{
    std::lock_guard<std::mutex> g(lock);
    return !jobs.empty() || running > 0;
}

bool patterns::Export()
{
    std::string path = settings::Path();
    const size_t slash = path.find_last_of("\\/");
    path = (slash == std::string::npos ? std::string() : path.substr(0, slash + 1)) + "patterns.txt";
    FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "w") || !f)
        return false;
    static const char* kStatus[] = { "missing", "found", "unique", "ambiguous" };
    for (const Entry& e : Snapshot())
    {
        fprintf(f, "[%s]\nkind = %s\nmodule = %s\nrva = 0x%X\nstatus = %s\nmatches = %d/%d\npattern = %s\n", e.name.c_str(), e.kind.c_str(), e.module.c_str(), e.rva, kStatus[e.status], e.matches, e.expect, e.pattern.c_str());
        if (!e.generated.empty())
            fprintf(f, "generated = %s\n", e.generated.c_str());
        fputc('\n', f);
    }
    fclose(f);
    logs::Add(logs::Success, "Patterns exported to %s", path.c_str());
    return true;
}

void patterns::Shutdown()
{
    {
        std::lock_guard<std::mutex> g(lock);
        stop = true;
        jobs.clear();
        wake.notify_all();
    }
    if (worker.joinable())
        worker.join();
}
