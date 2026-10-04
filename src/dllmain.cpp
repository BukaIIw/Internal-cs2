#include <Windows.h>
#include "core/hooks.h"
#include "core/keys.h"
#include "core/runtime.h"
#include "ui/menu.h"

namespace
{
    constexpr DWORD module_poll_ms = 100;
    constexpr DWORD unload_poll_ms = 50;
    constexpr DWORD exit_delay_ms = 100;

    DWORD WINAPI main_thread(LPVOID parameter)
    {
        const auto module = static_cast<HMODULE>(parameter);

        while (!GetModuleHandleA("client.dll"))
            Sleep(module_poll_ms);

        if (!hooks::initialize())
        {
            hooks::shutdown();
            if (hooks::can_free())
                FreeLibraryAndExitThread(module, 0);
            return 0;
        }

        bool end_was_down = true;
        while (!hooks::unloading.load())
        {
            const bool end_down = keys::down(VK_END);
            if (end_down && !end_was_down && !menu::open && !keys::capturing)
                hooks::unloading.store(true);
            end_was_down = end_down;
            Sleep(unload_poll_ms);
        }

        hooks::shutdown();
        if (!hooks::can_free())
            return 0;

        Sleep(exit_delay_ms);
        FreeLibraryAndExitThread(module, 0);
        return 0;
    }
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(module);
        if (HANDLE thread = CreateThread(nullptr, 0, main_thread, module, 0, nullptr))
            CloseHandle(thread);
    }
    return TRUE;
}
