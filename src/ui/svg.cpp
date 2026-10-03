#include "svg.h"
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <unordered_map>
#include <vector>

namespace
{
    constexpr float kPi = 3.14159265f;

    struct Sub
    {
        std::vector<ImVec2> points;
        bool closed = false;
    };

    using Shape = std::vector<Sub>;

    struct Parser
    {
        const char* p;

        void Skip()
        {
            while (*p && (std::isspace(static_cast<unsigned char>(*p)) || *p == ','))
                ++p;
        }

        bool HasNumber()
        {
            Skip();
            return *p && (std::isdigit(static_cast<unsigned char>(*p)) || *p == '-' || *p == '+' || *p == '.');
        }

        float Number()
        {
            Skip();
            char* end = nullptr;
            const float v = std::strtof(p, &end);
            p = end && end != p ? end : p + 1;
            return v;
        }

        ImVec2 Point(ImVec2 origin)
        {
            const float x = Number();
            const float y = Number();
            return ImVec2(origin.x + x, origin.y + y);
        }

        float Flag()
        {
            Skip();
            if (*p == '0' || *p == '1')
                return static_cast<float>(*p++ - '0');
            return Number();
        }
    };

    float Angle(float ux, float uy, float vx, float vy)
    {
        return std::atan2(ux * vy - uy * vx, ux * vx + uy * vy);
    }

    void Arc(std::vector<ImVec2>& out, ImVec2 a, float rx, float ry, float rotation, bool large, bool sweep, ImVec2 b)
    {
        if (a.x == b.x && a.y == b.y)
            return;
        if (rx == 0.f || ry == 0.f)
        {
            out.push_back(b);
            return;
        }
        rx = std::fabs(rx);
        ry = std::fabs(ry);
        const float phi = rotation * kPi / 180.f, c = std::cos(phi), s = std::sin(phi);
        const float dx = (a.x - b.x) * 0.5f, dy = (a.y - b.y) * 0.5f;
        const float x1 = c * dx + s * dy, y1 = -s * dx + c * dy;
        const float lambda = x1 * x1 / (rx * rx) + y1 * y1 / (ry * ry);
        if (lambda > 1.f)
        {
            const float k = std::sqrt(lambda);
            rx *= k;
            ry *= k;
        }
        const float num = rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1;
        const float den = rx * rx * y1 * y1 + ry * ry * x1 * x1;
        float coef = den > 0.f ? std::sqrt(std::fmax(0.f, num / den)) : 0.f;
        if (large == sweep)
            coef = -coef;
        const float cxp = coef * rx * y1 / ry, cyp = -coef * ry * x1 / rx;
        const float cx = c * cxp - s * cyp + (a.x + b.x) * 0.5f, cy = s * cxp + c * cyp + (a.y + b.y) * 0.5f;
        const float t1 = Angle(1.f, 0.f, (x1 - cxp) / rx, (y1 - cyp) / ry);
        float dt = Angle((x1 - cxp) / rx, (y1 - cyp) / ry, (-x1 - cxp) / rx, (-y1 - cyp) / ry);
        if (!sweep && dt > 0.f)
            dt -= 2.f * kPi;
        else if (sweep && dt < 0.f)
            dt += 2.f * kPi;
        const int n = static_cast<int>(std::fmax(4.f, std::ceil(std::fabs(dt) / (kPi / 16.f))));
        for (int i = 1; i <= n; ++i)
        {
            const float t = t1 + dt * i / n;
            out.push_back(ImVec2(c * rx * std::cos(t) - s * ry * std::sin(t) + cx, s * rx * std::cos(t) + c * ry * std::sin(t) + cy));
        }
    }

    void Cubic(std::vector<ImVec2>& out, ImVec2 p0, ImVec2 p1, ImVec2 p2, ImVec2 p3)
    {
        constexpr int n = 14;
        for (int i = 1; i <= n; ++i)
        {
            const float t = static_cast<float>(i) / n, u = 1.f - t;
            const float a = u * u * u, b = 3.f * u * u * t, c = 3.f * u * t * t, d = t * t * t;
            out.push_back(ImVec2(a * p0.x + b * p1.x + c * p2.x + d * p3.x, a * p0.y + b * p1.y + c * p2.y + d * p3.y));
        }
    }

    void Quad(std::vector<ImVec2>& out, ImVec2 p0, ImVec2 p1, ImVec2 p2)
    {
        constexpr int n = 10;
        for (int i = 1; i <= n; ++i)
        {
            const float t = static_cast<float>(i) / n, u = 1.f - t;
            out.push_back(ImVec2(u * u * p0.x + 2.f * u * t * p1.x + t * t * p2.x, u * u * p0.y + 2.f * u * t * p1.y + t * t * p2.y));
        }
    }

    Shape Parse(const char* text)
    {
        Shape shape;
        Parser ps{ text };
        ImVec2 cur(0, 0), start(0, 0), ctrl(0, 0);
        char cmd = 0, last = 0;
        auto sub = [&]() -> Sub& {
            if (shape.empty())
                shape.push_back({ { cur }, false });
            return shape.back();
        };
        while (true)
        {
            ps.Skip();
            if (!*ps.p)
                break;
            if (std::isalpha(static_cast<unsigned char>(*ps.p)))
                cmd = *ps.p++;
            else if (!cmd)
                break;
            const bool rel = std::islower(static_cast<unsigned char>(cmd)) != 0;
            const ImVec2 o = rel ? cur : ImVec2(0, 0);
            switch (std::toupper(static_cast<unsigned char>(cmd)))
            {
            case 'M':
            {
                cur = ps.Point(o);
                start = cur;
                shape.push_back({ { cur }, false });
                cmd = rel ? 'l' : 'L';
                break;
            }
            case 'L':
            {
                cur = ps.Point(o);
                sub().points.push_back(cur);
                break;
            }
            case 'H':
                cur.x = o.x + ps.Number();
                sub().points.push_back(cur);
                break;
            case 'V':
                cur.y = o.y + ps.Number();
                sub().points.push_back(cur);
                break;
            case 'C':
            {
                const ImVec2 a = ps.Point(o);
                const ImVec2 b = ps.Point(o);
                const ImVec2 e = ps.Point(o);
                Cubic(sub().points, cur, a, b, e);
                ctrl = b;
                cur = e;
                break;
            }
            case 'S':
            {
                const ImVec2 a = (last == 'C' || last == 'S') ? ImVec2(2.f * cur.x - ctrl.x, 2.f * cur.y - ctrl.y) : cur;
                const ImVec2 b = ps.Point(o);
                const ImVec2 e = ps.Point(o);
                Cubic(sub().points, cur, a, b, e);
                ctrl = b;
                cur = e;
                break;
            }
            case 'Q':
            {
                const ImVec2 a = ps.Point(o);
                const ImVec2 e = ps.Point(o);
                Quad(sub().points, cur, a, e);
                ctrl = a;
                cur = e;
                break;
            }
            case 'T':
            {
                const ImVec2 a = (last == 'Q' || last == 'T') ? ImVec2(2.f * cur.x - ctrl.x, 2.f * cur.y - ctrl.y) : cur;
                const ImVec2 e = ps.Point(o);
                Quad(sub().points, cur, a, e);
                ctrl = a;
                cur = e;
                break;
            }
            case 'A':
            {
                const float rx = ps.Number();
                const float ry = ps.Number();
                const float rot = ps.Number();
                const bool large = ps.Flag() != 0.f;
                const bool sweep = ps.Flag() != 0.f;
                const ImVec2 e = ps.Point(o);
                Arc(sub().points, cur, rx, ry, rot, large, sweep, e);
                cur = e;
                break;
            }
            case 'Z':
                if (!shape.empty())
                    shape.back().closed = true;
                cur = start;
                break;
            default:
                return shape;
            }
            last = static_cast<char>(std::toupper(static_cast<unsigned char>(cmd)));
            if (last == 'Z')
                cmd = 0;
            if (!ps.HasNumber() && cmd && !std::isalpha(static_cast<unsigned char>(*ps.p)))
                break;
        }
        return shape;
    }

    const Shape& Get(const char* path)
    {
        static std::unordered_map<const char*, Shape> cache;
        auto it = cache.find(path);
        if (it == cache.end())
            it = cache.emplace(path, Parse(path)).first;
        return it->second;
    }

    void Transform(const Sub& s, ImVec2 pos, float scale, std::vector<ImVec2>& out)
    {
        out.resize(s.points.size());
        for (size_t i = 0; i < s.points.size(); ++i)
            out[i] = ImVec2(pos.x + s.points[i].x * scale, pos.y + s.points[i].y * scale);
    }
}

void svg::Stroke(ImDrawList* dl, const char* path, ImVec2 pos, float size, ImU32 color, float thickness)
{
    static std::vector<ImVec2> buf;
    const float scale = size / 24.f;
    pos = ImVec2(std::floor(pos.x + 0.5f), std::floor(pos.y + 0.5f));
    for (const Sub& s : Get(path))
    {
        if (s.points.size() < 2)
            continue;
        Transform(s, pos, scale, buf);
        int n = static_cast<int>(buf.size());
        if (s.closed && n > 2 && std::fabs(buf[0].x - buf[n - 1].x) < 0.01f && std::fabs(buf[0].y - buf[n - 1].y) < 0.01f)
            --n;
        dl->AddPolyline(buf.data(), n, color, thickness * scale, s.closed ? ImDrawFlags_Closed : 0);
    }
}

void svg::Fill(ImDrawList* dl, const char* path, ImVec2 pos, float size, ImU32 color)
{
    static std::vector<ImVec2> buf;
    const float scale = size / 24.f;
    for (const Sub& s : Get(path))
    {
        if (s.points.size() < 3)
            continue;
        Transform(s, pos, scale, buf);
        dl->AddConcavePolyFilled(buf.data(), static_cast<int>(buf.size()), color);
    }
}
