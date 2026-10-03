#pragma once
#include <cstdint>
#include <vector>

namespace events
{
    enum Type
    {
        FrameStage,
        CreateMove,
        Present,
        Unload,
        LevelInit,
        LevelShutdown,
        PawnChanged,
        MenuToggle,
        Count
    };

    using Handler = void (*)(int arg, void* data);

    struct Info
    {
        int type;
        int index;
        const char* name;
        uint64_t calls;
        double avgUs;
        double maxUs;
        bool faulted;
    };

    void Subscribe(Type type, const char* name, Handler handler);
    void Publish(Type type, int arg = 0, void* data = nullptr);
    const char* Name(int type);
    std::vector<Info> Snapshot();
    void Enable(int type, int index);
    void ResetStats();
}
