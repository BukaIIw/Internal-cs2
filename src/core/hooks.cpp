#include "hooks.h"
#include "addresses.h"
#include "cstypes.h"
#include "events.h"
#include "keys.h"
#include "log.h"
#include "memory.h"
#include "patterns.h"
#include "runtime.h"
#include "schema.h"
#include "settings.h"
#include "../features/features.h"
#include "../features/misc/misc.h"
#include "../features/visuals/visuals.h"
#include "../icons.h"
#include "../items.h"
#include "../systems/systems.h"
#include "../ui/fonts.h"
#include "../ui/menu.h"
#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <MinHook.h>
#include <atomic>
#include <climits>
#include <mutex>
#include <thread>
#include <vector>
#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace
{
    constexpr std::size_t present_index = 8;
    constexpr std::size_t resize_buffers_index = 13;
    constexpr std::size_t frame_stage_notify_index = 36;
    constexpr std::size_t create_move_index = 5;
    constexpr std::size_t commit_cmd_index = 6;
    constexpr std::size_t draw_scene_object_index = 1;
    constexpr std::uintptr_t merge_frames_offset = 0xB50;
    constexpr int history_limit = 64;
    constexpr DWORD cleaned_timeout_ms = 2000;
    constexpr DWORD drain_timeout_ms = 3000;
    constexpr int schema_attempts = 50;
    constexpr DWORD schema_retry_ms = 200;

    using present_fn = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT);
    using resize_buffers_fn = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
    using frame_stage_notify_fn = void(__fastcall*)(void*, int);
    using create_move_fn = void*(__fastcall*)(void*, int, bool);
    using merge_subtick_fn = void(__fastcall*)(void*, int);
    using get_user_cmd_fn = void*(__fastcall*)(void*, int);
    using override_view_fn = void(__fastcall*)(void*, void*);
    using camera_think_fn = void(__fastcall*)(void*, int);
    using draw_scene_object_fn = void(__fastcall*)(void*, void*, std::uint8_t*, int, void*, void*, void*, void*);
    using is_glowing_fn = bool(__fastcall*)(void*);

    present_fn o_present = nullptr;
    resize_buffers_fn o_resize_buffers = nullptr;
    frame_stage_notify_fn o_frame_stage_notify = nullptr;
    create_move_fn o_create_move = nullptr;
    merge_subtick_fn o_merge_subtick = nullptr;
    get_user_cmd_fn o_get_user_cmd = nullptr;
    override_view_fn o_override_view = nullptr;
    camera_think_fn o_camera_think = nullptr;
    draw_scene_object_fn o_draw_scene_object = nullptr;
    is_glowing_fn o_is_glowing = nullptr;

    std::atomic<int> g_in_flight{ 0 };

    struct in_flight
    {
        in_flight() { g_in_flight.fetch_add(1, std::memory_order_acq_rel); }
        ~in_flight() { g_in_flight.fetch_sub(1, std::memory_order_acq_rel); }
        in_flight(const in_flight&) = delete;
        in_flight& operator=(const in_flight&) = delete;
    };

    struct fault_gate
    {
        const char* name;
        bool sticky;
        std::atomic<bool> disabled{ false };
        std::atomic<bool> reported{ false };
    };

    fault_gate g_local_gate{ "Local player update", false };
    fault_gate g_prediction_gate{ "Prediction update", false };
    fault_gate g_cmd_gate{ "User command lookup", false };
    fault_gate g_commit_gate{ "User command commit", true };
    fault_gate g_camera_gate{ "Thirdperson camera", true };
    fault_gate g_camera_restore_gate{ "Thirdperson camera restore", false };
    fault_gate g_chams_gate{ "Chams draw", true };
    fault_gate g_glow_gate{ "Glow override", true };

    template <typename F>
    bool seh(F&& fn)
    {
        __try
        {
            fn();
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    template <typename F>
    bool run(fault_gate& gate, F&& fn)
    {
        if (gate.disabled.load(std::memory_order_relaxed))
            return false;
        if (seh(fn))
            return true;
        if (gate.sticky)
            gate.disabled.store(true, std::memory_order_relaxed);
        if (!gate.reported.exchange(true, std::memory_order_relaxed))
            logs::Add(logs::Error, gate.sticky ? "%s faulted and was disabled" : "%s faulted", gate.name);
        return false;
    }

    std::vector<hooks::info> g_hooks;
    std::mutex g_hooks_mutex;
    bool g_minhook = false;
    bool g_can_free = true;
    std::atomic<bool> g_unload_started{ false };
    std::thread g_items_loader;

    std::atomic<DWORD> g_create_move_thread{ 0 };
    std::uintptr_t g_captured_cmd = 0;
    int g_captured_sequence = INT_MIN;
    std::uint64_t g_real_buttons = 0;
    systems::input::subtick_input* g_merged_data = nullptr;
    std::uint64_t g_merged_down = 0;

    struct render_state
    {
        IDXGISwapChain* swapchain = nullptr;
        ID3D11Device* device = nullptr;
        ID3D11DeviceContext* context = nullptr;
        ID3D11RenderTargetView* rtv = nullptr;
        HWND window = nullptr;
        bool initialized = false;
        bool failed = false;
        bool last_menu_open = false;
    };

    render_state g_render{};
    std::atomic<ID3D11Device*> g_device{ nullptr };
    std::atomic<HWND> g_window{ nullptr };
    std::atomic<bool> g_imgui_ready{ false };
    std::atomic<WNDPROC> g_original_wndproc{ nullptr };
    std::atomic<bool> g_wndproc_installed{ false };

    std::uintptr_t in_module(const char* module, std::uintptr_t address)
    {
        return address && memory::get_module(module).contains(address) ? address : 0;
    }

    std::uintptr_t vtable_entry(std::uintptr_t vtable, std::size_t index)
    {
        const auto slot = vtable + index * sizeof(std::uintptr_t);
        if (!vtable || !memory::is_readable(slot, sizeof(std::uintptr_t)))
            return 0;
        return memory::read<std::uintptr_t>(slot);
    }

    std::uintptr_t object_vfunc(std::uintptr_t object, std::size_t index)
    {
        if (!object || !memory::is_readable(object, sizeof(std::uintptr_t)))
            return 0;
        return vtable_entry(memory::read<std::uintptr_t>(object), index);
    }

    std::uintptr_t prefer_proven(const char* name, const char* module, std::uintptr_t pattern, std::uintptr_t proven)
    {
        pattern = in_module(module, pattern);
        proven = in_module(module, proven);
        if (pattern && proven && pattern != proven)
        {
            logs::Add(logs::Warning, "%s: pattern and vtable disagree, using vtable", name);
            return proven;
        }
        return pattern ? pattern : proven;
    }

    void record(const char* name, std::uintptr_t target, hooks::state status, bool required)
    {
        std::lock_guard lock(g_hooks_mutex);
        hooks::info entry{};
        entry.name = name;
        entry.target = target;
        entry.status = status;
        entry.required = required;
        g_hooks.push_back(std::move(entry));
    }

    template <typename T>
    void create(const char* name, std::uintptr_t target, T detour, T& original, bool required)
    {
        auto status = hooks::state::missing;
        if (target && memory::is_readable(target, 16))
        {
            const auto result = MH_CreateHook(reinterpret_cast<LPVOID>(target), reinterpret_cast<LPVOID>(detour), reinterpret_cast<LPVOID*>(&original));
            status = result == MH_OK ? hooks::state::created : hooks::state::failed;
        }
        record(name, target, status, required);
        if (status != hooks::state::created)
            logs::Add(required ? logs::Error : logs::Warning, "Hook %s: %s", name, status == hooks::state::missing ? "target not found" : "creation failed");
    }

    int enable_all()
    {
        std::lock_guard lock(g_hooks_mutex);
        int enabled = 0;
        for (auto& h : g_hooks)
        {
            if (h.status != hooks::state::created)
                continue;
            if (MH_EnableHook(reinterpret_cast<LPVOID>(h.target)) == MH_OK)
            {
                h.status = hooks::state::enabled;
                ++enabled;
            }
            else
            {
                h.status = hooks::state::failed;
                logs::Add(h.required ? logs::Error : logs::Warning, "Hook %s: enable failed", h.name.c_str());
            }
        }
        return enabled;
    }

    bool hook_enabled(const char* name)
    {
        std::lock_guard lock(g_hooks_mutex);
        for (const auto& h : g_hooks)
            if (h.name == name)
                return h.status == hooks::state::enabled;
        return false;
    }

    int menu_key()
    {
        const int key = settings::g_ui.menu_key.key;
        return key > 0 && key < 256 ? key : VK_INSERT;
    }

    void update_cursor(bool release)
    {
        static void* restore_window = nullptr;
        const auto focus = MODULE_EXPORT("SDL3.dll:SDL_GetKeyboardFocus");
        const auto get_relative = MODULE_EXPORT("SDL3.dll:SDL_GetWindowRelativeMouseMode");
        const auto set_relative = MODULE_EXPORT("SDL3.dll:SDL_SetWindowRelativeMouseMode");
        if (!focus || !get_relative || !set_relative)
            return;

        if (release)
        {
            void* window = memory::call<void*>(focus);
            if (window && memory::call<bool>(get_relative, window))
            {
                memory::call<bool>(set_relative, window, false);
                restore_window = window;
            }
        }
        else if (restore_window)
        {
            memory::call<bool>(set_relative, restore_window, true);
            restore_window = nullptr;
        }
    }

    void track_session()
    {
        static std::uintptr_t last_controller = 0;
        static std::uintptr_t last_pawn = 0;

        const auto local = systems::g_local.get();
        if (last_controller && local.controller != last_controller)
        {
            events::publish(events::type::level_shutdown);
            logs::Add(logs::Info, "Level unloaded");
        }
        if (local.controller && local.controller != last_controller)
        {
            events::publish(events::type::level_init);
            logs::Add(logs::Info, "Level loaded");
        }
        if (local.pawn != last_pawn)
            events::publish(events::type::local_pawn_changed);

        last_controller = local.controller;
        last_pawn = local.pawn;
    }

    void publish_unload_once()
    {
        if (!g_unload_started.exchange(true))
            events::publish(events::type::unload);
        hooks::cleaned.store(true);
    }

    std::uint64_t fingerprint(systems::input::usercmd& cmd)
    {
        std::uint64_t hash = 1469598103934665603ull;
        const auto mix = [&hash](const void* data, std::size_t size) {
            const auto* bytes = static_cast<const std::uint8_t*>(data);
            for (std::size_t i = 0; i < size; ++i)
            {
                hash ^= bytes[i];
                hash *= 1099511628211ull;
            }
        };

        const std::uint64_t buttons = cmd.buttons();
        const std::uint64_t changed = cmd.buttons_changed();
        mix(&buttons, sizeof(buttons));
        mix(&changed, sizeof(changed));
        if (const float* forward = cmd.forwardmove())
            mix(forward, sizeof(float));
        if (const float* left = cmd.leftmove())
            mix(left, sizeof(float));

        math::qangle angles{};
        if (cmd.base_angles(angles))
            mix(&angles, sizeof(angles));

        const int size = cmd.history_size();
        mix(&size, sizeof(size));
        for (int i = 0; i < size && i < history_limit; ++i)
            if (cmd.history_angles(i, angles))
                mix(&angles, sizeof(angles));

        const int attack = cmd.attack1_index();
        mix(&attack, sizeof(attack));
        return hash;
    }

    void process_cmd(std::uintptr_t input)
    {
        const auto controller = systems::g_local.get().controller;
        systems::input::usercmd found{};
        if (controller)
            run(g_cmd_gate, [&] { found = systems::g_input.find_cmd(controller); });

        const std::uintptr_t cmd = found.ptr ? found.ptr : g_captured_cmd;
        if (!cmd)
            return;

        systems::g_input.set_cmd(cmd);
        auto usercmd = systems::g_input.current_cmd();

        std::uint64_t before = 0, after = 0;
        const bool readable = seh([&] { before = fingerprint(usercmd); });
        const bool firing = systems::g_view.firing();

        events::publish(events::type::create_move_post, &usercmd);

        if (readable && seh([&] { after = fingerprint(usercmd); }) && (firing || systems::g_view.firing() || after != before))
            run(g_commit_gate, [&] { memory::call_vfunc<void>(input, commit_cmd_index, cmd); });

        systems::g_input.clear_cmd();
    }

    void __fastcall hk_frame_stage_notify(void* self, int stage)
    {
        in_flight guard;
        o_frame_stage_notify(self, stage);

        const bool unloading = hooks::unloading.load();
        if (unloading || stage == cstypes::frame_stage::update)
            seh([&] { update_cursor(menu::open && !unloading); });

        if (unloading)
        {
            if (!hooks::cleaned.load())
                publish_unload_once();
            return;
        }

        events::frame_stage_args args{ stage };
        events::publish(events::type::frame_stage, &args);

        if (stage == cstypes::frame_stage::update)
        {
            track_session();
        }
    }

    void* __fastcall hk_create_move(void* input, int slot, bool active)
    {
        in_flight guard;
        if (hooks::unloading.load() || slot != 0 || !input)
            return o_create_move(input, slot, active);

        run(g_local_gate, [] { systems::g_local.update(); });

        g_captured_cmd = 0;
        g_captured_sequence = INT_MIN;
        g_merged_data = nullptr;
        g_create_move_thread.store(GetCurrentThreadId());
        void* result = o_create_move(input, slot, active);
        g_create_move_thread.store(0);
        if (g_merged_data)
        {
            auto* merged = g_merged_data;
            const std::uint64_t down = g_merged_down;
            seh([&] { merged->down = down; });
            g_merged_data = nullptr;
        }

        process_cmd(reinterpret_cast<std::uintptr_t>(input));
        return result;
    }

    void __fastcall hk_merge_subtick(void* input, int slot)
    {
        in_flight guard;
        if (hooks::unloading.load() || slot != 0 || !input || g_create_move_thread.load() != GetCurrentThreadId())
            return o_merge_subtick(input, slot);

        const auto base = reinterpret_cast<std::uintptr_t>(input);
        auto* data = reinterpret_cast<systems::input::subtick_input*>(base + systems::input::slot_offset + static_cast<std::uintptr_t>(slot) * systems::input::slot_stride);
        const math::vector2 last{ data->forward, data->left };
        const int frames = memory::read<int>(base + merge_frames_offset);

        o_merge_subtick(input, slot);

        if (frames > 0)
            g_real_buttons = data->down | data->pressed;
        g_merged_data = data;
        g_merged_down = data->down;

        const auto local = systems::g_local.get();
        if (!run(g_prediction_gate, [&] { systems::g_prediction.update(local.controller, local.pawn, last); }))
            systems::g_prediction.invalidate();

        systems::g_input.begin_frame(base, data, last, g_real_buttons);
        if (auto* frame = systems::g_input.current_frame())
            events::publish(events::type::create_move, frame);
        systems::g_input.end_frame();
    }

    void* __fastcall hk_get_user_cmd(void* controller, int sequence)
    {
        in_flight guard;
        void* cmd = o_get_user_cmd(controller, sequence);
        if (hooks::unloading.load() || !cmd || g_create_move_thread.load() != GetCurrentThreadId())
            return cmd;
        if (sequence >= g_captured_sequence)
        {
            g_captured_cmd = reinterpret_cast<std::uintptr_t>(cmd);
            g_captured_sequence = sequence;
        }
        return cmd;
    }

    void __fastcall hk_override_view(void* client_mode, void* view_setup)
    {
        in_flight guard;
        if (hooks::unloading.load())
            return o_override_view(client_mode, view_setup);

        o_override_view(client_mode, view_setup);
        events::override_view_args args{ reinterpret_cast<std::uintptr_t>(client_mode), reinterpret_cast<std::uintptr_t>(view_setup) };
        events::publish(events::type::override_view, &args);
    }

    void __fastcall hk_camera_think(void* input, int slot)
    {
        in_flight guard;
        if (hooks::unloading.load() || !input)
            return o_camera_think(input, slot);

        const auto address = reinterpret_cast<std::uintptr_t>(input);
        bool restore = false;
        run(g_camera_gate, [&] { restore = features::misc::g_thirdperson.on_camera_think(address, slot); });

        o_camera_think(input, slot);

        if (restore && !run(g_camera_restore_gate, [&] { features::misc::g_thirdperson.after_camera_think(address, slot); }))
            g_camera_gate.disabled.store(true, std::memory_order_relaxed);
    }

    void __fastcall hk_draw_scene_object(void* desc, void* ctx, std::uint8_t* meshes, int count, void* view, void* layer, void* a7, void* a8)
    {
        in_flight guard;
        if (!hooks::unloading.load() && desc && meshes && count > 0 && (settings::g_visuals.hands_tint || settings::g_visuals.weapon_tint))
        {
            const auto object = reinterpret_cast<std::uintptr_t>(desc);
            const auto list = reinterpret_cast<std::uintptr_t>(meshes);
            run(g_chams_gate, [&] { features::visuals::g_chams.on_draw_object(object, list, count); });
        }
        o_draw_scene_object(desc, ctx, meshes, count, view, layer, a7, a8);
    }

    bool __fastcall hk_is_glowing(void* property)
    {
        in_flight guard;
        const bool original = o_is_glowing(property);
        if (hooks::unloading.load() || !property || !settings::g_visuals.glow)
            return original;

        bool result = original;
        const auto address = reinterpret_cast<std::uintptr_t>(property);
        if (!run(g_glow_gate, [&] { result = features::visuals::g_glow.override_glow(address, original); }))
            return original;
        return result;
    }

    bool is_input_message(UINT msg)
    {
        switch (msg)
        {
        case WM_MOUSEMOVE:
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
        case WM_MBUTTONDBLCLK:
        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL:
        case WM_XBUTTONDOWN:
        case WM_XBUTTONUP:
        case WM_XBUTTONDBLCLK:
        case WM_KEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP:
        case WM_CHAR:
        case WM_INPUT:
            return true;
        default:
            return false;
        }
    }

    int xbutton_key(WPARAM wparam)
    {
        return GET_XBUTTON_WPARAM(wparam) == XBUTTON1 ? VK_XBUTTON1 : VK_XBUTTON2;
    }

    LRESULT CALLBACK hk_wndproc(HWND window, UINT msg, WPARAM wparam, LPARAM lparam)
    {
        in_flight guard;
        const WNDPROC original = g_original_wndproc.load();
        if (!original)
            return DefWindowProcW(window, msg, wparam, lparam);
        if (hooks::unloading.load())
            return CallWindowProcW(original, window, msg, wparam, lparam);

        if (msg == WM_KILLFOCUS || (msg == WM_ACTIVATEAPP && !wparam))
            for (int vk = 1; vk < 256; ++vk)
                keys::on_key(vk, false);

        int vk = 0;
        bool down = false;
        bool repeat = false;
        switch (msg)
        {
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            vk = static_cast<int>(wparam);
            down = true;
            repeat = (lparam & (1 << 30)) != 0;
            break;
        case WM_KEYUP:
        case WM_SYSKEYUP:
            vk = static_cast<int>(wparam);
            break;
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK:
            vk = VK_LBUTTON;
            down = true;
            break;
        case WM_LBUTTONUP:
            vk = VK_LBUTTON;
            break;
        case WM_RBUTTONDOWN:
        case WM_RBUTTONDBLCLK:
            vk = VK_RBUTTON;
            down = true;
            break;
        case WM_RBUTTONUP:
            vk = VK_RBUTTON;
            break;
        case WM_MBUTTONDOWN:
        case WM_MBUTTONDBLCLK:
            vk = VK_MBUTTON;
            down = true;
            break;
        case WM_MBUTTONUP:
            vk = VK_MBUTTON;
            break;
        case WM_XBUTTONDOWN:
        case WM_XBUTTONDBLCLK:
            vk = xbutton_key(wparam);
            down = true;
            break;
        case WM_XBUTTONUP:
            vk = xbutton_key(wparam);
            break;
        default:
            break;
        }

        if (vk)
        {
            if (!down)
                keys::on_key(vk, false);
            else if (!repeat)
            {
                if (keys::capturing)
                {
                    if (keys::on_key(vk, true))
                        return 0;
                }
                else if (vk == menu_key())
                {
                    menu::open = !menu::open;
                    return 0;
                }
                else if (!menu::open)
                    keys::on_key(vk, true);
            }
        }

        if (menu::open && g_imgui_ready.load())
        {
            ImGui_ImplWin32_WndProcHandler(window, msg, wparam, lparam);
            if (is_input_message(msg))
                return 0;
        }
        return CallWindowProcW(original, window, msg, wparam, lparam);
    }

    void install_wndproc(HWND window)
    {
        const auto current = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(window, GWLP_WNDPROC));
        if (!current)
        {
            logs::Add(logs::Error, "Window procedure not found, input is not captured");
            return;
        }
        g_original_wndproc.store(current);
        const auto previous = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&hk_wndproc)));
        if (!previous)
        {
            logs::Add(logs::Error, "Window procedure could not be replaced");
            return;
        }
        if (previous != current)
            g_original_wndproc.store(previous);
        g_wndproc_installed.store(true);
    }

    bool restore_wndproc()
    {
        if (!g_wndproc_installed.load())
            return true;
        const HWND window = g_render.window;
        if (!window || !IsWindow(window))
        {
            g_wndproc_installed.store(false);
            return true;
        }
        const auto current = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(window, GWLP_WNDPROC));
        if (current != &hk_wndproc)
        {
            logs::Add(logs::Warning, "Window procedure was replaced by another module, staying resident");
            return false;
        }
        SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_original_wndproc.load()));
        g_wndproc_installed.store(false);
        return true;
    }

    void create_rtv(IDXGISwapChain* swapchain)
    {
        ID3D11Texture2D* back = nullptr;
        if (FAILED(swapchain->GetBuffer(0, IID_PPV_ARGS(&back))) || !back)
            return;
        if (FAILED(g_render.device->CreateRenderTargetView(back, nullptr, &g_render.rtv)))
            g_render.rtv = nullptr;
        back->Release();
    }

    void release_rtv()
    {
        if (g_render.rtv)
        {
            g_render.rtv->Release();
            g_render.rtv = nullptr;
        }
    }

    bool initialize_render(IDXGISwapChain* swapchain)
    {
        ID3D11Device* device = nullptr;
        if (FAILED(swapchain->GetDevice(IID_PPV_ARGS(&device))) || !device)
            return false;

        DXGI_SWAP_CHAIN_DESC desc{};
        if (FAILED(swapchain->GetDesc(&desc)) || !desc.OutputWindow)
        {
            device->Release();
            return false;
        }

        ID3D11DeviceContext* context = nullptr;
        device->GetImmediateContext(&context);
        if (!context)
        {
            device->Release();
            return false;
        }

        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        ui::initialize_fonts();

        if (!ImGui_ImplWin32_Init(desc.OutputWindow))
        {
            ImGui::DestroyContext();
            context->Release();
            device->Release();
            g_render.failed = true;
            logs::Add(logs::Error, "Overlay: Win32 backend failed");
            return false;
        }
        if (!ImGui_ImplDX11_Init(device, context))
        {
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
            context->Release();
            device->Release();
            g_render.failed = true;
            logs::Add(logs::Error, "Overlay: DX11 backend failed");
            return false;
        }

        ui::style();
        menu::initialize();
        icons::Init(device);

        g_render.swapchain = swapchain;
        g_render.device = device;
        g_render.context = context;
        g_render.window = desc.OutputWindow;
        g_render.last_menu_open = menu::open;
        g_render.initialized = true;
        create_rtv(swapchain);

        g_device.store(device);
        g_window.store(desc.OutputWindow);
        g_imgui_ready.store(true);
        install_wndproc(desc.OutputWindow);
        return true;
    }

    void render_frame(IDXGISwapChain* swapchain)
    {
        const bool open = menu::open;
        if (open != g_render.last_menu_open)
        {
            g_render.last_menu_open = open;
            bool state = open;
            events::publish(events::type::menu_toggle, &state);
        }

        if (!features::wants_overlay())
            return;

        if (!g_render.rtv)
            create_rtv(swapchain);
        if (!g_render.rtv)
            return;

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        ImGui::GetIO().MouseDrawCursor = open;
        icons::Frame();
        events::publish(events::type::present);
        ImGui::Render();

        ID3D11RenderTargetView* previous_rtv = nullptr;
        ID3D11DepthStencilView* previous_dsv = nullptr;
        g_render.context->OMGetRenderTargets(1, &previous_rtv, &previous_dsv);
        g_render.context->OMSetRenderTargets(1, &g_render.rtv, nullptr);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_render.context->OMSetRenderTargets(1, &previous_rtv, previous_dsv);
        if (previous_rtv)
            previous_rtv->Release();
        if (previous_dsv)
            previous_dsv->Release();
    }

    HRESULT __stdcall hk_present(IDXGISwapChain* swapchain, UINT sync, UINT flags)
    {
        in_flight guard;
        if (hooks::unloading.load() || !swapchain)
            return o_present(swapchain, sync, flags);

        if (!g_render.initialized && (g_render.failed || !initialize_render(swapchain)))
            return o_present(swapchain, sync, flags);

        if (swapchain == g_render.swapchain)
            render_frame(swapchain);
        return o_present(swapchain, sync, flags);
    }

    HRESULT __stdcall hk_resize_buffers(IDXGISwapChain* swapchain, UINT count, UINT width, UINT height, DXGI_FORMAT format, UINT flags)
    {
        in_flight guard;
        if (!g_render.initialized || swapchain != g_render.swapchain)
            return o_resize_buffers(swapchain, count, width, height, format, flags);

        release_rtv();
        if (hooks::unloading.load())
            return o_resize_buffers(swapchain, count, width, height, format, flags);

        ImGui_ImplDX11_InvalidateDeviceObjects();
        const HRESULT result = o_resize_buffers(swapchain, count, width, height, format, flags);
        ImGui_ImplDX11_CreateDeviceObjects();
        return result;
    }

    bool swapchain_functions(std::uintptr_t& present, std::uintptr_t& resize)
    {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"internal_cs2_dx11";
        RegisterClassExW(&wc);
        const HWND hwnd = CreateWindowW(wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, nullptr, nullptr, wc.hInstance, nullptr);
        if (!hwnd)
        {
            UnregisterClassW(wc.lpszClassName, wc.hInstance);
            return false;
        }

        DXGI_SWAP_CHAIN_DESC sd{};
        sd.BufferCount = 1;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = hwnd;
        sd.SampleDesc.Count = 1;
        sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        IDXGISwapChain* swapchain = nullptr;
        ID3D11Device* device = nullptr;
        ID3D11DeviceContext* context = nullptr;
        const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;

        const bool ok = SUCCEEDED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &level, 1, D3D11_SDK_VERSION, &sd, &swapchain, &device, nullptr, &context)) && swapchain;
        if (ok)
        {
            const auto object = reinterpret_cast<std::uintptr_t>(swapchain);
            present = object_vfunc(object, present_index);
            resize = object_vfunc(object, resize_buffers_index);
        }

        if (swapchain)
            swapchain->Release();
        if (context)
            context->Release();
        if (device)
            device->Release();
        DestroyWindow(hwnd);
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return ok && present && resize;
    }

    void install_hooks()
    {
        std::uintptr_t present = 0, resize = 0;
        if (!swapchain_functions(present, resize))
            logs::Add(logs::Error, "Overlay: D3D11 swapchain unavailable");
        create("Present", present, &hk_present, o_present, true);
        create("ResizeBuffers", resize, &hk_resize_buffers, o_resize_buffers, false);

        const auto frame_stage = prefer_proven("FrameStageNotify", "client.dll", PATTERN(patterns::frame_stage_notify),
            object_vfunc(addresses::globals::source2_client(), frame_stage_notify_index));
        create("FrameStageNotify", frame_stage, &hk_frame_stage_notify, o_frame_stage_notify, true);

        const auto create_move = prefer_proven("CreateMove", "client.dll", PATTERN(patterns::create_move),
            vtable_entry(memory::find_vtable("client.dll", ".?AVCCSGOInput@@"), create_move_index));
        create("CreateMove", create_move, &hk_create_move, o_create_move, true);

        create("MergeSubtick", in_module("client.dll", PATTERN(patterns::merge_subtick)), &hk_merge_subtick, o_merge_subtick, true);
        create("GetUserCmd", in_module("client.dll", PATTERN(patterns::get_user_cmd_legacy)), &hk_get_user_cmd, o_get_user_cmd, false);
        create("OverrideView", in_module("client.dll", PATTERN(patterns::override_view)), &hk_override_view, o_override_view, false);
        create("CameraThink", in_module("client.dll", PATTERN(patterns::camera_think)), &hk_camera_think, o_camera_think, false);

        const auto draw = prefer_proven("DrawSceneObject", "scenesystem.dll", PATTERN(patterns::draw_scene_object),
            vtable_entry(memory::find_vtable("scenesystem.dll", ".?AVCAnimatableSceneObjectDesc@@"), draw_scene_object_index));
        create("DrawSceneObject", draw, &hk_draw_scene_object, o_draw_scene_object, false);

        create("IsGlowing", in_module("client.dll", PATTERN(patterns::is_glowing)), &hk_is_glowing, o_is_glowing, false);
    }

    bool drain()
    {
        Sleep(20);
        const ULONGLONG start = GetTickCount64();
        while (g_in_flight.load(std::memory_order_acquire) > 0)
        {
            if (GetTickCount64() - start > drain_timeout_ms)
                return false;
            Sleep(1);
        }
        Sleep(20);
        return g_in_flight.load(std::memory_order_acquire) == 0;
    }

    void wait_cleaned()
    {
        if (!hook_enabled("FrameStageNotify"))
            return;
        const ULONGLONG start = GetTickCount64();
        while (!hooks::cleaned.load() && GetTickCount64() - start < cleaned_timeout_ms)
            Sleep(10);
    }

    void release_render()
    {
        if (g_render.initialized)
        {
            g_imgui_ready.store(false);
            ImGui_ImplDX11_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
            g_render.initialized = false;
        }
        release_rtv();
        if (g_render.context)
        {
            g_render.context->Release();
            g_render.context = nullptr;
        }
        g_device.store(nullptr);
        if (g_render.device)
        {
            g_render.device->Release();
            g_render.device = nullptr;
        }
        g_render.swapchain = nullptr;
    }
}

bool hooks::initialize()
{
    if (MH_Initialize() != MH_OK)
    {
        logs::Add(logs::Error, "MinHook initialization failed");
        return false;
    }
    g_minhook = true;

    addresses::resolve_all();

    bool schema_ready = schema::initialize();
    for (int i = 1; i < schema_attempts && !schema_ready; ++i)
    {
        Sleep(schema_retry_ms);
        schema_ready = schema::initialize();
    }
    if (!schema_ready)
        logs::Add(logs::Error, "Schema system unavailable");

    settings::register_all();
    settings::load();

    if (!systems::g_input.initialize())
        logs::Add(logs::Warning, "CSGOInput not found");
    if (!systems::g_tracing.initialize())
        logs::Add(logs::Warning, "Trace system not found, visibility checks are limited");

    features::register_all();
    features::initialize();

    g_items_loader = std::thread([] {
        try
        {
            items::Load();
        }
        catch (...)
        {
            items::state = -1;
        }
    });

    install_hooks();
    const int enabled = enable_all();

    int total = 0;
    {
        std::lock_guard lock(g_hooks_mutex);
        total = static_cast<int>(g_hooks.size());
    }
    logs::Add(enabled == total ? logs::Success : logs::Warning, "Loaded, %d/%d hooks active. %s opens the menu", enabled, total, keys::key_name(menu_key()));
    return true;
}

void hooks::shutdown()
{
    unloading.store(true);
    menu::open = false;
    keys::capturing = nullptr;

    wait_cleaned();

    if (g_minhook)
        MH_DisableHook(MH_ALL_HOOKS);

    if (!restore_wndproc())
        g_can_free = false;

    const bool drained = drain();
    if (!drained)
    {
        g_can_free = false;
        logs::Add(logs::Error, "Hooks did not drain, staying resident");
    }

    publish_unload_once();

    if (drained && g_minhook)
    {
        MH_RemoveHook(MH_ALL_HOOKS);
        MH_Uninitialize();
        g_minhook = false;
    }

    if (g_items_loader.joinable())
        g_items_loader.join();
    addresses::shutdown();

    if (drained)
    {
        icons::Shutdown();
        release_render();
    }
}

std::vector<hooks::info> hooks::snapshot()
{
    std::lock_guard lock(g_hooks_mutex);
    return g_hooks;
}

void* hooks::device()
{
    return g_device.load();
}

void* hooks::window()
{
    return g_window.load();
}

bool hooks::can_free()
{
    return g_can_free;
}
