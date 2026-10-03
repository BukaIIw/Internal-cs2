#pragma once
#include "../game.h"
#include <cmath>

namespace vm
{
    using game::Vec3;

    constexpr float kPi = 3.14159265f;
    constexpr float kRad = kPi / 180.f;

    inline Vec3 operator+(const Vec3& a, const Vec3& b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
    inline Vec3 operator-(const Vec3& a, const Vec3& b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
    inline Vec3 operator*(const Vec3& a, float s) { return { a.x * s, a.y * s, a.z * s }; }
    inline float Dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
    inline Vec3 Cross(const Vec3& a, const Vec3& b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
    inline float Length(const Vec3& a) { return std::sqrt(Dot(a, a)); }

    inline Vec3 Normalized(const Vec3& a)
    {
        const float l = Length(a);
        return l > 1e-6f ? a * (1.f / l) : Vec3{};
    }

    inline float NormalizeYaw(float a)
    {
        a = std::fmod(a + 180.f, 360.f);
        return (a < 0.f ? a + 360.f : a) - 180.f;
    }

    inline void Basis(float pitch, float yaw, Vec3& forward, Vec3& right, Vec3& up)
    {
        const float sp = std::sin(pitch * kRad), cp = std::cos(pitch * kRad);
        const float sy = std::sin(yaw * kRad), cy = std::cos(yaw * kRad);
        forward = { cp * cy, cp * sy, -sp };
        right = { sy, -cy, 0.f };
        up = { sp * cy, sp * sy, cp };
    }

    inline Vec3 Forward(float pitch, float yaw)
    {
        Vec3 f, r, u;
        Basis(pitch, yaw, f, r, u);
        return f;
    }

    inline void Angles(const Vec3& dir, float& pitch, float& yaw)
    {
        const float hyp = std::sqrt(dir.x * dir.x + dir.y * dir.y);
        pitch = -std::atan2(dir.z, hyp) / kRad;
        yaw = std::atan2(dir.y, dir.x) / kRad;
    }

    inline Vec3 Rotate(const float* q, const Vec3& v)
    {
        const float x = q[0], y = q[1], z = q[2], w = q[3];
        const Vec3 u{ x, y, z };
        const Vec3 t = Cross(u, v) * 2.f;
        return v + t * w + Cross(u, t);
    }
}
