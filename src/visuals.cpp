#include "visuals.h"
#include "aim.h"
#include "ragebot.h"
#include "game.h"
#include "items.h"
#include "mem.h"
#include "visibility.h"
#include <Windows.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "imgui.h"

namespace
{
    struct Pair
    {
        int a, b;
    };

    constexpr Pair kSkeleton[] = {
        { 6, 5 }, { 5, 4 }, { 4, 2 }, { 2, 0 },
        { 5, 8 }, { 8, 9 }, { 9, 10 },
        { 5, 13 }, { 13, 14 }, { 14, 15 },
        { 0, 22 }, { 22, 23 }, { 23, 24 },
        { 0, 25 }, { 25, 26 }, { 26, 27 },
    };

    ImU32 Color(const float* c, float alpha = 1.f)
    {
        return ImGui::ColorConvertFloat4ToU32(ImVec4(c[0], c[1], c[2], c[3] * alpha));
    }

    bool Project(const float* m, const game::Vec3& v, ImVec2& out)
    {
        const float w = m[12] * v.x + m[13] * v.y + m[14] * v.z + m[15];
        if (w < 0.01f)
            return false;
        const float x = m[0] * v.x + m[1] * v.y + m[2] * v.z + m[3];
        const float y = m[4] * v.x + m[5] * v.y + m[6] * v.z + m[7];
        const ImVec2 size = ImGui::GetIO().DisplaySize;
        out.x = size.x * 0.5f + size.x * 0.5f * x / w;
        out.y = size.y * 0.5f - size.y * 0.5f * y / w;
        return true;
    }

    void Text(ImDrawList* dl, ImVec2 pos, ImU32 color, const char* text, bool center)
    {
        if (center)
            pos.x -= ImGui::CalcTextSize(text).x * 0.5f;
        dl->AddText(ImVec2(pos.x + 1.f, pos.y + 1.f), IM_COL32(0, 0, 0, 200), text);
        dl->AddText(pos, color, text);
    }

    const char* WeaponName(void* pawn)
    {
        auto services = mem::At<uint8_t*>(pawn, game::off.weaponServices);
        void* weapon = services ? game::Handle(mem::At<uint32_t>(services, game::off.activeWeapon)) : nullptr;
        if (!weapon)
            return nullptr;
        const int def = mem::At<uint16_t>(static_cast<uint8_t*>(weapon) + game::off.attributeManager + game::off.item, game::off.defIndex);
        const items::Item* it = items::Find(def);
        return it ? items::Name(it->name) : nullptr;
    }

    void DrawPlayer(ImDrawList* dl, const float* m, void* controller, void* pawn, bool team, const game::Vec3& eye)
    {
        game::Vec3 origin;
        if (!game::Origin(pawn, origin))
            return;
        float height = 72.f;
        if (game::off.collision)
            if (void* col = mem::At<void*>(pawn, game::off.collision))
            {
                const float z = mem::At<float>(col, game::off.vecMaxs + 8);
                if (z > 30.f && z < 90.f)
                    height = z;
            }
        ImVec2 feet, head;
        if (!Project(m, origin, feet) || !Project(m, game::Vec3{ origin.x, origin.y, origin.z + height + 6.f }, head))
            return;
        const float h = feet.y - head.y;
        if (h < 4.f || h > 4000.f)
            return;
        const float w = h * 0.45f;
        const ImVec2 a(feet.x - w * 0.5f, head.y), b(feet.x + w * 0.5f, feet.y);
        const bool visible = visibility::Visible(pawn);
        const ImU32 col = Color(team ? visuals::teamColor : visible ? visuals::visibleColor : visuals::hiddenColor);

        if (visuals::box)
        {
            dl->AddRect(ImVec2(a.x - 1.f, a.y - 1.f), ImVec2(b.x + 1.f, b.y + 1.f), IM_COL32(0, 0, 0, 180));
            dl->AddRect(ImVec2(a.x + 1.f, a.y + 1.f), ImVec2(b.x - 1.f, b.y - 1.f), IM_COL32(0, 0, 0, 180));
            dl->AddRect(a, b, col);
        }

        if (visuals::health)
        {
            int hp = mem::At<int>(pawn, game::off.health);
            hp = hp < 0 ? 0 : hp > 100 ? 100 : hp;
            const float frac = hp / 100.f;
            const ImVec2 ba(a.x - 6.f, a.y), bb(a.x - 3.f, b.y);
            dl->AddRectFilled(ImVec2(ba.x - 1.f, ba.y - 1.f), ImVec2(bb.x + 1.f, bb.y + 1.f), IM_COL32(0, 0, 0, 180));
            const ImU32 hc = IM_COL32(static_cast<int>(255 * (1.f - frac)), static_cast<int>(255 * frac), 40, 255);
            dl->AddRectFilled(ImVec2(ba.x, bb.y - (bb.y - ba.y) * frac), bb, hc);
            if (hp < 100)
            {
                char t[8];
                snprintf(t, sizeof(t), "%d", hp);
                Text(dl, ImVec2(ba.x - 2.f, bb.y - (bb.y - ba.y) * frac - 6.f), IM_COL32(255, 255, 255, 255), t, true);
            }
        }

        if (visuals::name && game::off.sanitizedName)
        {
            const char* n = mem::At<const char*>(controller, game::off.sanitizedName);
            if (n && *n)
            {
                char buf[64];
                snprintf(buf, sizeof(buf), "%s", n);
                Text(dl, ImVec2(feet.x, a.y - ImGui::GetTextLineHeight() - 2.f), IM_COL32(255, 255, 255, 255), buf, true);
            }
        }

        float below = b.y + 2.f;
        if (visuals::weapon)
            if (const char* wn = WeaponName(pawn))
            {
                Text(dl, ImVec2(feet.x, below), IM_COL32(220, 220, 220, 255), wn, true);
                below += ImGui::GetTextLineHeight();
            }
        if (visuals::distance)
        {
            const float dx = origin.x - eye.x, dy = origin.y - eye.y, dz = origin.z - eye.z;
            char t[16];
            snprintf(t, sizeof(t), "%.0fm", std::sqrt(dx * dx + dy * dy + dz * dz) * 0.0254f);
            Text(dl, ImVec2(feet.x, below), IM_COL32(200, 200, 200, 255), t, true);
        }

        if (visuals::skeleton)
            for (const Pair& p : kSkeleton)
            {
                game::Vec3 va, vb;
                ImVec2 sa, sb;
                if (game::Bone(pawn, p.a, va) && game::Bone(pawn, p.b, vb) && Project(m, va, sa) && Project(m, vb, sb))
                    dl->AddLine(sa, sb, col, 1.2f);
            }

        if (visuals::snaplines)
        {
            const ImVec2 size = ImGui::GetIO().DisplaySize;
            dl->AddLine(ImVec2(size.x * 0.5f, size.y), feet, col);
        }
    }

    void SafeDraw(ImDrawList* dl, const float* m, void* controller, void* pawn, bool team, const game::Vec3& eye)
    {
        __try
        {
            DrawPlayer(dl, m, controller, pawn, team, eye);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void RenderAll()
    {
        const float* m = game::ViewMatrix();
        void* localController = game::LocalController();
        void* local = localController ? game::Handle(mem::At<uint32_t>(localController, game::off.playerPawn)) : nullptr;
        if (!m || !local)
            return;
        ImDrawList* dl = ImGui::GetBackgroundDrawList();
        const int localTeam = mem::At<uint8_t>(local, game::off.teamNum);
        const game::Vec3 eye = game::EyePosition(local);
        const int localIndex = game::LocalIndex();

        if (visuals::fovCircle && (aim::legit || ragebot::enabled))
        {
            const float fov = aim::legit ? aim::legitFov : ragebot::fov;
            const ImVec2 size = ImGui::GetIO().DisplaySize;
            const float r = std::tan(fov * 3.14159265f / 180.f) / (size.x / size.y * 0.75f) * size.x * 0.5f;
            if (r > 0.f && r < size.x)
                dl->AddCircle(ImVec2(size.x * 0.5f, size.y * 0.5f), r, IM_COL32(255, 255, 255, 90), 64);
        }

        if (!visuals::esp)
            return;
        for (int i = 1; i <= 64; ++i)
        {
            const char* designer = nullptr;
            void* controller = game::EntityAt(i, &designer);
            if (!controller || !designer || strcmp(designer, "cs_player_controller") || i == localIndex)
                continue;
            void* pawn = game::Handle(mem::At<uint32_t>(controller, game::off.playerPawn));
            if (!pawn || pawn == local || mem::At<int>(pawn, game::off.health) <= 0)
                continue;
            void* node = mem::At<void*>(pawn, game::off.sceneNode);
            if (!node || (game::off.dormant && mem::At<bool>(node, game::off.dormant)))
                continue;
            const bool team = mem::At<uint8_t>(pawn, game::off.teamNum) == localTeam;
            if (team && !visuals::teammates)
                continue;
            SafeDraw(dl, m, controller, pawn, team, eye);
        }
    }
}

bool visuals::Any()
{
    return esp || fovCircle;
}

void visuals::Render()
{
    if (!game::Ready() || !(esp || fovCircle))
        return;
    __try
    {
        RenderAll();
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
}

void visuals::Cleanup()
{
    esp = fovCircle = false;
}
