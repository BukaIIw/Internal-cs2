#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>

namespace subtick
{
    constexpr int kMaxEvents = 32;

    struct Event
    {
        float when;
        uint32_t pad;
        uint64_t button;
        union
        {
            bool pressed;
            struct
            {
                float forward, left;
            } analog;
        };
        float pitch, yaw;
    };

    struct Input
    {
        uint8_t pad0[0x28];
        uint64_t previous;
        uint64_t down;
        uint64_t pressed;
        uint64_t released;
        float forward, left, up;
        uint8_t pad1[0x8];
        int count;
        Event events[kMaxEvents];
        float pitch, yaw, roll;
    };

    static_assert(sizeof(Event) == 0x20);
    static_assert(offsetof(Input, down) == 0x30 && offsetof(Input, forward) == 0x48 && offsetof(Input, count) == 0x5c);
    static_assert(offsetof(Input, events) == 0x60 && offsetof(Input, pitch) == 0x460);

    template <typename F>
    void Remove(Input* in, F match)
    {
        int kept = 0;
        for (int i = 0; i < in->count && i < kMaxEvents; ++i)
            if (!match(in->events[i]))
                in->events[kept++] = in->events[i];
        in->count = kept;
    }

    inline Event* Insert(Input* in, float when)
    {
        if (in->count < 0 || in->count >= kMaxEvents)
            return nullptr;
        int at = 0;
        while (at < in->count && in->events[at].when <= when)
            ++at;
        const Event* ref = at > 0 ? &in->events[at - 1] : at < in->count ? &in->events[at] : nullptr;
        const float pitch = ref ? ref->pitch : in->pitch;
        const float yaw = ref ? ref->yaw : in->yaw;
        memmove(&in->events[at + 1], &in->events[at], (in->count - at) * sizeof(Event));
        ++in->count;
        Event* e = &in->events[at];
        memset(e, 0, sizeof(Event));
        e->when = when;
        e->pitch = pitch;
        e->yaw = yaw;
        return e;
    }

    inline void Button(Input* in, uint64_t button, bool pressed, float when)
    {
        if (Event* e = Insert(in, when))
        {
            e->button = button;
            e->pressed = pressed;
        }
    }

}
