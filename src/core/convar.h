#pragma once
#include "hash.h"
#include <atomic>
#include <cstdint>

namespace convars
{
    struct convar
    {
        const char* name = nullptr;
        std::uintptr_t object = 0;
        std::uintptr_t value = 0;

        template <typename T>
        T get() const
        {
            if (!value)
                return T{};
            return *reinterpret_cast<const T*>(value);
        }

        template <typename T>
        void set(T v) const
        {
            if (value)
                *reinterpret_cast<T*>(value) = v;
        }
    };

    convar* find(const char* name);

    template <std::uint32_t Hash>
    inline convar* cached(const char* name)
    {
        static std::atomic<convar*> value{ nullptr };
        auto v = value.load(std::memory_order_relaxed);
        if (!v)
        {
            v = find(name);
            if (v)
                value.store(v, std::memory_order_relaxed);
        }
        static convar empty{};
        return v ? v : &empty;
    }
}

#define CONVAR(name) (::convars::cached<::hash::fnv1a(name)>(name))
