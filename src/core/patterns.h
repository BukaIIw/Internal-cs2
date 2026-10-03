#pragma once
#include "../mem.h"
#include <cstdint>
#include <string>
#include <vector>

namespace patterns
{
    enum Status
    {
        Missing,
        Found,
        Unique,
        Ambiguous
    };

    struct Entry
    {
        std::string name;
        std::string module;
        std::string pattern;
        std::string generated;
        std::string kind = "sig";
        uint32_t rva = 0;
        int expect = 1;
        int status = Missing;
        int matches = -1;
        bool pending = false;
    };

    inline bool looseOffsets = true;

    uint8_t* Find(const mem::Module& m, const char* name, const char* pattern);
    std::vector<uint8_t*> FindAll(const mem::Module& m, const char* name, const char* pattern, size_t limit, int expect = 1);
    uint8_t* FindRef(const mem::Module& m, const char* name, const char* pattern, int dispOffset, int length);
    void Note(const mem::Module& m, const char* name, const char* kind, const void* address);

    std::vector<Entry> Snapshot();
    void Verify();
    void Generate(int index);
    void GenerateAll();
    void GenerateAt(const char* module, uint32_t rva);
    std::string Custom(bool* pending);
    bool Busy();
    bool Export();
    void Shutdown();
}
