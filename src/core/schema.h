#pragma once
#include "hash.h"
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace schema
{
    constexpr std::uint32_t invalid = 0xFFFFFFFFu;

    struct missing_field
    {
        std::string class_name;
        std::string field;
    };

    bool initialize();
    bool ready();
    std::uint32_t find(const char* class_name, std::uint32_t field_hash, const char* field_label);
    std::uint32_t find(const char* class_name, const char* field_name);
    int class_size(const char* class_name);
    std::vector<missing_field> missing();

    template <std::uint32_t ClassHash, std::uint32_t FieldHash>
    inline std::uint32_t offset(const char* class_name, const char* field_label)
    {
        static std::atomic<std::uint32_t> value{ invalid };
        static std::atomic<bool> absent{ false };
        auto v = value.load(std::memory_order_relaxed);
        if (v == invalid)
        {
            if (absent.load(std::memory_order_relaxed))
                return 0;
            v = find(class_name, FieldHash, field_label);
            if (v != invalid)
                value.store(v, std::memory_order_relaxed);
            else
            {
                if (ready())
                    absent.store(true, std::memory_order_relaxed);
                v = 0;
            }
        }
        return v;
    }
}

#define SCHEMA(class_name, field_hash) (::schema::offset<::hash::fnv1a(class_name), (field_hash)>(class_name, #field_hash))
