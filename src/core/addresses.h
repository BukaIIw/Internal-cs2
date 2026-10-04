#pragma once
#include "hash.h"
#include "memory.h"
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace addresses
{
    enum class status : std::uint8_t
    {
        pending,
        missing,
        unique,
        ambiguous
    };

    struct entry
    {
        const char* name;
        const char* spec;
        mutable std::atomic<std::uintptr_t> cached{ 0 };
        mutable std::atomic<bool> attempted{ false };
    };

    struct record
    {
        std::string name;
        std::string spec;
        std::string module;
        std::uintptr_t address = 0;
        std::uint32_t rva = 0;
        int matches = -1;
        status state = status::pending;
        std::string generated;
    };

    std::uintptr_t resolve(const entry& e);
    void resolve_all();
    std::vector<record> snapshot();
    void verify();
    void generate(int index);
    void generate_at(const char* module, std::uint32_t rva);
    std::string generated_custom(bool* pending);
    bool busy();
    bool export_list();
    void shutdown();

    namespace globals
    {
        std::uintptr_t schema_system();
        std::uintptr_t cvar();
        std::uintptr_t localize();
        std::uintptr_t engine_client();
        std::uintptr_t source2_client();
        std::uintptr_t entity_system();
        std::uintptr_t global_vars();
        std::uintptr_t csgo_input();
        std::uintptr_t item_system();
        std::uintptr_t trace_manager();
        std::uintptr_t view_matrix();
    }
}

#define PATTERN(e) (::addresses::resolve(e))

#define INTERFACE_(spec)                                                    \
    ([]() -> std::uintptr_t {                                               \
        static std::atomic<std::uintptr_t> value{ 0 };                      \
        auto v = value.load(std::memory_order_relaxed);                     \
        if (!v)                                                             \
            value.store(v = ::memory::get_module_interface(spec));          \
        return v;                                                           \
    }())

#define MODULE_BASE(name)                                                   \
    ([]() -> std::uintptr_t {                                               \
        static std::atomic<std::uintptr_t> value{ 0 };                      \
        auto v = value.load(std::memory_order_relaxed);                     \
        if (!v)                                                             \
            value.store(v = ::memory::get_module_base(name));               \
        return v;                                                           \
    }())

#define MODULE_EXPORT(spec)                                                 \
    ([]() -> std::uintptr_t {                                               \
        static std::atomic<std::uintptr_t> value{ 0 };                      \
        auto v = value.load(std::memory_order_relaxed);                     \
        if (!v)                                                             \
            value.store(v = ::memory::get_module_export(spec));             \
        return v;                                                           \
    }())
