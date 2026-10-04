#include "schema.h"
#include "addresses.h"
#include "log.h"
#include "memory.h"
#include <Windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
    constexpr std::size_t find_type_scope_index = 13;
    constexpr std::size_t find_declared_class_index = 2;

    constexpr std::uintptr_t class_name_offset = 0x08;
    constexpr std::uintptr_t class_size_offset = 0x18;
    constexpr std::uintptr_t class_field_count_offset = 0x1C;
    constexpr std::uintptr_t class_base_count_offset = 0x23;
    constexpr std::uintptr_t class_fields_offset = 0x28;
    constexpr std::uintptr_t class_bases_offset = 0x38;
    constexpr std::size_t class_info_size = 0x40;

    constexpr std::uintptr_t field_stride = 0x20;
    constexpr std::uintptr_t field_name_offset = 0x00;
    constexpr std::uintptr_t field_offset_offset = 0x10;

    constexpr std::uintptr_t base_stride = 0x10;
    constexpr std::uintptr_t base_offset_offset = 0x00;
    constexpr std::uintptr_t base_class_offset = 0x08;

    constexpr int max_fields = 4096;
    constexpr int max_bases = 16;
    constexpr int max_depth = 16;
    constexpr int max_name = 256;
    constexpr std::int32_t max_field_offset = 0x1000000;

    constexpr const char* scope_modules[] = { "client.dll", "animationsystem.dll", "engine2.dll", "scenesystem.dll", "materialsystem2.dll" };
    constexpr std::size_t scope_count = sizeof(scope_modules) / sizeof(scope_modules[0]);

    std::atomic<std::uintptr_t> g_scopes[scope_count]{};
    std::mutex g_lock;
    std::unordered_map<std::uint64_t, std::uint32_t> g_offsets;
    std::unordered_map<std::string, std::uintptr_t> g_classes;
    std::vector<schema::missing_field> g_missing;

    std::uintptr_t call_find_scope(std::uintptr_t system, const char* module)
    {
        __try
        {
            return memory::call_vfunc<std::uintptr_t>(system, find_type_scope_index, module, static_cast<void*>(nullptr));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    std::uintptr_t call_find_class(std::uintptr_t scope, const char* name)
    {
        std::uintptr_t info = 0;
        __try
        {
            memory::call_vfunc<void>(scope, find_declared_class_index, &info, name);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
        return info;
    }

    std::uintptr_t scope_at(std::size_t index)
    {
        std::uintptr_t scope = g_scopes[index].load(std::memory_order_acquire);
        if (scope)
            return scope;
        const std::uintptr_t system = addresses::globals::schema_system();
        if (!system || !memory::get_module_base(scope_modules[index]))
            return 0;
        scope = call_find_scope(system, scope_modules[index]);
        if (scope && memory::is_readable(scope))
            g_scopes[index].store(scope, std::memory_order_release);
        else
            scope = 0;
        return scope;
    }

    bool same_name(std::uintptr_t name, const char* expected)
    {
        if (!name || !expected)
            return false;
        int i = 0;
        for (; i < max_name; ++i)
        {
            if ((i == 0 || ((name + i) & 0xFFF) == 0) && !memory::is_readable(name + i, 1))
                return false;
            const char c = memory::read<char>(name + i);
            if (c != expected[i])
                return false;
            if (!c)
                return true;
        }
        return false;
    }

    std::uint32_t hash_name(std::uintptr_t name, bool& ok)
    {
        ok = false;
        std::uint32_t value = 0x811C9DC5u;
        for (int i = 0; i < max_name; ++i)
        {
            if ((i == 0 || ((name + i) & 0xFFF) == 0) && !memory::is_readable(name + i, 1))
                return 0;
            const auto c = memory::read<std::uint8_t>(name + i);
            if (!c)
            {
                ok = i > 0;
                return value;
            }
            value = (value ^ c) * 0x01000193u;
        }
        return 0;
    }

    bool valid_class(std::uintptr_t info)
    {
        return info && memory::is_readable(info, class_info_size);
    }

    bool search_class(std::uintptr_t info, std::uint32_t field_hash, int depth, std::uint32_t& out)
    {
        if (depth > max_depth || !valid_class(info))
            return false;
        const int count = memory::read<std::int16_t>(info + class_field_count_offset);
        const auto fields = memory::read<std::uintptr_t>(info + class_fields_offset);
        if (count > 0 && count <= max_fields && fields && memory::is_readable(fields, static_cast<std::size_t>(count) * field_stride))
        {
            for (int i = 0; i < count; ++i)
            {
                const std::uintptr_t field = fields + static_cast<std::uintptr_t>(i) * field_stride;
                const auto name = memory::read<std::uintptr_t>(field + field_name_offset);
                bool ok = false;
                if (!name || hash_name(name, ok) != field_hash || !ok)
                    continue;
                const auto offset = memory::read<std::int32_t>(field + field_offset_offset);
                if (offset < 0 || offset > max_field_offset)
                    continue;
                out = static_cast<std::uint32_t>(offset);
                return true;
            }
        }
        const int base_count = memory::read<std::uint8_t>(info + class_base_count_offset);
        const auto bases = memory::read<std::uintptr_t>(info + class_bases_offset);
        if (base_count <= 0 || base_count > max_bases || !bases || !memory::is_readable(bases, static_cast<std::size_t>(base_count) * base_stride))
            return false;
        for (int i = 0; i < base_count; ++i)
        {
            const std::uintptr_t base = bases + static_cast<std::uintptr_t>(i) * base_stride;
            const auto base_offset = memory::read<std::uint32_t>(base + base_offset_offset);
            const auto base_info = memory::read<std::uintptr_t>(base + base_class_offset);
            if (base_info == info || base_offset > static_cast<std::uint32_t>(max_field_offset))
                continue;
            std::uint32_t inner = 0;
            if (search_class(base_info, field_hash, depth + 1, inner))
            {
                out = base_offset + inner;
                return true;
            }
        }
        return false;
    }

    bool search_class_safe(std::uintptr_t info, std::uint32_t field_hash, std::uint32_t& out)
    {
        __try
        {
            return search_class(info, field_hash, 0, out);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool class_matches(std::uintptr_t info, const char* name)
    {
        __try
        {
            return valid_class(info) && same_name(memory::read<std::uintptr_t>(info + class_name_offset), name);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    int read_class_size(std::uintptr_t info)
    {
        __try
        {
            if (!valid_class(info))
                return -1;
            const int size = memory::read<std::int32_t>(info + class_size_offset);
            return size > 0 && size <= max_field_offset ? size : -1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return -1;
        }
    }

    std::uintptr_t find_class_locked(const char* name)
    {
        if (const auto it = g_classes.find(name); it != g_classes.end())
            return it->second;
        for (std::size_t i = 0; i < scope_count; ++i)
        {
            const std::uintptr_t scope = scope_at(i);
            if (!scope)
                continue;
            const std::uintptr_t info = call_find_class(scope, name);
            if (info && class_matches(info, name))
            {
                g_classes.emplace(name, info);
                return info;
            }
        }
        return 0;
    }

    std::string field_display(const char* label, std::uint32_t field_hash)
    {
        if (label && *label == '"')
        {
            const char* end = std::strchr(label + 1, '"');
            if (end)
                return std::string(label + 1, end);
        }
        if (label && *label)
            return label;
        char buffer[16]{};
        std::snprintf(buffer, sizeof(buffer), "0x%08X", field_hash);
        return buffer;
    }
}

namespace schema
{
    bool initialize()
    {
        return scope_at(0) != 0;
    }

    bool ready()
    {
        return g_scopes[0].load(std::memory_order_acquire) != 0;
    }

    std::uint32_t find(const char* class_name, std::uint32_t field_hash, const char* field_label)
    {
        if (!class_name || !*class_name)
            return invalid;
        const std::uint64_t key = static_cast<std::uint64_t>(hash::runtime(class_name)) << 32 | field_hash;
        std::lock_guard lock(g_lock);
        if (const auto it = g_offsets.find(key); it != g_offsets.end())
            return it->second;
        if (!scope_at(0))
            return invalid;
        std::uint32_t offset = invalid;
        const std::uintptr_t info = find_class_locked(class_name);
        if (!info || !search_class_safe(info, field_hash, offset))
            offset = invalid;
        g_offsets.emplace(key, offset);
        if (offset == invalid)
        {
            missing_field entry{ class_name, field_display(field_label, field_hash) };
            logs::Add(logs::Warning, "Schema: %s::%s not found%s", entry.class_name.c_str(), entry.field.c_str(), info ? "" : " (class missing)");
            g_missing.push_back(std::move(entry));
        }
        return offset;
    }

    std::uint32_t find(const char* class_name, const char* field_name)
    {
        if (!field_name || !*field_name)
            return invalid;
        return find(class_name, hash::runtime(field_name), field_name);
    }

    int class_size(const char* class_name)
    {
        if (!class_name || !*class_name)
            return -1;
        std::lock_guard lock(g_lock);
        if (!scope_at(0))
            return -1;
        const std::uintptr_t info = find_class_locked(class_name);
        return info ? read_class_size(info) : -1;
    }

    std::vector<missing_field> missing()
    {
        std::lock_guard lock(g_lock);
        return g_missing;
    }
}
