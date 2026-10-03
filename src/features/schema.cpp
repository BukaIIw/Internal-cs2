#include "schema.h"
#include "../core/patterns.h"
#include "../mem.h"

uint8_t* schema::Code(const char* module, const char* name, const char* pattern)
{
    mem::Module m = mem::Load(module);
    return m ? patterns::Find(m, name, pattern) : nullptr;
}

uint32_t schema::Field(const char* module, const char* name, const char* pattern, int at, int size)
{
    uint8_t* code = Code(module, name, pattern);
    if (!code)
        return 0;
    return size == 1 ? code[at] : *reinterpret_cast<uint32_t*>(code + at);
}
