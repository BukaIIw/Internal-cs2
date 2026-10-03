#pragma once
#include <cstdint>

namespace schema
{
    uint32_t Field(const char* module, const char* name, const char* pattern, int at, int size = 4);
    uint8_t* Code(const char* module, const char* name, const char* pattern);
}
