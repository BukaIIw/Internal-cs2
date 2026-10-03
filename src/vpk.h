#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace vpk
{
    bool Open(const std::wstring& dirFile);
    void Close();
    bool Read(const std::string& path, std::vector<uint8_t>& out);
    const std::vector<std::string>& List(const std::string& dir);
}
