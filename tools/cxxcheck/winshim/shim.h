#pragma once
#include <windows.h>
#if __has_include(<MinHook.h>)
#include <MinHook.h>
template <class A, class B, class C>
inline MH_STATUS MH_CreateHook(A target, B detour, C original)
{
    return MH_CreateHook((LPVOID)target, (LPVOID)detour, (LPVOID*)original);
}
#endif
