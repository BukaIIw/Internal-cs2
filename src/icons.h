#pragma once
#include <string>
#include <cstdint>

struct ID3D11Device;

namespace icons
{
    void Init(ID3D11Device* device);
    void Shutdown();
    void Frame();
    uint64_t Get(const std::string& path);
}
