#pragma once
#include <cstdint>
#include <vector>
#include <atomic>

namespace skins
{
    struct Entry
    {
        uint32_t uid = 0;
        int def = 0;
        int paint = 0;
        float wear = 0.0001f;
        int seed = 0;
        int stattrak = -1;
        int stickers[5]{};
        char tag[32]{};
        uint8_t equip = 0;
        uint8_t applied = 0;
        uint8_t blocked = 0;
        bool removed = false;
        uint64_t id = 0;
        void* econ = nullptr;
    };

    inline bool enabled = true;
    inline bool knifeAnimations = true;
    inline int paintMode = 0;

    struct Debug
    {
        bool valid = false;
        int def = 0;
        uint64_t itemId = 0;
        int paint = 0;
        int fallbackPaint = 0;
        bool legacy = false;
        bool knife = false;
        uint64_t mask = 0;
        uint64_t attachMask = 0;
        bool attach = false;
        int regenerations = 0;
        char model[96]{};
    };
    inline Debug debug{};
    inline std::atomic<bool> inventoryReady{ false };
    inline std::atomic<int> knifeMode{ 0 };
    inline std::atomic<bool> faulted{ false };

    void Load();
    std::vector<Entry> Snapshot();
    void Add(Entry e);
    void Remove(uint32_t uid);
    void RemoveAll();
    void SetEquip(uint32_t uid, uint8_t mask);
    void OnFrameStage(int stage);
    void Refresh();
    void Cleanup();
}
