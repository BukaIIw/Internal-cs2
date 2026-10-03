#include "hooks.h"
#include "menu.h"
#include "game.h"
#include "items.h"
#include "skins.h"
#include "movement.h"
#include "misc.h"
#include "hands.h"
#include "visuals.h"
#include "icons.h"
#include "mem.h"
#include "core/events.h"
#include "core/patterns.h"
#include "core/wiring.h"
#include "ui/ui.h"
#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <string>
#include <thread>
#include <MinHook.h>
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace
{
    using PresentFn = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT);
    using ResizeBuffersFn = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
    using FrameStageFn = void(__fastcall*)(void*, int);

    PresentFn oPresent = nullptr;
    ResizeBuffersFn oResizeBuffers = nullptr;
    FrameStageFn oFrameStage = nullptr;

    HWND window = nullptr;
    WNDPROC oWndProc = nullptr;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    bool initialized = false;
    std::thread loader;

    void CreateRTV(IDXGISwapChain* sc)
    {
        ID3D11Texture2D* back = nullptr;
        if (SUCCEEDED(sc->GetBuffer(0, IID_PPV_ARGS(&back))))
        {
            device->CreateRenderTargetView(back, nullptr, &rtv);
            back->Release();
        }
    }

    void ReleaseRTV()
    {
        if (rtv)
        {
            rtv->Release();
            rtv = nullptr;
        }
    }

    void UpdateCursor()
    {
        static HMODULE sdl = GetModuleHandleA("SDL3.dll");
        if (!sdl)
            return;
        static auto focus = reinterpret_cast<void* (*)()>(GetProcAddress(sdl, "SDL_GetKeyboardFocus"));
        static auto getRelative = reinterpret_cast<bool (*)(void*)>(GetProcAddress(sdl, "SDL_GetWindowRelativeMouseMode"));
        static auto setRelative = reinterpret_cast<bool (*)(void*, bool)>(GetProcAddress(sdl, "SDL_SetWindowRelativeMouseMode"));
        static void* restore = nullptr;
        if (!focus || !getRelative || !setRelative)
            return;
        if (menu::open)
        {
            void* w = focus();
            if (w && getRelative(w))
            {
                setRelative(w, false);
                restore = w;
            }
        }
        else if (restore)
        {
            setRelative(restore, true);
            restore = nullptr;
        }
    }

    void __fastcall hkFrameStage(void* self, int stage)
    {
        oFrameStage(self, stage);
        UpdateCursor();
        if (hooks::unload)
        {
            if (!hooks::cleaned)
            {
                events::Publish(events::Unload);
                hooks::cleaned = true;
            }
            return;
        }
        events::Publish(events::FrameStage, stage);
    }

    LRESULT CALLBACK hkWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (msg == WM_KEYDOWN && wParam == VK_INSERT && !(lParam & (1 << 30)))
        {
            menu::open = !menu::open;
            return 0;
        }

        int vk = 0;
        if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && !(lParam & (1 << 30)))
            vk = static_cast<int>(wParam);
        else if (misc::capturing && msg == WM_LBUTTONDOWN)
            vk = VK_LBUTTON;
        else if (misc::capturing && msg == WM_RBUTTONDOWN)
            vk = VK_RBUTTON;
        else if (msg == WM_MBUTTONDOWN)
            vk = VK_MBUTTON;
        else if (msg == WM_XBUTTONDOWN)
            vk = GET_XBUTTON_WPARAM(wParam) == XBUTTON1 ? VK_XBUTTON1 : VK_XBUTTON2;
        if (vk && (misc::capturing || !menu::open) && misc::OnKey(vk))
            return 0;


        if (menu::open && initialized)
        {
            ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
            switch (msg)
            {
            case WM_MOUSEMOVE: case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
            case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK: case WM_MBUTTONDOWN:
            case WM_MBUTTONUP: case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL: case WM_XBUTTONDOWN: case WM_XBUTTONUP:
            case WM_KEYDOWN: case WM_KEYUP: case WM_SYSKEYDOWN: case WM_SYSKEYUP: case WM_CHAR: case WM_INPUT:
                return 0;
            }
        }
        return CallWindowProcW(oWndProc, hWnd, msg, wParam, lParam);
    }

    HRESULT __stdcall hkPresent(IDXGISwapChain* sc, UINT sync, UINT flags)
    {
        if (!initialized)
        {
            if (FAILED(sc->GetDevice(IID_PPV_ARGS(&device))))
                return oPresent(sc, sync, flags);
            device->GetImmediateContext(&context);

            DXGI_SWAP_CHAIN_DESC desc{};
            sc->GetDesc(&desc);
            window = desc.OutputWindow;

            CreateRTV(sc);

            ImGui::CreateContext();
            ImGui::GetIO().IniFilename = nullptr;
            ui::LoadFonts();
            ImGui_ImplWin32_Init(window);
            ImGui_ImplDX11_Init(device, context);
            menu::Style();
            icons::Init(device);

            oWndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(hkWndProc)));
            initialized = true;
        }

        if (!menu::NeedsFrame() && !misc::watermark && !misc::keybinds && !visuals::Any())
            return oPresent(sc, sync, flags);

        if (!rtv)
            CreateRTV(sc);

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        ImGui::GetIO().MouseDrawCursor = menu::open;
        static bool menuWasOpen = false;
        if (menu::open != menuWasOpen)
        {
            menuWasOpen = menu::open;
            events::Publish(events::MenuToggle, menu::open ? 1 : 0);
        }
        events::Publish(events::Present);
        menu::Render();
        ImGui::Render();
        context->OMSetRenderTargets(1, &rtv, nullptr);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        return oPresent(sc, sync, flags);
    }

    HRESULT __stdcall hkResizeBuffers(IDXGISwapChain* sc, UINT count, UINT w, UINT h, DXGI_FORMAT fmt, UINT flags)
    {
        ReleaseRTV();
        if (initialized)
            ImGui_ImplDX11_InvalidateDeviceObjects();
        HRESULT hr = oResizeBuffers(sc, count, w, h, fmt, flags);
        if (initialized)
            ImGui_ImplDX11_CreateDeviceObjects();
        return hr;
    }

    bool GetSwapChainVTable(void** present, void** resize)
    {
        WNDCLASSEXW wc{ sizeof(wc), CS_HREDRAW | CS_VREDRAW, DefWindowProcW, 0, 0, GetModuleHandleW(nullptr), nullptr, nullptr, nullptr, nullptr, L"dummy_dx11", nullptr };
        RegisterClassExW(&wc);
        HWND hwnd = CreateWindowW(wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, nullptr, nullptr, wc.hInstance, nullptr);

        DXGI_SWAP_CHAIN_DESC sd{};
        sd.BufferCount = 1;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = hwnd;
        sd.SampleDesc.Count = 1;
        sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        IDXGISwapChain* sc = nullptr;
        ID3D11Device* dev = nullptr;
        ID3D11DeviceContext* ctx = nullptr;
        D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;

        bool ok = SUCCEEDED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &fl, 1, D3D11_SDK_VERSION, &sd, &sc, &dev, nullptr, &ctx));
        if (ok)
        {
            void** vt = *reinterpret_cast<void***>(sc);
            *present = vt[8];
            *resize = vt[13];
        }

        if (sc) sc->Release();
        if (ctx) ctx->Release();
        if (dev) dev->Release();
        DestroyWindow(hwnd);
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return ok;
    }
}

bool hooks::Init()
{
    for (int i = 0; i < 50 && !game::Init(); ++i)
        Sleep(200);

    void* presentAddr = nullptr;
    void* resizeAddr = nullptr;
    if (!GetSwapChainVTable(&presentAddr, &resizeAddr))
        return false;
    if (MH_Initialize() != MH_OK)
        return false;
    if (MH_CreateHook(presentAddr, &hkPresent, reinterpret_cast<void**>(&oPresent)) != MH_OK ||
        MH_CreateHook(resizeAddr, &hkResizeBuffers, reinterpret_cast<void**>(&oResizeBuffers)) != MH_OK)
    {
        MH_Uninitialize();
        return false;
    }
    if (game::Ready())
    {
        void* client = mem::Interface("client.dll", "Source2Client002");
        patterns::Note(mem::Load("client.dll"), "Source2Client002", "iface", client);
        if (client)
            MH_CreateHook((*reinterpret_cast<void***>(client))[36], &hkFrameStage, reinterpret_cast<void**>(&oFrameStage));
    }
    if (game::Ready())
    {
        movement::Install();
        misc::Install();
        hands::Install();
    }
    wiring::Install();

    skins::Load();
    loader = std::thread(items::Load);
    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK)
    {
        loader.join();
        MH_Uninitialize();
        return false;
    }
    return true;
}

void hooks::Shutdown()
{
    menu::open = false;
    misc::capturing = nullptr;
    for (int i = 0; i < 100 && oFrameStage && !cleaned; ++i)
        Sleep(10);
    Sleep(150);

    MH_DisableHook(MH_ALL_HOOKS);
    Sleep(300);
    MH_RemoveHook(MH_ALL_HOOKS);
    MH_Uninitialize();
    Sleep(100);

    if (oWndProc)
        SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(oWndProc));

    if (loader.joinable())
        loader.join();
    icons::Shutdown();

    if (initialized)
    {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }
    ReleaseRTV();
    if (context) context->Release();
    if (device) device->Release();
}
