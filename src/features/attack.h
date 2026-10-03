#pragma once
#include <cstdint>

enum class AttackType : int
{
    None = -1,
    Primary = 0,
    Secondary = 1
};

enum class WeaponMode : int
{
    Primary = 0,
    Secondary = 1
};

class Attack
{
public:
    static constexpr uint64_t kInAttack = 1ull << 0;
    static constexpr uint64_t kInAttack2 = 1ull << 11;
    static constexpr int kCount = 2;

    static constexpr uint64_t Button(AttackType t)
    {
        return t == AttackType::Secondary ? kInAttack2 : t == AttackType::Primary ? kInAttack : 0;
    }

    static constexpr int Index(AttackType t)
    {
        return t == AttackType::Secondary ? 1 : t == AttackType::Primary ? 0 : -1;
    }

    static constexpr AttackType FromButtons(uint64_t buttons)
    {
        return buttons & kInAttack ? AttackType::Primary : buttons & kInAttack2 ? AttackType::Secondary : AttackType::None;
    }
};
