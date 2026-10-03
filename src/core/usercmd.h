#pragma once
#include <cstddef>
#include <cstdint>

namespace usercmd
{
    constexpr uint64_t kAttack = 1ull << 0;

    struct QAngle
    {
        uint8_t meta[0x10];
        uint32_t has;
        uint32_t pad;
        float x, y, z;
    };

    struct ButtonsPB
    {
        uint8_t meta[0x10];
        uint32_t has;
        uint32_t pad;
        uint64_t state1, state2, state3;
    };

    struct History
    {
        uint8_t meta[0x10];
        uint32_t has;
        uint32_t pad;
        QAngle* angles;
    };

    struct HistoryField
    {
        void* arena;
        int size;
        int total;
        struct
        {
            int allocated;
            int pad;
            History* items[1];
        }* rep;
    };

    struct Base
    {
        uint8_t meta[0x10];
        uint32_t has;
        uint32_t pad;
        uint8_t steps[0x18];
        void* crc;
        ButtonsPB* buttons;
        QAngle* angles;
        uint8_t pad1[0x10];
        float forward, left, up;
    };

    struct ButtonState
    {
        void* vtable;
        uint64_t value, changed, scroll;
    };

    struct Cmd
    {
        void* vtable;
        uint8_t pad0[0x18];
        uint32_t has;
        uint32_t cached;
        HistoryField history;
        Base* base;
        bool leftHand;
        uint8_t pad1[3];
        int attack1;
        int attack2;
        uint8_t pad2[4];
        ButtonState buttons;
    };

    static_assert(offsetof(Base, buttons) == 0x38 && offsetof(Base, angles) == 0x40 && offsetof(Base, forward) == 0x58);
    static_assert(offsetof(ButtonsPB, state1) == 0x18 && offsetof(QAngle, x) == 0x18 && offsetof(History, angles) == 0x18);
    static_assert(offsetof(Cmd, has) == 0x20 && offsetof(Cmd, history) == 0x28 && offsetof(Cmd, base) == 0x40);
    static_assert(offsetof(Cmd, attack1) == 0x4c && offsetof(Cmd, buttons) == 0x58 && offsetof(ButtonState, value) == 0x8);

    bool Attacking(const Cmd* cmd);
    void Attack(Cmd* cmd);
    bool SetAngles(Cmd* cmd, float pitch, float yaw);
    bool ViewAngles(const Cmd* cmd, float& pitch, float& yaw);
}
