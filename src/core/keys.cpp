#include "keys.h"
#include "hooks.h"
#include <Windows.h>
#include <array>
#include <cstdio>
#include <mutex>
#include <vector>

namespace
{
    std::array<std::atomic<bool>, 256> g_state{};
    std::vector<keys::bind*> g_binds;
    std::mutex g_mutex;

    bool valid_key(int vk)
    {
        return vk > 0 && vk < static_cast<int>(g_state.size());
    }

    bool focused()
    {
        const HWND foreground = GetForegroundWindow();
        if (!foreground)
            return false;
        if (const auto window = static_cast<HWND>(hooks::window()))
            return foreground == window;
        DWORD process = 0;
        GetWindowThreadProcessId(foreground, &process);
        return process == GetCurrentProcessId();
    }

    bool extended_key(int vk)
    {
        switch (vk)
        {
        case VK_INSERT:
        case VK_DELETE:
        case VK_HOME:
        case VK_END:
        case VK_PRIOR:
        case VK_NEXT:
        case VK_LEFT:
        case VK_RIGHT:
        case VK_UP:
        case VK_DOWN:
        case VK_NUMLOCK:
        case VK_DIVIDE:
        case VK_RCONTROL:
        case VK_RMENU:
        case VK_SNAPSHOT:
            return true;
        default:
            return false;
        }
    }
}

void keys::register_bind(bind* b, const char* name)
{
    if (!b)
        return;

    std::lock_guard lock(g_mutex);
    b->name = name;
    for (const auto* existing : g_binds)
        if (existing == b)
            return;
    g_binds.push_back(b);
}

bool keys::active(const bind& b)
{
    if (b.type == mode::off)
        return false;
    if (!b.key || b.type == mode::always)
        return true;
    if (b.type == mode::hold)
        return down(b.key);
    return b.toggled;
}

bool keys::active(bool enabled, const bind& b)
{
    return enabled && active(b);
}

bool keys::down(int vk)
{
    if (!valid_key(vk))
        return false;
    if (g_state[vk].load(std::memory_order_relaxed))
        return true;
    return focused() && (GetAsyncKeyState(vk) & 0x8000) != 0;
}

bool keys::on_key(int vk, bool is_down)
{
    if (!valid_key(vk))
        return false;

    if (!is_down)
    {
        g_state[vk].store(false, std::memory_order_relaxed);
        return false;
    }

    if (bind* target = capturing)
    {
        target->key = vk == VK_ESCAPE ? 0 : vk;
        target->toggled = false;
        capturing = nullptr;
        return true;
    }

    if (g_state[vk].exchange(true, std::memory_order_relaxed))
        return false;

    std::lock_guard lock(g_mutex);
    for (auto* b : g_binds)
        if (b->key == vk && b->type == mode::toggle)
            b->toggled = !b->toggled;
    return false;
}

const char* keys::key_name(int vk)
{
    thread_local char buffer[32];
    switch (vk)
    {
    case 0:
        return "-";
    case VK_LBUTTON:
        return "MOUSE1";
    case VK_RBUTTON:
        return "MOUSE2";
    case VK_MBUTTON:
        return "MOUSE3";
    case VK_XBUTTON1:
        return "MOUSE4";
    case VK_XBUTTON2:
        return "MOUSE5";
    case VK_SPACE:
        return "SPACE";
    case VK_SHIFT:
        return "SHIFT";
    case VK_CONTROL:
        return "CTRL";
    case VK_MENU:
        return "ALT";
    case VK_CAPITAL:
        return "CAPS";
    case VK_TAB:
        return "TAB";
    default:
        break;
    }

    const UINT scan = MapVirtualKeyA(static_cast<UINT>(vk), MAPVK_VK_TO_VSC);
    LONG param = static_cast<LONG>(scan << 16);
    if (extended_key(vk))
        param |= 1 << 24;
    if (scan && GetKeyNameTextA(param, buffer, static_cast<int>(sizeof(buffer))) > 0)
        return buffer;
    std::snprintf(buffer, sizeof(buffer), "0x%02X", vk);
    return buffer;
}

int keys::count()
{
    std::lock_guard lock(g_mutex);
    return static_cast<int>(g_binds.size());
}

keys::bind* keys::at(int index)
{
    std::lock_guard lock(g_mutex);
    if (index < 0 || index >= static_cast<int>(g_binds.size()))
        return nullptr;
    return g_binds[index];
}
