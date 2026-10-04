#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace memory
{
    struct section
    {
        std::uintptr_t start = 0;
        std::size_t size = 0;
        bool contains(std::uintptr_t p) const { return p >= start && p < start + size; }
    };

    struct module_info
    {
        const char* name = nullptr;
        std::uintptr_t base = 0;
        std::size_t size = 0;
        section text, rdata, data;
        explicit operator bool() const { return base != 0; }
        bool contains(std::uintptr_t p) const { return p >= base && p < base + size; }
    };

    struct pattern
    {
        std::string module;
        std::vector<std::uint8_t> bytes;
        std::vector<std::uint8_t> mask;
        int marker = -1;
        char marker_kind = 0;
        std::intptr_t offset = 0;
        bool deref = false;
        bool valid = false;
    };

    const module_info& get_module(const char* name);
    std::uintptr_t get_module_base(const char* name);
    std::uintptr_t get_module_export(const char* spec);
    std::uintptr_t get_module_interface(const char* spec);

    pattern parse_pattern(const char* spec);
    std::uintptr_t scan(const section& where, const pattern& p, int* matches = nullptr, int limit = 1);
    std::uintptr_t apply(std::uintptr_t match, const pattern& p);
    std::uintptr_t resolve_pattern(const char* spec, int* matches = nullptr);

    std::uintptr_t rel32(std::uintptr_t instruction, int disp_offset, int length);
    std::uintptr_t find_vtable(const char* module, const char* rtti_name);

    bool is_readable(std::uintptr_t address, std::size_t size = sizeof(void*));

    template <typename T>
    inline T read(std::uintptr_t address)
    {
        return *reinterpret_cast<const T*>(address);
    }

    template <typename T>
    inline void write(std::uintptr_t address, const T& value)
    {
        *reinterpret_cast<T*>(address) = value;
    }

    template <typename T>
    inline T& ref(std::uintptr_t address)
    {
        return *reinterpret_cast<T*>(address);
    }

    template <typename T>
    inline std::optional<T> safe_read(std::uintptr_t address)
    {
        if (!address || !is_readable(address, sizeof(T)))
            return std::nullopt;
        T value{};
        std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(T));
        return value;
    }

    std::string read_string(std::uintptr_t address, std::size_t max = 260);

    inline std::uintptr_t get_vfunc(std::uintptr_t object, std::size_t index)
    {
        return (*reinterpret_cast<std::uintptr_t**>(object))[index];
    }

    template <typename R, typename... A>
    inline R call(std::uintptr_t function, A... args)
    {
        return reinterpret_cast<R(__fastcall*)(A...)>(function)(args...);
    }

    template <typename R, typename... A>
    inline R call_vfunc(std::uintptr_t object, std::size_t index, A... args)
    {
        return reinterpret_cast<R(__fastcall*)(std::uintptr_t, A...)>(get_vfunc(object, index))(object, args...);
    }
}
