#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>

namespace mem
{
    struct Section
    {
        uint8_t* start = nullptr;
        size_t size = 0;
    };

    struct Module
    {
        const char* name = nullptr;
        uint8_t* base = nullptr;
        size_t size = 0;
        Section text, rdata, data;
        explicit operator bool() const { return base != nullptr; }
        bool Contains(const void* p) const { return p >= base && p < base + size; }
    };

    Module Load(const char* name);
    std::vector<uint8_t*> FindAll(const Section& s, const char* pattern, size_t limit = 64);
    uint8_t* Find(const Section& s, const char* pattern);
    uint8_t* Rel(uint8_t* insn, int dispOffset, int length);
    uint8_t* VTable(const Module& m, const char* typeName);
    uint8_t* StaticInstance(const Module& m, const uint8_t* vtable);
    void* Interface(const char* module, const char* name);

    template <typename R, size_t I, typename... A>
    inline R Call(void* self, A... args)
    {
        return (*reinterpret_cast<R(***)(void*, A...)>(self))[I](self, args...);
    }

    template <typename T>
    inline T& At(void* base, uint32_t offset)
    {
        return *reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(base) + offset);
    }
}
