#include "convar.h"
#include "log.h"
#include "memory.h"
#include <Windows.h>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace
{
    constexpr std::uintptr_t convar_data_offset = 0x8;
    constexpr std::uintptr_t convar_value_offset = 0x58;
    constexpr std::size_t object_search_window = 0x90;
    constexpr std::size_t lea_length = 7;
    constexpr int max_string_hits = 8;
    constexpr const char* modules[] = { "client.dll", "engine2.dll" };

    std::mutex g_lock;
    std::unordered_map<std::string, std::unique_ptr<convars::convar>> g_found;
    std::unordered_set<std::string> g_missing;

    std::uintptr_t lea_target(const std::uint8_t* insn)
    {
        std::int32_t disp = 0;
        std::memcpy(&disp, insn + 3, sizeof(disp));
        return reinterpret_cast<std::uintptr_t>(insn) + lea_length + static_cast<std::uintptr_t>(static_cast<std::intptr_t>(disp));
    }

    int find_strings(const memory::section& rdata, const char* name, std::size_t len, std::uintptr_t* out)
    {
        int count = 0;
        if (!rdata.start || rdata.size < len + 1)
            return 0;
        const auto* begin = reinterpret_cast<const std::uint8_t*>(rdata.start);
        const std::uint8_t* end = begin + rdata.size;
        const std::uint8_t* p = begin + 1;
        while (p + len <= end && count < max_string_hits)
        {
            p = static_cast<const std::uint8_t*>(std::memchr(p, static_cast<std::uint8_t>(name[0]), static_cast<std::size_t>(end - len - p) + 1));
            if (!p)
                break;
            if (!p[-1] && !std::memcmp(p, name, len))
                out[count++] = reinterpret_cast<std::uintptr_t>(p);
            ++p;
        }
        return count;
    }

    std::uintptr_t find_object(const memory::module_info& m, const std::uintptr_t* strings, int string_count)
    {
        if (!m.text.start || m.text.size < lea_length)
            return 0;
        const auto* begin = reinterpret_cast<const std::uint8_t*>(m.text.start);
        const std::uint8_t* end = begin + m.text.size;
        const std::uint8_t* p = begin + 2;
        while (p + 5 <= end)
        {
            p = static_cast<const std::uint8_t*>(std::memchr(p, 0x15, static_cast<std::size_t>(end - 5 - p) + 1));
            if (!p)
                break;
            const std::uint8_t* insn = p - 2;
            ++p;
            if (insn[0] != 0x48 || insn[1] != 0x8D)
                continue;
            const std::uintptr_t target = lea_target(insn);
            bool match = false;
            for (int i = 0; i < string_count && !match; ++i)
                match = target == strings[i];
            if (!match)
                continue;
            const std::uint8_t* window_end = insn + lea_length + object_search_window;
            for (const std::uint8_t* q = insn + lea_length; q < window_end && q + lea_length <= end; ++q)
            {
                if (q[0] != 0x48 || q[1] != 0x8D || q[2] != 0x0D)
                    continue;
                const std::uintptr_t object = lea_target(q);
                return m.contains(object) ? object : 0;
            }
        }
        return 0;
    }

    std::uintptr_t search_module(const memory::module_info& m, const char* name)
    {
        std::uintptr_t strings[max_string_hits]{};
        const int count = find_strings(m.rdata, name, std::strlen(name) + 1, strings);
        return count ? find_object(m, strings, count) : 0;
    }

    std::uintptr_t search_module_safe(const memory::module_info& m, const char* name)
    {
        __try
        {
            return search_module(m, name);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    std::uintptr_t value_of(std::uintptr_t object)
    {
        if (!object || !memory::is_readable(object + convar_data_offset))
            return 0;
        const auto data = memory::read<std::uintptr_t>(object + convar_data_offset);
        if (!data || !memory::is_readable(data + convar_value_offset, sizeof(std::uint64_t)))
            return 0;
        return data + convar_value_offset;
    }
}

namespace convars
{
    convar* find(const char* name)
    {
        if (!name || !*name)
            return nullptr;
        std::lock_guard lock(g_lock);
        if (const auto it = g_found.find(name); it != g_found.end())
        {
            convar& c = *it->second;
            if (!c.value)
                c.value = value_of(c.object);
            return c.value ? &c : nullptr;
        }
        if (g_missing.contains(name))
            return nullptr;
        std::uintptr_t object = 0;
        bool all_loaded = true;
        for (const char* module : modules)
        {
            const memory::module_info& m = memory::get_module(module);
            if (!m)
            {
                all_loaded = false;
                continue;
            }
            object = search_module_safe(m, name);
            if (object)
                break;
        }
        if (!object)
        {
            if (all_loaded)
            {
                g_missing.emplace(name);
                logs::Add(logs::Warning, "ConVar not found: %s", name);
            }
            return nullptr;
        }
        const auto [it, inserted] = g_found.emplace(name, std::make_unique<convar>());
        convar& c = *it->second;
        c.name = it->first.c_str();
        c.object = object;
        c.value = value_of(object);
        return c.value ? &c : nullptr;
    }
}
