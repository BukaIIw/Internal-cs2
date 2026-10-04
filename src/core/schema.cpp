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
    constexpr std::size_t class_info_size = 0x40;
    constexpr std::size_t class_scan_size = 0x80;
    constexpr std::int32_t anchor_field_offset = 0x10;

    struct schema_layout
    {
        std::uintptr_t field_count = 0x1C;
        std::uintptr_t fields = 0x28;
        std::uintptr_t field_stride = 0x20;
        std::uintptr_t field_name = 0x00;
        std::uintptr_t field_offset = 0x10;
        std::uintptr_t base_count = 0x23;
        std::uintptr_t bases = 0x38;
        std::uintptr_t base_stride = 0x10;
        std::uintptr_t base_offset = 0x00;
        std::uintptr_t base_class = 0x08;
    };

    schema_layout g_layout{};
    bool g_layout_detected = false;

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
        const schema_layout& l = g_layout;
        const int count = memory::read<std::int16_t>(info + l.field_count);
        const auto fields = memory::read<std::uintptr_t>(info + l.fields);
        if (count > 0 && count <= max_fields && fields && memory::is_readable(fields, static_cast<std::size_t>(count) * l.field_stride))
        {
            for (int i = 0; i < count; ++i)
            {
                const std::uintptr_t field = fields + static_cast<std::uintptr_t>(i) * l.field_stride;
                const auto name = memory::read<std::uintptr_t>(field + l.field_name);
                bool ok = false;
                if (!name || hash_name(name, ok) != field_hash || !ok)
                    continue;
                const auto offset = memory::read<std::int32_t>(field + l.field_offset);
                if (offset < 0 || offset > max_field_offset)
                    continue;
                out = static_cast<std::uint32_t>(offset);
                return true;
            }
        }
        const int base_count = memory::read<std::uint8_t>(info + l.base_count);
        const auto bases = memory::read<std::uintptr_t>(info + l.bases);
        if (base_count <= 0 || base_count > max_bases || !bases || !memory::is_readable(bases, static_cast<std::size_t>(base_count) * l.base_stride))
            return false;
        for (int i = 0; i < base_count; ++i)
        {
            const std::uintptr_t base = bases + static_cast<std::uintptr_t>(i) * l.base_stride;
            const auto base_offset = memory::read<std::uint32_t>(base + l.base_offset);
            const auto base_info = memory::read<std::uintptr_t>(base + l.base_class);
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

    bool is_identifier(std::uintptr_t name)
    {
        if (!name || !memory::is_readable(name, 1))
            return false;
        for (int i = 0; i < max_name; ++i)
        {
            if (((name + i) & 0xFFF) == 0 && !memory::is_readable(name + i, 1))
                return false;
            const char c = memory::read<char>(name + i);
            if (!c)
                return i > 0;
            const bool alpha = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
            if (!alpha && !(i > 0 && c >= '0' && c <= '9'))
                return false;
        }
        return false;
    }

    bool entries_valid(std::uintptr_t array, std::uintptr_t stride, std::uintptr_t name_offset, int count)
    {
        if (!array || count <= 0 || !memory::is_readable(array, static_cast<std::size_t>(count) * stride))
            return false;
        for (int i = 0; i < count; ++i)
        {
            if (!is_identifier(memory::read<std::uintptr_t>(array + static_cast<std::uintptr_t>(i) * stride + name_offset)))
                return false;
        }
        return true;
    }

    int field_count_at(std::uintptr_t info, std::uintptr_t at)
    {
        return memory::read<std::int16_t>(info + at);
    }

    bool fields_match(std::uintptr_t info, const schema_layout& l)
    {
        const int count = field_count_at(info, l.field_count);
        return count > 0 && count <= max_fields && entries_valid(memory::read<std::uintptr_t>(info + l.fields), l.field_stride, l.field_name, count);
    }

    bool detect_fields(std::uintptr_t info, schema_layout& out)
    {
        if (fields_match(info, out))
            return true;
        constexpr std::uintptr_t strides[] = { 0x20, 0x18, 0x28, 0x30, 0x38, 0x40 };
        constexpr std::uintptr_t name_offsets[] = { 0x00, 0x08 };
        for (std::uintptr_t fields_at = 0x10; fields_at + 8 <= class_scan_size; fields_at += 8)
        {
            const auto array = memory::read<std::uintptr_t>(info + fields_at);
            if (!array || !memory::is_readable(array, 0x18 * 3))
                continue;
            for (const auto stride : strides)
            {
                for (const auto name_at : name_offsets)
                {
                    if (!entries_valid(array, stride, name_at, 3))
                        continue;
                    int best = 0;
                    std::uintptr_t best_at = 0;
                    for (std::uintptr_t count_at = 0x10; count_at + 2 <= 0x40; count_at += 2)
                    {
                        if (count_at + 2 > fields_at && count_at < fields_at + 8)
                            continue;
                        const int n = field_count_at(info, count_at);
                        if (n < 3 || n > max_fields || n <= best)
                            continue;
                        if (entries_valid(array, stride, name_at, n))
                        {
                            best = n;
                            best_at = count_at;
                        }
                    }
                    if (!best)
                        continue;
                    out.fields = fields_at;
                    out.field_stride = stride;
                    out.field_name = name_at;
                    out.field_count = best_at;
                    return true;
                }
            }
        }
        return false;
    }

    bool detect_field_offset(std::uintptr_t info, schema_layout& out)
    {
        const int count = field_count_at(info, out.field_count);
        const auto array = memory::read<std::uintptr_t>(info + out.fields);
        if (count <= 0 || count > max_fields || !entries_valid(array, out.field_stride, out.field_name, count))
            return false;
        for (int i = 0; i < count; ++i)
        {
            const std::uintptr_t entry = array + static_cast<std::uintptr_t>(i) * out.field_stride;
            bool ok = false;
            if (hash_name(memory::read<std::uintptr_t>(entry + out.field_name), ok) != hash::fnv1a("m_pEntity") || !ok)
                continue;
            if (out.field_offset + 4 <= out.field_stride && memory::read<std::int32_t>(entry + out.field_offset) == anchor_field_offset)
                return true;
            for (std::uintptr_t at = 0; at + 4 <= out.field_stride; at += 4)
            {
                if (at + 4 > out.field_name && at < out.field_name + 8)
                    continue;
                if (memory::read<std::int32_t>(entry + at) == anchor_field_offset)
                {
                    out.field_offset = at;
                    return true;
                }
            }
            return false;
        }
        return false;
    }

    bool base_entry_matches(std::uintptr_t bases, std::uintptr_t class_at, const char* name)
    {
        if (!bases || !memory::is_readable(bases, 0x18))
            return false;
        const auto base = memory::read<std::uintptr_t>(bases + class_at);
        return valid_class(base) && same_name(memory::read<std::uintptr_t>(base + class_name_offset), name);
    }

    bool detect_bases(std::uintptr_t derived, std::uintptr_t root, schema_layout& out)
    {
        bool found = base_entry_matches(memory::read<std::uintptr_t>(derived + out.bases), out.base_class, "CEntityInstance");
        if (!found)
        {
            constexpr std::uintptr_t class_offsets[] = { 0x08, 0x00, 0x10 };
            for (std::uintptr_t bases_at = 0x10; bases_at + 8 <= class_scan_size && !found; bases_at += 8)
            {
                if (bases_at == class_name_offset || bases_at == out.fields)
                    continue;
                const auto bases = memory::read<std::uintptr_t>(derived + bases_at);
                for (const auto class_at : class_offsets)
                {
                    if (!base_entry_matches(bases, class_at, "CEntityInstance"))
                        continue;
                    out.bases = bases_at;
                    out.base_class = class_at;
                    out.base_offset = class_at == 0x00 ? 0x08 : 0x00;
                    out.base_stride = class_at == 0x10 ? 0x18 : 0x10;
                    found = true;
                    break;
                }
            }
        }
        if (!found)
            return false;
        if (memory::read<std::uint8_t>(derived + out.base_count) == 1 && memory::read<std::uint8_t>(root + out.base_count) == 0)
            return true;
        for (std::uintptr_t at = 0x10; at < 0x40; ++at)
        {
            if (memory::read<std::uint8_t>(derived + at) == 1 && memory::read<std::uint8_t>(root + at) == 0)
            {
                out.base_count = at;
                return true;
            }
        }
        return false;
    }

    void dump_class(const char* label, std::uintptr_t info)
    {
        if (!info || !memory::is_readable(info, class_scan_size))
            return;
        for (std::uintptr_t at = 0; at < 0x40; at += 0x20)
        {
            logs::Add(logs::Info, "Schema dump %s+%02X: %016llX %016llX %016llX %016llX", label, static_cast<unsigned>(at),
                memory::read<unsigned long long>(info + at), memory::read<unsigned long long>(info + at + 8),
                memory::read<unsigned long long>(info + at + 0x10), memory::read<unsigned long long>(info + at + 0x18));
        }
    }

    bool detect_layout_unsafe()
    {
        const std::uintptr_t root = find_class_locked("CEntityInstance");
        const std::uintptr_t entity = find_class_locked("C_BaseEntity");
        if (!root || !entity || !memory::is_readable(root, class_scan_size) || !memory::is_readable(entity, class_scan_size))
            return false;
        schema_layout layout{};
        const bool fields = detect_fields(entity, layout);
        const bool offset = fields && detect_field_offset(root, layout);
        const bool bases = detect_bases(entity, root, layout);
        g_layout = layout;
        logs::Add(fields && offset && bases ? logs::Success : logs::Warning,
            "Schema layout: n+%X f+%X s%X nm+%X off+%X b+%X bn+%X bs%X bc+%X [%d%d%d]",
            static_cast<unsigned>(layout.field_count), static_cast<unsigned>(layout.fields), static_cast<unsigned>(layout.field_stride),
            static_cast<unsigned>(layout.field_name), static_cast<unsigned>(layout.field_offset), static_cast<unsigned>(layout.bases),
            static_cast<unsigned>(layout.base_count), static_cast<unsigned>(layout.base_stride), static_cast<unsigned>(layout.base_class),
            fields ? 1 : 0, offset ? 1 : 0, bases ? 1 : 0);
        dump_class("CEntityInstance", root);
        dump_class("C_BaseEntity", entity);
        const auto array = memory::read<std::uintptr_t>(root + layout.fields);
        if (array && memory::is_readable(array, class_scan_size))
            dump_class("fields", array);
        return true;
    }

    void detect_layout()
    {
        if (g_layout_detected)
            return;
        __try
        {
            g_layout_detected = detect_layout_unsafe();
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            g_layout = schema_layout{};
            g_layout_detected = true;
        }
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
        detect_layout();
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
