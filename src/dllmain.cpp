#include <Windows.h>
#include "hooks.h"

static DWORD WINAPI MainThread(LPVOID module)
{
    while (!GetModuleHandleA("client.dll"))
        Sleep(100);

    if (!hooks::Init())
    {
        FreeLibraryAndExitThread(static_cast<HMODULE>(module), 0);
        return 0;
    }

    while (!hooks::unload)
        Sleep(50);

    hooks::Shutdown();
    Sleep(200);
    FreeLibraryAndExitThread(static_cast<HMODULE>(module), 0);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(module);
        if (HANDLE h = CreateThread(nullptr, 0, MainThread, module, 0, nullptr))
            CloseHandle(h);
    }
    return TRUE;
}
