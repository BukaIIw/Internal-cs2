#pragma once
#include <algorithm>
#include <cmath>
#include <numbers>

namespace math
{
    constexpr float pi = std::numbers::pi_v<float>;

    constexpr float deg2rad(float v) { return v * (pi / 180.f); }
    constexpr float rad2deg(float v) { return v * (180.f / pi); }

    struct vector2
    {
        float x = 0.f, y = 0.f;

        constexpr vector2 operator+(const vector2& o) const { return { x + o.x, y + o.y }; }
        constexpr vector2 operator-(const vector2& o) const { return { x - o.x, y - o.y }; }
        constexpr vector2 operator*(float s) const { return { x * s, y * s }; }
        float length() const { return std::sqrt(x * x + y * y); }
    };

    struct vector3
    {
        float x = 0.f, y = 0.f, z = 0.f;

        constexpr vector3() = default;
        constexpr vector3(float x, float y, float z) : x(x), y(y), z(z) {}

        constexpr vector3 operator+(const vector3& o) const { return { x + o.x, y + o.y, z + o.z }; }
        constexpr vector3 operator-(const vector3& o) const { return { x - o.x, y - o.y, z - o.z }; }
        constexpr vector3 operator*(float s) const { return { x * s, y * s, z * s }; }
        constexpr vector3 operator/(float s) const { return { x / s, y / s, z / s }; }
        constexpr vector3 operator-() const { return { -x, -y, -z }; }
        vector3& operator+=(const vector3& o) { x += o.x; y += o.y; z += o.z; return *this; }
        vector3& operator-=(const vector3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
        vector3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }

        constexpr float dot(const vector3& o) const { return x * o.x + y * o.y + z * o.z; }
        constexpr vector3 cross(const vector3& o) const { return { y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x }; }
        float length() const { return std::sqrt(x * x + y * y + z * z); }
        float length_sqr() const { return x * x + y * y + z * z; }
        float length_2d() const { return std::sqrt(x * x + y * y); }
        float distance(const vector3& o) const { return (*this - o).length(); }
        vector3 normalized() const
        {
            const float l = length();
            return l > 1e-6f ? *this / l : vector3{};
        }
        bool is_zero() const { return x == 0.f && y == 0.f && z == 0.f; }
        bool is_valid() const { return std::isfinite(x) && std::isfinite(y) && std::isfinite(z); }
    };

    using qangle = vector3;

    struct matrix3x4
    {
        float m[3][4]{};
        vector3 origin() const { return { m[0][3], m[1][3], m[2][3] }; }
        vector3 transform(const vector3& v) const
        {
            return { v.x * m[0][0] + v.y * m[0][1] + v.z * m[0][2] + m[0][3],
                     v.x * m[1][0] + v.y * m[1][1] + v.z * m[1][2] + m[1][3],
                     v.x * m[2][0] + v.y * m[2][1] + v.z * m[2][2] + m[2][3] };
        }
    };

    struct view_matrix
    {
        float m[4][4]{};
    };

    struct bone
    {
        vector3 position;
        float scale = 1.f;
        float rotation[4]{ 0.f, 0.f, 0.f, 1.f };
        vector3 rotate(const vector3& v) const;
    };
    static_assert(sizeof(bone) == 0x20);

    namespace helpers
    {
        inline void normalize_angle(float& a)
        {
            a = std::remainder(a, 360.f);
        }

        inline float normalized_angle(float a)
        {
            normalize_angle(a);
            return a;
        }

        inline void normalize_angles(qangle& a)
        {
            normalize_angle(a.x);
            normalize_angle(a.y);
            a.z = 0.f;
        }

        inline void clamp_angles(qangle& a)
        {
            a.x = std::clamp(a.x, -89.f, 89.f);
            normalize_angle(a.y);
            a.z = 0.f;
        }

        inline qangle sanitized(qangle a)
        {
            normalize_angles(a);
            clamp_angles(a);
            return a;
        }

        void angle_vectors(const qangle& angles, vector3& forward);
        void angle_vectors(const qangle& angles, vector3& forward, vector3& right, vector3& up);
        void angle_vectors_2d(float yaw, vector3& forward, vector3& right);
        qangle vector_angles(const vector3& forward);
        qangle calc_angle(const vector3& from, const vector3& to);
        float angle_fov(const qangle& view, const qangle& aim);
        vector3 closest_point_on_segment(const vector3& a, const vector3& b, const vector3& p);
        float segment_distance(const vector3& a0, const vector3& a1, const vector3& b0, const vector3& b1);
        bool world_to_screen(const view_matrix& matrix, const vector3& world, float screen_w, float screen_h, vector2& out);
    }
}
