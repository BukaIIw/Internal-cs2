#include "math.h"

namespace
{
    constexpr float epsilon = 1e-6f;
    constexpr float min_clip_w = 0.01f;
}

namespace math
{
    vector3 bone::rotate(const vector3& v) const
    {
        const vector3 u{ rotation[0], rotation[1], rotation[2] };
        const float w = rotation[3];
        const vector3 t = u.cross(v) * 2.f;
        return v + t * w + u.cross(t);
    }

    namespace helpers
    {
        void angle_vectors(const qangle& angles, vector3& forward)
        {
            const float sp = std::sin(deg2rad(angles.x)), cp = std::cos(deg2rad(angles.x));
            const float sy = std::sin(deg2rad(angles.y)), cy = std::cos(deg2rad(angles.y));
            forward = { cp * cy, cp * sy, -sp };
        }

        void angle_vectors(const qangle& angles, vector3& forward, vector3& right, vector3& up)
        {
            const float sp = std::sin(deg2rad(angles.x)), cp = std::cos(deg2rad(angles.x));
            const float sy = std::sin(deg2rad(angles.y)), cy = std::cos(deg2rad(angles.y));
            const float sr = std::sin(deg2rad(angles.z)), cr = std::cos(deg2rad(angles.z));
            forward = { cp * cy, cp * sy, -sp };
            right = { -sr * sp * cy + cr * sy, -sr * sp * sy - cr * cy, -sr * cp };
            up = { cr * sp * cy + sr * sy, cr * sp * sy - sr * cy, cr * cp };
        }

        void angle_vectors_2d(float yaw, vector3& forward, vector3& right)
        {
            const float sy = std::sin(deg2rad(yaw)), cy = std::cos(deg2rad(yaw));
            forward = { cy, sy, 0.f };
            right = { sy, -cy, 0.f };
        }

        qangle vector_angles(const vector3& forward)
        {
            if (std::fabs(forward.x) < epsilon && std::fabs(forward.y) < epsilon)
                return { forward.z > 0.f ? -90.f : 90.f, 0.f, 0.f };
            const float hyp = forward.length_2d();
            return { rad2deg(-std::atan2(forward.z, hyp)), rad2deg(std::atan2(forward.y, forward.x)), 0.f };
        }

        qangle calc_angle(const vector3& from, const vector3& to)
        {
            return vector_angles(to - from);
        }

        float angle_fov(const qangle& view, const qangle& aim)
        {
            const float dp = normalized_angle(aim.x - view.x);
            const float dy = normalized_angle(aim.y - view.y);
            return std::sqrt(dp * dp + dy * dy);
        }

        vector3 closest_point_on_segment(const vector3& a, const vector3& b, const vector3& p)
        {
            const vector3 ab = b - a;
            const float len = ab.length_sqr();
            if (len < epsilon)
                return a;
            const float t = std::clamp((p - a).dot(ab) / len, 0.f, 1.f);
            return a + ab * t;
        }

        float segment_distance(const vector3& a0, const vector3& a1, const vector3& b0, const vector3& b1)
        {
            const vector3 d1 = a1 - a0, d2 = b1 - b0, r = a0 - b0;
            const float a = d1.dot(d1), e = d2.dot(d2), f = d2.dot(r);
            if (a <= epsilon && e <= epsilon)
                return r.length();
            float s = 0.f, t = 0.f;
            if (a <= epsilon)
                t = std::clamp(f / e, 0.f, 1.f);
            else
            {
                const float c = d1.dot(r);
                if (e <= epsilon)
                    s = std::clamp(-c / a, 0.f, 1.f);
                else
                {
                    const float b = d1.dot(d2);
                    const float denom = a * e - b * b;
                    s = denom != 0.f ? std::clamp((b * f - c * e) / denom, 0.f, 1.f) : 0.f;
                    t = (b * s + f) / e;
                    if (t < 0.f)
                    {
                        t = 0.f;
                        s = std::clamp(-c / a, 0.f, 1.f);
                    }
                    else if (t > 1.f)
                    {
                        t = 1.f;
                        s = std::clamp((b - c) / a, 0.f, 1.f);
                    }
                }
            }
            return ((a0 + d1 * s) - (b0 + d2 * t)).length();
        }

        bool world_to_screen(const view_matrix& matrix, const vector3& world, float screen_w, float screen_h, vector2& out)
        {
            const auto& m = matrix.m;
            const float w = m[3][0] * world.x + m[3][1] * world.y + m[3][2] * world.z + m[3][3];
            if (!std::isfinite(w) || w < min_clip_w)
                return false;
            const float x = m[0][0] * world.x + m[0][1] * world.y + m[0][2] * world.z + m[0][3];
            const float y = m[1][0] * world.x + m[1][1] * world.y + m[1][2] * world.z + m[1][3];
            const float half_w = screen_w * 0.5f, half_h = screen_h * 0.5f;
            out.x = half_w + half_w * x / w;
            out.y = half_h - half_h * y / w;
            return std::isfinite(out.x) && std::isfinite(out.y);
        }
    }
}
