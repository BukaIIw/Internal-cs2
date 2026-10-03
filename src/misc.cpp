#include "core/patterns.h"
#include "misc.h"
#include "movement.h"
#include "aim.h"
#include "ragebot.h"
#include "ui/ui.h"
#include "game.h"
#include "mem.h"
#include <Windows.h>
#include <MinHook.h>
#include <cstdio>
#include <ctime>
#include "imgui.h"

namespace
{
    using CamThinkFn = void(__fastcall*)(void*, int);

    CamThinkFn oCamThink = nullptr;
    uint8_t* cheats = nullptr;
    uint8_t* idealDist = nullptr;
    bool thirdActive = false;
    bool thirdWanted = false;
    void* thirdPawn = nullptr;
    void* lastInput = nullptr;

    void __fastcall hkCamThink(void* input, int slot)
    {
        uint8_t* flag = game::ConVarValue(cheats);
        if (slot != 0 || !thirdWanted || !flag)
            return oCamThink(input, slot);
        mem::At<bool>(input, 0x229) = true;
        if (float* dist = reinterpret_cast<float*>(game::ConVarValue(idealDist)))
            *dist = misc::thirdDistance;
        const uint8_t saved = *flag;
        *flag = 1;
        oCamThink(input, slot);
        *flag = saved;
        mem::At<bool>(input, 0x229) = true;
    }

    void* LocalPawn()
    {
        void* controller = game::LocalController();
        return controller ? game::Handle(mem::At<uint32_t>(controller, game::off.playerPawn)) : nullptr;
    }
}

void misc::Install()
{
    mem::Module client = mem::Load("client.dll");
    if (!client)
        return;
    cheats = game::FindConVar("sv_cheats");
    idealDist = game::FindConVar("cam_idealdist");
    void* think = patterns::Find(client, "CameraThink", "40 55 53 56 57 41 57 48 8D AC 24 70 FE FF FF 48 81 EC 90 02 00 00 48 63 DA 48 8B F9 48 69 F3 28 09 00 00");
    if (think && cheats && MH_CreateHook(think, &hkCamThink, reinterpret_cast<void**>(&oCamThink)) == MH_OK)
        cameraHooked = true;
}

namespace
{
    misc::Bind* registered[32]{};
    int registeredCount = 0;
}

void misc::Register(Bind* bind)
{
    for (int i = 0; i < registeredCount; ++i)
        if (registered[i] == bind)
            return;
    if (registeredCount < 32)
        registered[registeredCount++] = bind;
}

bool misc::Pressed(const Bind& bind)
{
    if (!bind.key || bind.mode == Always)
        return true;
    if (bind.mode == Hold)
        return (GetAsyncKeyState(bind.key) & 0x8000) != 0;
    return bind.toggled;
}

bool misc::Active(bool enabled, const Bind& bind)
{
    return enabled && Pressed(bind);
}

bool misc::OnKey(int vk)
{
    if (capturing)
    {
        capturing->key = vk == VK_ESCAPE || vk == VK_BACK ? 0 : vk;
        capturing->toggled = false;
        capturing = nullptr;
        return true;
    }
    for (Bind* b : { &thirdBind, &bhopBind, &strafeBind, &aim::legitKey, &aim::triggerKey, &ragebot::key })
        Register(b);
    for (int i = 0; i < registeredCount; ++i)
    {
        Bind* b = registered[i];
        if (b->key && b->key == vk && b->mode == Toggle)
            b->toggled = !b->toggled;
    }
    return false;
}

void misc::OnCreateMove(void* input)
{
    lastInput = input;
    void* pawn = LocalPawn();
    const bool alive = pawn && mem::At<int>(pawn, game::off.health) > 0;
    const bool want = cameraHooked && alive && Active(thirdperson, thirdBind);
    thirdWanted = want;
    auto base = static_cast<uint8_t*>(input);
    if (want == thirdActive)
    {
        if (want && pawn != thirdPawn)
        {
            thirdPawn = pawn;
            mem::Call<void, 0x9d8 / 8>(pawn, true);
        }
        return;
    }
    thirdPawn = want ? pawn : nullptr;
    if (want)
    {
        mem::At<float>(base, 0x230) = mem::At<float>(base, 0x688);
        mem::At<float>(base, 0x234) = mem::At<float>(base, 0x68c);
        mem::At<float>(base, 0x238) = 30.f;
    }
    thirdActive = want;
    mem::At<bool>(base, 0x229) = want;
    mem::At<uint32_t>(base, 0x6a8) = 0;
    if (pawn)
        mem::Call<void, 0x9d8 / 8>(pawn, want);
}

const char* misc::KeyName(int vk)
{
    static char buf[32];
    switch (vk)
    {
    case 0: return "-";
    case VK_LBUTTON: return "MOUSE1";
    case VK_RBUTTON: return "MOUSE2";
    case VK_MBUTTON: return "MOUSE3";
    case VK_XBUTTON1: return "MOUSE4";
    case VK_XBUTTON2: return "MOUSE5";
    case VK_SPACE: return "SPACE";
    case VK_SHIFT: return "SHIFT";
    case VK_CONTROL: return "CTRL";
    case VK_MENU: return "ALT";
    case VK_CAPITAL: return "CAPS";
    case VK_TAB: return "TAB";
    }
    UINT scan = MapVirtualKeyA(vk, MAPVK_VK_TO_VSC);
    if (scan && GetKeyNameTextA(static_cast<LONG>(scan << 16), buf, sizeof(buf)) > 0)
        return buf;
    snprintf(buf, sizeof(buf), "0x%02X", vk);
    return buf;
}

void misc::RenderHud()
{
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const ImVec2 screen = ImGui::GetIO().DisplaySize;
    const ImU32 bg = ui::Color(ui::Surface, 0.94f);
    const ImU32 accent = ui::Accent();
    const ImU32 text = ui::Color(ui::Text);
    float y = 10.f;

    if (watermark)
    {
        char line[128];
        time_t t = time(nullptr);
        tm local{};
        localtime_s(&local, &t);
        snprintf(line, sizeof(line), "internal-cs2 | %d fps | %02d:%02d:%02d", static_cast<int>(ImGui::GetIO().Framerate), local.tm_hour, local.tm_min, local.tm_sec);
        const ImVec2 size = ImGui::CalcTextSize(line);
        const ImVec2 a(screen.x - size.x - 26.f, y), b(screen.x - 10.f, y + size.y + 10.f);
        dl->AddRectFilled(a, b, bg, 8.f);
        dl->AddRect(a, b, ui::Color(ui::Border), 8.f);
        dl->AddRectFilled(ImVec2(a.x + 10.f, a.y), ImVec2(b.x - 10.f, a.y + 2.f), accent, 1.f);
        dl->AddText(ImVec2(a.x + 8.f, a.y + 5.f), text, line);
        y = b.y + 8.f;
    }

    if (!keybinds)
        return;
    struct Row
    {
        const char* name;
        const Bind* bind;
        bool on;
    };
    const Row rows[] = {
        { "Thirdperson", &thirdBind, Active(thirdperson, thirdBind) },
        { "Bunnyhop", &bhopBind, Active(movement::bhop, bhopBind) },
        { "Autostrafe", &strafeBind, Active(movement::autostrafe, strafeBind) },
        { "Legitbot", &aim::legitKey, Active(aim::legit, aim::legitKey) },
        { "Triggerbot", &aim::triggerKey, Active(aim::trigger, aim::triggerKey) },
        { "Ragebot", &ragebot::key, Active(ragebot::enabled, ragebot::key) },
    };
    int count = 0;
    for (const Row& r : rows)
        count += r.on;
    if (!count)
        return;
    const float width = 190.f, line = ImGui::GetTextLineHeight() + 4.f;
    const ImVec2 a(screen.x - width - 10.f, y), b(screen.x - 10.f, y + line * (count + 1) + 10.f);
    dl->AddRectFilled(a, b, bg, 8.f);
    dl->AddRect(a, b, ui::Color(ui::Border), 8.f);
    dl->AddRectFilled(ImVec2(a.x + 10.f, a.y), ImVec2(b.x - 10.f, a.y + 2.f), accent, 1.f);
    const char* title = "keybinds";
    dl->AddText(ImVec2(a.x + (width - ImGui::CalcTextSize(title).x) * 0.5f, a.y + 5.f), text, title);
    float ry = a.y + 5.f + line;
    for (const Row& r : rows)
    {
        if (!r.on)
            continue;
        dl->AddText(ImVec2(a.x + 8.f, ry), text, r.name);
        char key[40];
        if (!r.bind->key || r.bind->mode == Always)
            snprintf(key, sizeof(key), "[always]");
        else
            snprintf(key, sizeof(key), "[%s] %s", KeyName(r.bind->key), r.bind->mode == Hold ? "hold" : "toggle");
        dl->AddText(ImVec2(b.x - 8.f - ImGui::CalcTextSize(key).x, ry), ui::Color(ui::TextDim), key);
        ry += line;
    }
}

void misc::Cleanup()
{
    thirdWanted = false;
    capturing = nullptr;
    if (!thirdActive || !lastInput)
        return;
    thirdActive = false;
    mem::At<bool>(lastInput, 0x229) = false;
    mem::At<uint32_t>(lastInput, 0x6a8) = 0;
    if (void* pawn = LocalPawn())
        mem::Call<void, 0x9d8 / 8>(pawn, false);
}
