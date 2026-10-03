#include "render.h"
#include <cmath>

namespace
{
    constexpr float kPi = 3.14159265f;
    constexpr int kMaxPoints = 4 * 33;

    struct Outline
    {
        ImVec2 points[kMaxPoints];
        ImVec2 normals[kMaxPoints];
        int count = 0;
    };

    int CornerSegments(float r)
    {
        const int n = static_cast<int>(std::ceil(r * 0.9f));
        return n < 4 ? 4 : n > 32 ? 32 : n;
    }

    void Build(Outline& o, ImVec2 mn, ImVec2 mx, float r)
    {
        r = std::fmax(0.5f, std::fmin(r, std::fmin(mx.x - mn.x, mx.y - mn.y) * 0.5f));
        const ImVec2 centers[4] = { { mn.x + r, mn.y + r }, { mx.x - r, mn.y + r }, { mx.x - r, mx.y - r }, { mn.x + r, mx.y - r } };
        const float starts[4] = { kPi, kPi * 1.5f, 0.f, kPi * 0.5f };
        const int seg = CornerSegments(r);
        o.count = 0;
        for (int k = 0; k < 4; ++k)
            for (int i = 0; i <= seg; ++i)
            {
                const float a = starts[k] + kPi * 0.5f * i / seg;
                const ImVec2 n(std::cos(a), std::sin(a));
                o.normals[o.count] = n;
                o.points[o.count] = ImVec2(centers[k].x + n.x * r, centers[k].y + n.y * r);
                ++o.count;
            }
    }

    float Param(ImVec2 p, ImVec2 mn, ImVec2 mx, render::Direction d)
    {
        const float t = d == render::Vertical ? (p.y - mn.y) / std::fmax(1.f, mx.y - mn.y) : (p.x - mn.x) / std::fmax(1.f, mx.x - mn.x);
        return t < 0.f ? 0.f : t > 1.f ? 1.f : t;
    }
}

ImVec2 render::Snap(ImVec2 p)
{
    return ImVec2(std::floor(p.x + 0.5f), std::floor(p.y + 0.5f));
}

ImU32 render::Fade(ImU32 color, float alpha)
{
    const float a = static_cast<float>((color >> 24) & 0xFF) * (alpha < 0.f ? 0.f : alpha > 1.f ? 1.f : alpha);
    return (color & 0x00FFFFFF) | (static_cast<ImU32>(a + 0.5f) << 24);
}

ImU32 render::Lerp(ImU32 a, ImU32 b, float t)
{
    t = t < 0.f ? 0.f : t > 1.f ? 1.f : t;
    ImU32 out = 0;
    for (int s = 0; s < 32; s += 8)
    {
        const float x = static_cast<float>((a >> s) & 0xFF), y = static_cast<float>((b >> s) & 0xFF);
        out |= static_cast<ImU32>(x + (y - x) * t + 0.5f) << s;
    }
    return out;
}

void render::Rect(ImDrawList* dl, ImVec2 min, ImVec2 max, ImU32 color, float rounding)
{
    dl->AddRectFilled(min, max, color, rounding);
}

void render::Gradient(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 from, ImU32 to, float rounding, Direction direction)
{
    if (mx.x - mn.x < 1.f || mx.y - mn.y < 1.f)
        return;
    static Outline o;
    Build(o, mn, mx, rounding);
    const int n = o.count;
    const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
    const ImVec2 center((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f);
    dl->PrimReserve(n * 3 + n * 6, 1 + n * 2);
    const unsigned base = dl->_VtxCurrentIdx;
    dl->PrimWriteVtx(center, uv, Lerp(from, to, Param(center, mn, mx, direction)));
    for (int i = 0; i < n; ++i)
    {
        const ImVec2 p(o.points[i].x - o.normals[i].x * 0.5f, o.points[i].y - o.normals[i].y * 0.5f);
        dl->PrimWriteVtx(p, uv, Lerp(from, to, Param(p, mn, mx, direction)));
    }
    for (int i = 0; i < n; ++i)
    {
        const ImVec2 p(o.points[i].x + o.normals[i].x * 0.5f, o.points[i].y + o.normals[i].y * 0.5f);
        dl->PrimWriteVtx(p, uv, Fade(Lerp(from, to, Param(p, mn, mx, direction)), 0.f));
    }
    for (int i = 0; i < n; ++i)
    {
        const int j = (i + 1) % n;
        dl->PrimWriteIdx(static_cast<ImDrawIdx>(base));
        dl->PrimWriteIdx(static_cast<ImDrawIdx>(base + 1 + i));
        dl->PrimWriteIdx(static_cast<ImDrawIdx>(base + 1 + j));
        const unsigned a = base + 1 + i, b = base + 1 + j, c = a + n, d = b + n;
        dl->PrimWriteIdx(static_cast<ImDrawIdx>(a));
        dl->PrimWriteIdx(static_cast<ImDrawIdx>(c));
        dl->PrimWriteIdx(static_cast<ImDrawIdx>(d));
        dl->PrimWriteIdx(static_cast<ImDrawIdx>(a));
        dl->PrimWriteIdx(static_cast<ImDrawIdx>(d));
        dl->PrimWriteIdx(static_cast<ImDrawIdx>(b));
    }
}

void render::Border(ImDrawList* dl, ImVec2 min, ImVec2 max, ImU32 color, float rounding, float thickness)
{
    const float h = thickness * 0.5f;
    dl->AddRect(ImVec2(min.x + h, min.y + h), ImVec2(max.x - h, max.y - h), color, rounding, 0, thickness);
}

void render::Shadow(ImDrawList* dl, ImVec2 mn, ImVec2 mx, float rounding, float spread, ImU32 color)
{
    if (((color >> 24) & 0xFF) == 0)
        return;
    static Outline o;
    Build(o, mn, mx, rounding);
    const int n = o.count;
    const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
    const ImU32 clear = Fade(color, 0.f);
    constexpr int kRings = 4;
    dl->PrimReserve((n - 2) * 3 + n * 6 * kRings, n * (kRings + 1));
    const unsigned base = dl->_VtxCurrentIdx;
    for (int ring = 0; ring <= kRings; ++ring)
    {
        const float t = static_cast<float>(ring) / kRings;
        const float falloff = (1.f - t) * (1.f - t);
        const ImU32 col = ring == kRings ? clear : Fade(color, falloff);
        for (int i = 0; i < n; ++i)
            dl->PrimWriteVtx(ImVec2(o.points[i].x + o.normals[i].x * spread * t, o.points[i].y + o.normals[i].y * spread * t), uv, col);
    }
    for (int i = 1; i < n - 1; ++i)
    {
        dl->PrimWriteIdx(static_cast<ImDrawIdx>(base));
        dl->PrimWriteIdx(static_cast<ImDrawIdx>(base + i));
        dl->PrimWriteIdx(static_cast<ImDrawIdx>(base + i + 1));
    }
    for (int ring = 0; ring < kRings; ++ring)
        for (int i = 0; i < n; ++i)
        {
            const int j = (i + 1) % n;
            const unsigned a = base + ring * n + i, b = base + ring * n + j, c = a + n, d = b + n;
            dl->PrimWriteIdx(static_cast<ImDrawIdx>(a));
            dl->PrimWriteIdx(static_cast<ImDrawIdx>(c));
            dl->PrimWriteIdx(static_cast<ImDrawIdx>(d));
            dl->PrimWriteIdx(static_cast<ImDrawIdx>(a));
            dl->PrimWriteIdx(static_cast<ImDrawIdx>(d));
            dl->PrimWriteIdx(static_cast<ImDrawIdx>(b));
        }
}

void render::Glow(ImDrawList* dl, ImVec2 c, float radius, ImU32 color)
{
    if (((color >> 24) & 0xFF) == 0 || radius < 1.f)
        return;
    constexpr int kSeg = 96;
    constexpr int kRings = 6;
    const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
    dl->PrimReserve(kSeg * 3 + kSeg * 6 * (kRings - 1), 1 + kSeg * kRings);
    const unsigned base = dl->_VtxCurrentIdx;
    dl->PrimWriteVtx(c, uv, color);
    for (int r = 1; r <= kRings; ++r)
    {
        const float t = static_cast<float>(r) / kRings;
        const float falloff = r == kRings ? 0.f : std::exp(-4.f * t * t) * (1.f - t);
        for (int i = 0; i < kSeg; ++i)
        {
            const float a = 2.f * kPi * i / kSeg;
            dl->PrimWriteVtx(ImVec2(c.x + std::cos(a) * radius * t, c.y + std::sin(a) * radius * t), uv, Fade(color, falloff));
        }
    }
    for (int i = 0; i < kSeg; ++i)
    {
        const int j = (i + 1) % kSeg;
        dl->PrimWriteIdx(static_cast<ImDrawIdx>(base));
        dl->PrimWriteIdx(static_cast<ImDrawIdx>(base + 1 + i));
        dl->PrimWriteIdx(static_cast<ImDrawIdx>(base + 1 + j));
    }
    for (int r = 0; r < kRings - 1; ++r)
        for (int i = 0; i < kSeg; ++i)
        {
            const int j = (i + 1) % kSeg;
            const unsigned a = base + 1 + r * kSeg + i, b = base + 1 + r * kSeg + j, c2 = a + kSeg, d = b + kSeg;
            dl->PrimWriteIdx(static_cast<ImDrawIdx>(a));
            dl->PrimWriteIdx(static_cast<ImDrawIdx>(c2));
            dl->PrimWriteIdx(static_cast<ImDrawIdx>(d));
            dl->PrimWriteIdx(static_cast<ImDrawIdx>(a));
            dl->PrimWriteIdx(static_cast<ImDrawIdx>(d));
            dl->PrimWriteIdx(static_cast<ImDrawIdx>(b));
        }
}

void render::Circle(ImDrawList* dl, ImVec2 center, float radius, ImU32 color)
{
    const int seg = static_cast<int>(std::fmin(96.f, std::fmax(16.f, radius * 3.f)));
    dl->AddCircleFilled(center, radius, color, seg);
}

void render::Text(ImDrawList* dl, ImFont* font, float size, ImVec2 pos, ImU32 color, const char* text)
{
    dl->AddText(font, size, Snap(pos), color, text);
}
