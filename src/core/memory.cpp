#include "memory.h"
#include <Windows.h>
#include <array>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>

namespace
{
    constexpr std::uintptr_t min_address = 0x10000;
    constexpr std::uintptr_t max_address = 0x7FFFFFFEFFFFull;
    constexpr std::uint64_t readable_cache_ms = 1000;
    constexpr std::size_t readable_cache_size = 32;
    constexpr std::uint32_t col_signature = 1;
    constexpr std::uintptr_t type_descriptor_name_offset = 0x10;

    struct readable_region
    {
        std::uintptr_t start = 0;
        std::uintptr_t end = 0;
        std::uint64_t expires = 0;
    };

    std::shared_mutex g_readable_lock;
    std::array<readable_region, readable_cache_size> g_readable{};
    std::size_t g_readable_next = 0;

    std::mutex g_module_lock;
    std::unordered_map<std::string, std::unique_ptr<memory::module_info>> g_modules;

    std::mutex g_vtable_lock;
    std::unordered_map<std::string, std::uintptr_t> g_vtables;

    bool readable_protection(DWORD protect)
    {
        if (protect & (PAGE_GUARD | PAGE_NOACCESS))
            return false;
        constexpr DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
        return (protect & readable) != 0;
    }

    bool cached_readable(std::uintptr_t start, std::uintptr_t end, std::uint64_t now)
    {
        std::shared_lock lock(g_readable_lock);
        for (const readable_region& r : g_readable)
            if (r.expires > now && start >= r.start && end <= r.end)
                return true;
        return false;
    }

    void cache_readable(std::uintptr_t start, std::uintptr_t end, std::uint64_t now)
    {
        std::unique_lock lock(g_readable_lock);
        g_readable[g_readable_next] = { start, end, now + readable_cache_ms };
        g_readable_next = (g_readable_next + 1) % readable_cache_size;
    }

    int hex_value(char c)
    {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        return -1;
    }

    bool common_byte(std::uint8_t b)
    {
        switch (b)
        {
        case 0x00:
        case 0x24:
        case 0x44:
        case 0x48:
        case 0x49:
        case 0x4C:
        case 0x83:
        case 0x89:
        case 0x8B:
        case 0x8D:
        case 0x0F:
        case 0xCC:
        case 0xFF:
            return true;
        default:
            return false;
        }
    }

    std::size_t pick_anchor(const std::uint8_t* bytes, const std::uint8_t* mask, std::size_t n)
    {
        std::size_t first = n;
        for (std::size_t i = 0; i < n; ++i)
        {
            if (!mask[i])
                continue;
            if (first == n)
                first = i;
            if (!common_byte(bytes[i]))
                return i;
        }
        return first;
    }

    int scan_raw(const std::uint8_t* begin, std::size_t size, const std::uint8_t* bytes, const std::uint8_t* mask, std::size_t n, std::size_t anchor, int limit, std::uintptr_t* first)
    {
        int found = 0;
        __try
        {
            const std::uint8_t* q = begin + anchor;
            const std::uint8_t* q_last = begin + (size - n) + anchor;
            const std::uint8_t key = bytes[anchor];
            while (q <= q_last)
            {
                q = static_cast<const std::uint8_t*>(std::memchr(q, key, static_cast<std::size_t>(q_last - q) + 1));
                if (!q)
                    break;
                const std::uint8_t* s = q - anchor;
                std::size_t i = 0;
                while (i < n && (!mask[i] || s[i] == bytes[i]))
                    ++i;
                if (i == n)
                {
                    if (!found)
                        *first = reinterpret_cast<std::uintptr_t>(s);
                    if (++found >= limit)
                        break;
                }
                ++q;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
        return found;
    }

    std::size_t copy_string(std::uintptr_t address, char* out, std::size_t max)
    {
        std::size_t i = 0;
        __try
        {
            const char* src = reinterpret_cast<const char*>(address);
            while (i < max)
            {
                if ((i == 0 || ((address + i) & 0xFFF) == 0) && !memory::is_readable(address + i, 1))
                    break;
                const char c = src[i];
                if (!c)
                    break;
                out[i++] = c;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
        return i;
    }

    std::uintptr_t find_bytes(std::uintptr_t from, std::uintptr_t to, const void* bytes, std::size_t len)
    {
        const auto* b = static_cast<const std::uint8_t*>(bytes);
        while (from + len <= to)
        {
            const auto* hit = static_cast<const std::uint8_t*>(std::memchr(reinterpret_cast<const void*>(from), b[0], to - from - len + 1));
            if (!hit)
                return 0;
            from = reinterpret_cast<std::uintptr_t>(hit);
            if (!std::memcmp(hit, b, len))
                return from;
            ++from;
        }
        return 0;
    }

    std::uintptr_t find_vtable_raw(const memory::module_info& m, const char* rtti_name)
    {
        const std::size_t len = std::strlen(rtti_name) + 1;
        const std::uintptr_t data_end = m.data.start + m.data.size;
        const std::uintptr_t rdata_end = m.rdata.start + m.rdata.size;
        for (std::uintptr_t name = find_bytes(m.data.start, data_end, rtti_name, len); name; name = find_bytes(name + 1, data_end, rtti_name, len))
        {
            const std::uintptr_t td = name - type_descriptor_name_offset;
            if (td & 7 || td < m.base)
                continue;
            const auto td_rva = static_cast<std::uint32_t>(td - m.base);
            for (std::uintptr_t p = m.rdata.start; p + 24 <= rdata_end; p += 4)
            {
                const auto* col = reinterpret_cast<const std::uint32_t*>(p);
                if (col[0] != col_signature || col[1] != 0 || col[3] != td_rva || col[5] != static_cast<std::uint32_t>(p - m.base))
                    continue;
                for (std::uintptr_t q = m.rdata.start; q + 16 <= rdata_end; q += 8)
                    if (*reinterpret_cast<const std::uint64_t*>(q) == static_cast<std::uint64_t>(p))
                        return q + 8;
            }
        }
        return 0;
    }

    std::uintptr_t find_vtable_safe(const memory::module_info& m, const char* rtti_name)
    {
        __try
        {
            return find_vtable_raw(m, rtti_name);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    std::unique_ptr<memory::module_info> load_module(const std::string& name)
    {
        const HMODULE handle = GetModuleHandleA(name.c_str());
        if (!handle)
            return nullptr;
        const auto base = reinterpret_cast<std::uintptr_t>(handle);
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE)
            return nullptr;
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE)
            return nullptr;
        auto m = std::make_unique<memory::module_info>();
        m->base = base;
        m->size = nt->OptionalHeader.SizeOfImage;
        memory::section executable{};
        const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
        for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec)
        {
            char section_name[9]{};
            std::memcpy(section_name, sec->Name, 8);
            const memory::section s{ base + sec->VirtualAddress, sec->Misc.VirtualSize };
            if (!std::strcmp(section_name, ".text"))
                m->text = s;
            else if (!std::strcmp(section_name, ".rdata"))
                m->rdata = s;
            else if (!std::strcmp(section_name, ".data"))
                m->data = s;
            if (!executable.start && (sec->Characteristics & IMAGE_SCN_MEM_EXECUTE))
                executable = s;
        }
        if (!m->text.start)
            m->text = executable;
        return m;
    }
}

namespace memory
{
    const module_info& get_module(const char* name)
    {
        static const module_info empty{};
        if (!name || !*name)
            return empty;
        std::lock_guard lock(g_module_lock);
        const auto it = g_modules.find(name);
        if (it != g_modules.end())
            return *it->second;
        std::string key(name);
        auto loaded = load_module(key);
        if (!loaded)
            return empty;
        const auto [inserted, ok] = g_modules.emplace(std::move(key), std::move(loaded));
        inserted->second->name = inserted->first.c_str();
        return *inserted->second;
    }

    std::uintptr_t get_module_base(const char* name)
    {
        return get_module(name).base;
    }

    std::uintptr_t get_module_export(const char* spec)
    {
        if (!spec)
            return 0;
        const char* colon = std::strchr(spec, ':');
        if (!colon || colon == spec || !colon[1])
            return 0;
        const std::string module(spec, colon);
        const HMODULE handle = GetModuleHandleA(module.c_str());
        if (!handle)
            return 0;
        return reinterpret_cast<std::uintptr_t>(GetProcAddress(handle, colon + 1));
    }

    std::uintptr_t get_module_interface(const char* spec)
    {
        if (!spec)
            return 0;
        const char* colon = std::strchr(spec, ':');
        if (!colon || colon == spec || !colon[1])
            return 0;
        const std::string module(spec, colon);
        const HMODULE handle = GetModuleHandleA(module.c_str());
        if (!handle)
            return 0;
        using create_interface_fn = void* (*)(const char*, int*);
        const auto create = reinterpret_cast<create_interface_fn>(GetProcAddress(handle, "CreateInterface"));
        if (!create)
            return 0;
        return reinterpret_cast<std::uintptr_t>(create(colon + 1, nullptr));
    }

    pattern parse_pattern(const char* spec)
    {
        pattern p;
        if (!spec)
            return p;
        const char* colon = std::strchr(spec, ':');
        if (!colon || colon == spec)
            return p;
        p.module.assign(spec, colon);
        const char* c = colon + 1;
        while (*c && *c != '+' && *c != '-' && *c != '~')
        {
            if (*c == '*' || *c == '>')
            {
                if (p.marker >= 0)
                    return p;
                p.marker = static_cast<int>(p.bytes.size());
                p.marker_kind = *c;
                ++c;
                continue;
            }
            if (c[0] == '?' && c[1] == '?')
            {
                p.bytes.push_back(0);
                p.mask.push_back(0);
                c += 2;
                continue;
            }
            const int hi = hex_value(c[0]);
            const int lo = hi >= 0 ? hex_value(c[1]) : -1;
            if (hi < 0 || lo < 0)
                return p;
            p.bytes.push_back(static_cast<std::uint8_t>(hi << 4 | lo));
            p.mask.push_back(1);
            c += 2;
        }
        if (*c == '+' || *c == '-')
        {
            const bool negative = *c == '-';
            ++c;
            std::intptr_t value = 0;
            int digits = 0;
            for (int h = hex_value(*c); h >= 0; h = hex_value(*++c))
            {
                if (++digits > 8)
                    return p;
                value = value * 16 + h;
            }
            if (!digits)
                return p;
            p.offset = negative ? -value : value;
        }
        if (*c == '~')
        {
            p.deref = true;
            ++c;
        }
        if (*c || p.bytes.empty())
            return p;
        bool fixed = false;
        for (const std::uint8_t m : p.mask)
            fixed = fixed || m;
        if (!fixed)
            return p;
        if (p.marker >= 0)
        {
            const std::size_t need = p.marker_kind == '*' ? 4 : 5;
            if (static_cast<std::size_t>(p.marker) + need > p.bytes.size())
                return p;
        }
        p.valid = true;
        return p;
    }

    std::uintptr_t scan(const section& where, const pattern& p, int* matches, int limit)
    {
        if (matches)
            *matches = 0;
        const std::size_t n = p.bytes.size();
        if (!p.valid || !where.start || !n || p.mask.size() != n || where.size < n || limit < 1)
            return 0;
        const std::size_t anchor = pick_anchor(p.bytes.data(), p.mask.data(), n);
        if (anchor == n)
            return 0;
        std::uintptr_t first = 0;
        const int found = scan_raw(reinterpret_cast<const std::uint8_t*>(where.start), where.size, p.bytes.data(), p.mask.data(), n, anchor, limit, &first);
        if (matches)
            *matches = found;
        return found ? first : 0;
    }

    std::uintptr_t apply(std::uintptr_t match, const pattern& p)
    {
        if (!match || !p.valid)
            return 0;
        std::uintptr_t address = match;
        if (p.marker >= 0)
        {
            const std::uintptr_t at = match + static_cast<std::uintptr_t>(p.marker);
            if (p.marker_kind == '*')
            {
                if (!is_readable(at, 4))
                    return 0;
                address = at + 4 + static_cast<std::uintptr_t>(static_cast<std::intptr_t>(read<std::int32_t>(at)));
            }
            else
            {
                if (!is_readable(at, 5))
                    return 0;
                address = at + 5 + static_cast<std::uintptr_t>(static_cast<std::intptr_t>(read<std::int32_t>(at + 1)));
            }
        }
        address += static_cast<std::uintptr_t>(p.offset);
        if (p.deref)
        {
            if (!is_readable(address, sizeof(std::uintptr_t)))
                return 0;
            address = read<std::uintptr_t>(address);
        }
        return address;
    }

    std::uintptr_t resolve_pattern(const char* spec, int* matches)
    {
        if (matches)
            *matches = -1;
        const pattern p = parse_pattern(spec);
        if (!p.valid)
            return 0;
        const module_info& m = get_module(p.module.c_str());
        if (!m)
            return 0;
        int count = 0;
        const std::uintptr_t match = scan(m.text, p, &count, 2);
        if (matches)
            *matches = count;
        return match ? apply(match, p) : 0;
    }

    std::uintptr_t rel32(std::uintptr_t instruction, int disp_offset, int length)
    {
        if (!instruction || !is_readable(instruction + static_cast<std::uintptr_t>(disp_offset), 4))
            return 0;
        const auto disp = read<std::int32_t>(instruction + static_cast<std::uintptr_t>(disp_offset));
        return instruction + static_cast<std::uintptr_t>(length) + static_cast<std::uintptr_t>(static_cast<std::intptr_t>(disp));
    }

    std::uintptr_t find_vtable(const char* module, const char* rtti_name)
    {
        if (!module || !rtti_name || !*rtti_name)
            return 0;
        const module_info& m = get_module(module);
        if (!m || !m.data.start || !m.rdata.start)
            return 0;
        std::string key = std::string(module) + ':' + rtti_name;
        {
            std::lock_guard lock(g_vtable_lock);
            const auto it = g_vtables.find(key);
            if (it != g_vtables.end())
                return it->second;
        }
        const std::uintptr_t vtable = find_vtable_safe(m, rtti_name);
        if (vtable)
        {
            std::lock_guard lock(g_vtable_lock);
            g_vtables.emplace(std::move(key), vtable);
        }
        return vtable;
    }

    bool is_readable(std::uintptr_t address, std::size_t size)
    {
        if (address < min_address || address > max_address)
            return false;
        if (!size)
            size = 1;
        const std::uintptr_t end = address + size;
        if (end < address || end > max_address)
            return false;
        const std::uint64_t now = GetTickCount64();
        if (cached_readable(address, end, now))
            return true;
        std::uintptr_t at = address;
        while (at < end)
        {
            MEMORY_BASIC_INFORMATION mbi{};
            if (!VirtualQuery(reinterpret_cast<const void*>(at), &mbi, sizeof(mbi)))
                return false;
            if (mbi.State != MEM_COMMIT || !readable_protection(mbi.Protect))
                return false;
            const auto region_start = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
            const std::uintptr_t region_end = region_start + mbi.RegionSize;
            if (region_end <= at)
                return false;
            cache_readable(region_start, region_end, now);
            at = region_end;
        }
        return true;
    }

    std::string read_string(std::uintptr_t address, std::size_t max)
    {
        if (!address || !max)
            return {};
        std::string out(max, '\0');
        out.resize(copy_string(address, out.data(), max));
        return out;
    }
}
