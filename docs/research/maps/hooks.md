# Core runtime: startup/unload (dllmain), hook installation (hooks.cpp + MH_CreateHook in movement/misc/hands), DX11 Present/ResizeBuffers + WndProc + ImGui, event bus (core/events), wiring (core/wiring), settings registry (core/settings), logging (core/log), key binds (misc.h Bind)

Files: src/dllmain.cpp, src/hooks.h, src/hooks.cpp, src/core/events.h, src/core/events.cpp, src/core/wiring.h, src/core/wiring.cpp, src/core/settings.h, src/core/settings.cpp, src/core/log.h, src/core/log.cpp, src/misc.h, src/misc.cpp, src/movement.cpp, src/movement.h, src/hands.cpp, src/input.h, src/core/usercmd.h, src/core/patterns.h, src/core/patterns.cpp, src/mem.h, src/mem.cpp, src/features/spread.cpp, src/menu.cpp, src/ui/ui.cpp, Internal-cs2.vcxproj, docs/architecture.md, docs/research/ref/patterns.cpp, docs/research/ref/game_addresses.hpp

## Summary
STARTUP. DllMain(DLL_PROCESS_ATTACH) runs DisableThreadLibraryCalls and then CreateThread(MainThread) (dllmain.cpp:24-33). MainThread polls GetModuleHandleA("client.dll") every 100 ms, then calls hooks::Init(). If Init fails it calls FreeLibraryAndExitThread. Otherwise it polls hooks::unload every 50 ms, then runs hooks::Shutdown(), Sleep(200) and FreeLibraryAndExitThread (dllmain.cpp:4-22).

hooks::Init (hooks.cpp:245-286), step by step:
1. Retries game::Init() up to 50 times with Sleep(200), so up to 10 s. Each retry re-runs every pattern scan; patterns::Register dedups by name.
2. GetSwapChainVTable (hooks.cpp:208-242) creates a dummy window "dummy_dx11" (100x100) and calls D3D11CreateDeviceAndSwapChain (HARDWARE, FL 11_0, R8G8B8A8_UNORM, 1 buffer, DISCARD). It reads swapchain vtable[8]=Present and [13]=ResizeBuffers, then releases everything.
3. MH_Initialize, then MH_CreateHook on Present and ResizeBuffers (fatal if either fails).
4. If game::Ready(): CreateInterface("client.dll","Source2Client002"), patterns::Note(...), then MH_CreateHook(vtable[36] -> hkFrameStage). The result is NOT checked.
5. If game::Ready(): movement::Install() (3 hooks: GetUserCmd, MergeSubtick, CCSGOInput::CreateMove), misc::Install() (CameraThink hook), hands::Install() (CAnimatableSceneObjectDesc vtable[1] DrawArray hook).
6. wiring::Install(): register settings, settings::Load, subscribe events, ray/Hitboxes/Spread/JumpCheck/damage Init, status logs.
7. skins::Load() (inventory.txt), then std::thread loader(items::Load) (VPK parse; sets the items::state atomic).
8. MH_EnableHook(MH_ALL_HOOKS). On failure: loader.join, MH_Uninitialize, return false.
Init returns true even if game::Ready() is false. In that case only Present/Resize are hooked and the menu shows the error.

HOOK INVENTORY (all MinHook detours, enabled together by MH_EnableHook(MH_ALL_HOOKS)):
- **hkPresent** (hooks.cpp:146-195): IDXGISwapChain::Present, vtable index 8 from the dummy swapchain. Prototype HRESULT __stdcall(IDXGISwapChain*, UINT sync, UINT flags). Thread: the game's render/present thread (guess: CS2 presents from its render thread, not the client main thread).
  - First call (lazy init): sc->GetDevice, GetImmediateContext, desc.OutputWindow, CreateRTV(backbuffer 0), ImGui::CreateContext, IniFilename=nullptr, ui::LoadFonts, ImGui_ImplWin32_Init, ImGui_ImplDX11_Init, menu::Style, icons::Init(device) (starts the icon worker thread), then subclasses the window with SetWindowLongPtrW(GWLP_WNDPROC, hkWndProc). Sets initialized=true.
  - Each frame: returns early (no events at all) unless menu::NeedsFrame() || misc::watermark || misc::keybinds || visuals::Any() (hooks.cpp:172).
  - Then: lazy RTV, ImGui NewFrame, MouseDrawCursor=menu::open; publishes MenuToggle(arg=open) on edge (static menuWasOpen); publishes Present; calls menu::Render() directly; ImGui::Render; OMSetRenderTargets(rtv) (the game's RT is not saved or restored); RenderDrawData; oPresent.
- **hkResizeBuffers** (hooks.cpp:197-206): swapchain vtable[13]. HRESULT __stdcall(sc, count, w, h, fmt, flags). Order: ReleaseRTV, ImGui_ImplDX11_InvalidateDeviceObjects, original, ImGui_ImplDX11_CreateDeviceObjects. The RTV is recreated lazily in the next Present that passes the gate.
- **hkFrameStage** (hooks.cpp:92-106): Source2Client002 (client.dll CreateInterface) vtable index 36 (byte 0x120). Prototype void __fastcall(void* self, int stage). Thread: client main thread.
  - Calls the original FIRST, so all subscribers are post-stage.
  - Then UpdateCursor(), called directly: SDL3.dll exports SDL_GetKeyboardFocus / SDL_GetWindowRelativeMouseMode / SDL_SetWindowRelativeMouseMode turn relative mouse mode off while the menu is open and restore it afterwards.
  - If hooks::unload is set: publishes Unload once (sets hooks::cleaned) and returns without publishing FrameStage. Otherwise publishes FrameStage(arg=stage).
- **hkWndProc** (hooks.cpp:108-144): window subclass via SetWindowLongPtrW (not MinHook), installed from the Present thread. Thread: the window's message thread (guess: the CS2 main thread pumping SDL).
  - VK_INSERT keydown (ignoring repeats via lParam bit 30) toggles menu::open and returns 0.
  - Computes vk: keydown/syskeydown (non-repeat); L/R button only while misc::capturing; MBUTTON; XBUTTON1/2. If vk && (capturing || !menu::open), calls misc::OnKey(vk) directly. It returns true only when capturing a bind, and then the message is swallowed.
  - If menu::open && initialized: calls ImGui_ImplWin32_WndProcHandler and swallows mouse/keyboard/char/WM_INPUT messages.
  - Otherwise CallWindowProcW(oWndProc).
- **hkCreateMove** (movement.cpp:455-474): CCSGOInput vtable index 5. The vtable is found by RTTI ".?AVCCSGOInput@@" via mem::VTable. The ref confirms it: create_move = "client.dll:FFFFFFFF488D05*????????48890D????????+28~" (0x28/8=5). Prototype void* __fastcall(CCSGOInput*, int slot, bool active). Thread: client main thread.
  - slot!=0: passes through.
  - SafeMisc publishes CreateMove(arg=0, data=CCSGOInput*) BEFORE the original (subscribers: "Camera"=misc::OnCreateMove, "Spread tick"=Spread's OnCreateMove storing the input pointer).
  - Then a hardcoded gate `!movement::bhop && !autostrafe && !ragebot::enabled && !aim::Any() && !JumpCheck::enabled` passes through.
  - Else: SafeSample(&current), captured=nullptr, activeThread=GetCurrentThreadId(), then the original (inside it, hkGetUserCmd and hkMerge fire), then activeThread=0, debug counters.
  - If a cmd was captured: SafeCmd calls ragebot::OnCmd(cmd) directly. If that returns true it calls CCSGOInput vfunc 6 (mem::Call<void,6>(input, cmd)).
- **hkGetUserCmd** (movement.cpp:435-441): signature "GetUserCmd" (movement.cpp:485). void* __fastcall(void* controller, int sequence). It only records the first cmd returned on activeThread while CreateMove is running.
- **hkMerge** (movement.cpp:412-433): signature "MergeSubtick" (movement.cpp:486). void __fastcall(CCSGOInput*, int slot). Only acts for slot 0 on activeThread.
  - in = (subtick::Input*)(input+0x228). Saves the last forward/left. Reads int frames at input+0xb50.
  - Calls the original. If frames>0, latches realHeld (jump), realKeys (move bits), realSpeed.
  - SafeSample. SafeEdit: Bunnyhop, else JumpCheck::Run; then SubtickStrafe (mode 1 and sv_subtick_movement_view_angles != 0) or Autostrafe.
  - Then calls aim::OnInput and ragebot::OnInput directly (not via events).
- **hkCamThink** (misc.cpp:27-40): signature "CameraThink" (misc.cpp:56). void __fastcall(CCSGOInput*, int slot). It runs the original as-is unless slot==0, third person is wanted, and the sv_cheats value pointer is valid. In that case: input+0x229=true; cam_idealdist value=misc::thirdDistance; sv_cheats value temporarily set to 1 around the original call; input+0x229=true again. Thread: guess, client main thread.
- **hkDrawArray** (hands.cpp:139-153): scenesystem.dll RTTI ".?AVCAnimatableSceneObjectDesc@@" vtable[1]; the address must be inside scenesystem. Prototype void __fastcall(desc, ctx, uint8_t* meshes, int count, view, layer, a7, a8). Thread: scenesystem render job threads, possibly several at once, which is why the hands state uses atomics. It tints the mesh color at +0x50 under SEH, then calls the original. It publishes no events.

There are no other MH_CreateHook calls in src. Levels are not hooked: LevelInit, LevelShutdown and PawnChanged are synthesized by polling (see the events section).

EVENT BUS (core/events):
- Types, in this order: FrameStage, CreateMove, Present, Unload, LevelInit, LevelShutdown, PawnChanged, MenuToggle (Count=8).
- Handler signature: void(*)(int arg, void* data). Untyped payload.
- Storage: static table Subscriber[8][16] (kMax=16). Subscribe silently drops when full, and dedups by handler pointer per type.
- Publish runs subscribers in subscription order. Each call goes through Invoke() with __try/__except(EXCEPTION_EXECUTE_HANDLER), timed with QueryPerformanceCounter (calls, ticks, peak). On an exception the subscriber gets faulted=true and is skipped from then on; it logs "%s stopped after an exception in %s".
- Snapshot/Enable/ResetStats drive Settings -> Events in the menu (menu.cpp:1060-1110, the "Enable" button).
- There is no locking. Subscribe only happens during wiring::Install on MainThread before hooks are enabled. Stats counters are written from several threads (benign races).

Publishers:
- FrameStage: hkFrameStage, arg=stage.
- CreateMove: hkCreateMove via SafeMisc, data=CCSGOInput*.
- Present: hkPresent.
- MenuToggle: hkPresent, arg=open(0/1).
- Unload: hkFrameStage, the first frame after hooks::unload.
- LevelInit (data=controller), PawnChanged (arg=pawn!=null, data=pawn), LevelShutdown: published by the "Session tracker" FrameStage subscriber at stage 7, on transitions of game::LocalController() and the pawn resolved from controller+off.playerPawn (wiring.cpp:100-116).

Subscribers, in registration order (wiring.cpp:216-234, plus spread.cpp:194):
- FrameStage: Session tracker, Skin changer (skins::OnFrameStage), Visibility, Glow, Ragebot tracker, Hands.
- CreateMove: Camera (misc::OnCreateMove), Spread tick (subscribed from inside Spread::Init, and only if its InputHistory sigs resolved).
- Present: ESP (visuals::Render), Overlay (misc::RenderHud).
- LevelInit: Log. LevelShutdown: Log.
- MenuToggle: Autosave (settings::Save on close).
- Unload: Pattern worker (patterns::Shutdown), Settings (Save), Hands cleanup, Camera cleanup (misc::Cleanup), Visuals cleanup, Glow cleanup (glow::Cleanup + visibility::Cleanup), Skin cleanup.
- PawnChanged: no subscribers.

SETTINGS (core/settings):
- Registry is a std::vector<Var{const char* name, Kind, void* ptr}>. Kinds: KBool, KInt, KFloat, KColor (float[4]), KKey (misc::Bind*). Re-registering a name replaces ptr and kind.
- File: %APPDATA%\Internal-cs2\settings.ini (the directory is created on first Path()). It is not a real INI: one "name=value" line per var, in registration order, written with fopen "wb" (not atomic).
- Encodings: bool "0/1"; int "%d"; float "%.4f"; color "%.4f,%.4f,%.4f,%.4f"; key "vk,mode" with mode 0=Toggle, 1=Hold, 2=Always (clamped on load; toggled reset to false).
- Load: fgets with a 256-char line limit, split at the first '=', exact strcmp name match. Unknown keys are ignored and missing keys keep their defaults. No version and no sections.
- Save is called from: the menu button (render thread), MenuToggle close (render thread), Unload (main thread). Load is called from wiring::Install (MainThread) and the menu button (render thread, racing the game threads).
- Every Save logs "Config saved", so every menu close produces a toast.
- skins has its own separate persistence (%APPDATA%\Internal-cs2\inventory.txt, saved inside skins Sync on the FrameStage thread). patterns::Export writes patterns.txt next to settings.ini.

LOG (core/log):
- std::vector<Line{uint64 time=GetTickCount64, int level, char text[160]}> protected by a std::mutex.
- Keeps at most 200 lines, each for at most 10 minutes (Trim on Add and Snapshot).
- Levels: Info, Success, Warning, Error. Snapshot copies the whole vector; the UI uses it for toasts.
- No file or console sink.

BINDS (misc.h, misc.cpp):
- struct Bind {int key=0; bool toggled=false; int mode=0;}, BindMode {Toggle, Hold, Always}.
- Pressed(): key==0 or Always gives true; Hold uses GetAsyncKeyState&0x8000 (works even when the game is unfocused); Toggle reads `toggled`.
- Toggles flip in misc::OnKey from WndProc, but only for binds that are registered. Registration comes from a hardcoded list in OnKey (thirdBind, bhopBind, strafeBind, aim::legitKey, aim::triggerKey, ragebot::key) or lazily from ui::Feature when the widget is drawn (ui.cpp:318). Max 32.
- misc::capturing (Bind*) holds the bind currently being captured by the UI. ESC/BACKSPACE clears the key.

UNLOAD SEQUENCE:
1. Menu "Unload" button (menu.cpp:1023, render thread) sets hooks::unload=true.
2. MainThread leaves its poll and calls Shutdown (hooks.cpp:288-315): menu::open=false and misc::capturing=nullptr (plain non-atomic writes).
3. It waits up to 100x10 ms for `cleaned`, but only if oFrameStage is non-null.
4. Meanwhile, the first hkFrameStage on the main thread publishes Unload (cleanup subscribers run on the game thread) and sets cleaned.
5. Sleep(150); MH_DisableHook(ALL); Sleep(300); MH_RemoveHook(ALL); MH_Uninitialize; Sleep(100).
6. Restore WndProc via SetWindowLongPtrW. Join the items loader. icons::Shutdown (joins the icon worker and releases SRVs). ImGui DX11/Win32 shutdown and DestroyContext. Release RTV, context and device.
7. Back in MainThread: Sleep(200); FreeLibraryAndExitThread.
Nothing tracks in-flight hook calls; the sleeps are the only protection.

THREADS:
- Our MainThread: init, shutdown, initial settings::Load.
- Present/render thread: ImGui, menu, ESP, HUD, MenuToggle autosave, menu-driven settings Load/Save, pattern job pushes.
- Client main thread: FrameStage, CreateMove/GetUserCmd/Merge, CameraThink (guess), WndProc (guess).
- scenesystem workers: DrawArray.
- Background threads: items loader (std::thread in hooks.cpp), patterns worker (lazy std::thread in patterns.cpp), icons worker (std::thread in icons.cpp).

BUILD: MSVC v145, C++20, /EHa (ExceptionHandling=Async, vcxproj:30). Destructors therefore do run during SEH unwind, so a lock_guard inside a faulting subscriber is released. Static CRT (/MT, /MTd). Links d3d11, dxgi, windowscodecs, ole32.

## API
- hooks::Init `bool hooks::Init()` - game::Init retry, dummy-swapchain vtable fetch, MH_Initialize, Present/Resize/FrameStage hooks, feature Install()s, wiring::Install, skins::Load, items loader thread, MH_EnableHook(ALL) (callers: dllmain.cpp:9 MainThread)
- hooks::Shutdown `void hooks::Shutdown()` - close menu, wait for Unload event, disable/remove hooks, restore WndProc, join loader, icons shutdown, ImGui shutdown, release D3D objects (callers: dllmain.cpp:18)
- hooks::unload / hooks::cleaned `inline std::atomic<bool>` - unload request flag (set by menu) and 'Unload event already published' flag (callers: menu.cpp:1023, hooks.cpp:96-101, dllmain.cpp:15)
- events::Subscribe `void Subscribe(Type, const char* name, void(*)(int arg, void* data))` - add subscriber (max 16/type, dedup by handler) (callers: wiring.cpp:216-234, spread.cpp:194)
- events::Publish `void Publish(Type, int arg = 0, void* data = nullptr)` - SEH-guarded, timed dispatch; faulted subscribers skipped (callers: hooks.cpp:100,105,186,188; movement.cpp:447; wiring.cpp:109-113)
- events::Snapshot / Enable / ResetStats / Name `std::vector<Info> Snapshot(); void Enable(int type,int index); void ResetStats(); const char* Name(int)` - menu Events page (stats, re-enable faulted subscriber) (callers: menu.cpp:1070-1106)
- wiring::Install `void wiring::Install()` - register all settings, settings::Load, subscribe all event handlers, init ray/Hitboxes/Spread/JumpCheck/damage, log status (callers: hooks.cpp:275)
- settings::Bool/Int/Float/Color/Key `void Bool(const char*, bool*); Int(const char*, int*); Float(const char*, float*); Color(const char*, float* rgba4); Key(const char*, misc::Bind*)` - register variable by name in the settings registry (callers: wiring.cpp RegisterSettings, ragebot.cpp:509, hitbox.cpp:115, spread.cpp:209, nospread.cpp:16, jumpcheck.cpp:68)
- settings::Save / Load / Path `bool Save(); bool Load(); const char* Path()` - persist and restore registry to %APPDATA%\Internal-cs2\settings.ini (callers: wiring.cpp (Load at init, Save on MenuToggle close and Unload), menu.cpp:1008-1011, patterns.cpp:424)
- logs::Add / Snapshot / Clear `void Add(int level, const char* fmt, ...); std::vector<Line> Snapshot(); void Clear()` - thread-safe ring log, source for toasts and log UI (callers: everywhere)
- misc::Bind / Pressed / Active / Register / OnKey / KeyName `struct Bind{int key; bool toggled; int mode;}; bool Pressed(const Bind&); bool Active(bool enabled, const Bind&); void Register(Bind*); bool OnKey(int vk); const char* KeyName(int vk)` - keybind model (Toggle/Hold/Always), toggle handling from WndProc, capture via misc::capturing (callers: hooks.cpp:127 (OnKey), ui.cpp:318 (Register), movement/aim/ragebot/misc (Active/Pressed))
- movement::Install `void movement::Install()` - hooks CCSGOInput::CreateMove (vtable 5), GetUserCmd, MergeSubtick; resolves GetAimPunch and convars; sets movement::installed (callers: hooks.cpp:271)
- misc::Install `void misc::Install()` - hooks CameraThink; resolves sv_cheats, cam_idealdist; sets misc::cameraHooked (callers: hooks.cpp:272)
- hands::Install `void hands::Install()` - hooks CAnimatableSceneObjectDesc vtable[1] (draw array) in scenesystem.dll (callers: hooks.cpp:273)
- mem::VTable / mem::Interface / mem::Call / mem::At `uint8_t* VTable(const Module&, const char* rttiName); void* Interface(const char* module, const char* name); template<R,I,A...> R Call(void* self, A...); template<T> T& At(void* base, uint32_t off)` - RTTI vtable lookup, CreateInterface, vfunc call by index, raw field access (callers: hooks, movement, misc, hands, game)

## Game facts
- [works] IDXGISwapChain::Present vtable index (from dummy D3D11 device+swapchain): 8; prototype HRESULT __stdcall(IDXGISwapChain*, UINT SyncInterval, UINT Flags) (src/hooks.cpp:232 (vt[8]), typedef hooks.cpp:30)
- [works] IDXGISwapChain::ResizeBuffers vtable index: 13; prototype HRESULT __stdcall(IDXGISwapChain*, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT, UINT SwapChainFlags) (src/hooks.cpp:233, typedef hooks.cpp:31)
- [works] Dummy swapchain parameters used to fetch vtable: class L"dummy_dx11", WS_OVERLAPPEDWINDOW 100x100, BufferCount=1, R8G8B8A8_UNORM, RENDER_TARGET_OUTPUT, SampleDesc.Count=1, Windowed, DXGI_SWAP_EFFECT_DISCARD, D3D_DRIVER_TYPE_HARDWARE, FL 11_0 (src/hooks.cpp:208-242)
- [works] FrameStageNotify location: CreateInterface("client.dll","Source2Client002") -> vtable index 36 (byte offset 0x120); prototype void __fastcall(void* this, int stage) (src/hooks.cpp:264-267, typedef hooks.cpp:32)
- [unknown] Owner reference sig for FrameStageNotify (alternative to vtable index): frame_stage_notify = "client.dll:48895C241848896C2420574883EC40488BF9" (ref/patterns.cpp:111-114)
- [unknown] Frame stage used by every FrameStage subscriber: stage == 7 (hardcoded magic number; enum name not defined in code). Subscribers run AFTER the original FrameStageNotify for that stage. Guess: in the commonly published CS2 enum 7 = FRAME_NET_FULL_FRAME_UPDATE_ON_REMOVE / or post-net-update stage; meaning unverified (src/core/wiring.cpp:104, src/glow.cpp:130, src/hands.cpp:220, src/skins.cpp:661, src/ragebot.cpp:531, src/visibility.cpp:54)
- [works] CCSGOInput vtable found via RTTI: mem::VTable(client, ".?AVCCSGOInput@@"): finds TypeDescriptor name in .data (td = name-0x10, 8-aligned), COL in .rdata with signature 1, offset 0, col[3]=td RVA, col[5]=self RVA; vtable = (address of qword pointing to COL)+8 (src/movement.cpp:482, src/mem.cpp VTable())
- [works] CCSGOInput::CreateMove vtable index: 5; prototype void* __fastcall(CCSGOInput*, int slot, bool active) (return value passed through as void*). Ref confirms: create_move = "client.dll:FFFFFFFF488D05*????????48890D????????+28~" (0x28/8 = 5) (src/movement.cpp:484, typedef :32; ref/patterns.cpp:26-29)
- [unknown] Ref: CCSGOInput handle_view_angles vtable slot: "client.dll:FFFFFFFF488D05*????????48890D????????+40~" -> index 8 (ref/patterns.cpp:256-259)
- [unknown] CCSGOInput vfunc 6 called after ragebot modified the captured CUserCmd: mem::Call<void,6>(CCSGOInput*, CUserCmd*), purpose unknown (guess: re-serialize/commit cmd) (src/movement.cpp:388)
- [works] GetUserCmd signature (current code): "48 89 5C 24 08 57 48 83 EC 20 8B FA E8 ? ? ? ? 48 8B D8 44 8B C7 B8"; prototype void* __fastcall(void* controller, int sequence) (src/movement.cpp:485, typedef :33)
- [unknown] Ref get_usercmd / get_usercmd_base signatures (differ from current code): get_usercmd = "client.dll:40534883EC208BDAE8????????4C8BC0"; get_usercmd_base = "client.dll:4883EC28>E8????????8B8010590000" (ref/patterns.cpp:226-234)
- [works] MergeSubtick signature: "89 54 24 10 48 89 4C 24 08 53 56 57 48 83 EC 70 48 63 DA 48 8D B9 28 02 00 00 48 69 C3 28 09 00 00"; prototype void __fastcall(CCSGOInput*, int slot) (src/movement.cpp:486, typedef :34)
- [works] CCSGOInput per-split-screen-slot input block: slot block starts at CCSGOInput+0x228, stride 0x928 per slot (from 'lea rdi,[rcx+228h]' and 'imul rax,rbx,928h' in MergeSubtick and 'imul rsi,rbx,928h' in CameraThink) (src/movement.cpp:416, sigs :486, src/misc.cpp:56)
- [works] subtick::Input layout (at CCSGOInput+0x228): +0x28 u64 previous; +0x30 u64 down; +0x38 u64 pressed; +0x40 u64 released; +0x48 float forward; +0x4C left; +0x50 up; +0x5C int count; +0x60 Event events[32] (each 0x20: +0 float when, +4 pad, +8 u64 button, +0x10 union{bool pressed | float forward,left}, +0x18 float pitch, +0x1C yaw); +0x460 float pitch, +0x464 yaw, +0x468 roll (src/input.h:5-41 (static_asserts at :39-41))
- [suspect] 'frames' int read in hkMerge: int at CCSGOInput+0xB50 read before original Merge; >0 gates latching of real buttons. Note 0xB50 = 0x228 + 0x928, i.e. first dword after slot 0's block (= start of slot 1) — meaning unconfirmed (src/movement.cpp:419)
- [works] Button bits used in subtick input: IN_ATTACK 1<<0, IN_JUMP 1<<1, forward 1<<3, back 1<<4, moveleft 1<<9, moveright 1<<10, speed/walk 1<<16; kMove = bits 3|4|9|10 (src/movement.cpp:21-25, :167)
- [works] Tick interval / subtick constants: kTick = 1/64; subtick max events 32; autostrafe 16 steps; subtick strafer up to 32 subticks; land pad 0.02, max press 0.98, min press 1/64 (src/movement.cpp:26-29, :184, :262)
- [works] GetAimPunch signature: "48 8B C4 48 89 58 10 48 89 68 18 48 89 70 20 57 48 83 EC 70 48 8B EA 41 0F B6 F0"; called fn(aimPunchServices, float out[3], bool true) (src/movement.cpp:487, :102-104)
- [works] ConVars looked up by movement/camera/jump: sv_airaccelerate (fallback 12), sv_air_max_wishspeed (fallback 30), sv_gravity, sv_maxspeed (both unused in movement), sv_subtick_movement_view_angles (byte value !=0 allows subtick strafe), sv_cheats, cam_idealdist, sv_jump_precision_enable (src/movement.cpp:488-492, src/misc.cpp:54-55, src/features/jumpcheck.cpp:51-52)
- [works] CameraThink signature: "40 55 53 56 57 41 57 48 8D AC 24 70 FE FF FF 48 81 EC 90 02 00 00 48 63 DA 48 8B F9 48 69 F3 28 09 00 00"; prototype void __fastcall(CCSGOInput*, int slot) (src/misc.cpp:56, typedef :17)
- [works] Third-person flag in CCSGOInput: bool at CCSGOInput+0x229 (= slot0 block +1) set true to enable third person camera (src/misc.cpp:32, :39, :135, :242)
- [works] Third-person camera angles in CCSGOInput: +0x230 float cam pitch, +0x234 cam yaw (copied from +0x688/+0x68C = slot0 Input.pitch/yaw at 0x228+0x460), +0x238 float set to 30.f on enable; +0x6A8 uint32 zeroed on toggle (meaning unknown; = slot0 +0x480) (src/misc.cpp:130-136, :243)
- [works] sv_cheats bypass for CameraThink: temporarily write 1 into sv_cheats value byte (game::ConVarValue) around original CameraThink, restore after; cam_idealdist value float overwritten with misc::thirdDistance (default 120) (src/misc.cpp:29-38)
- [unknown] Pawn virtual called on third-person toggle / pawn change: vtable byte offset 0x9D8 (index 315), void(pawn, bool thirdperson); purpose guess: update third-person model visibility (src/misc.cpp:123, :138, :245)
- [works] CAnimatableSceneObjectDesc draw-array hook: scenesystem.dll RTTI ".?AVCAnimatableSceneObjectDesc@@" vtable[1]; prototype void __fastcall(desc, ctx, uint8_t* meshes, int count, void* view, void* layer, void* a7, void* a8). Ref draw_scene_object = "scenesystem.dll:488D05*????????488907488B7C2448+8~" (also index 1); ref draw_scene_object_array sig "scenesystem.dll:488BC4488950??488948??555356574154415541564157488DA8????????4881EC????????0F2970??" (src/hands.cpp:205-215, typedef :20; ref/patterns.cpp:51-59)
- [works] Scene mesh draw entry layout (hands tint): stride 0x70; +0x18 object pointer; +0x50 uint32 color RGBA8 packed r|g<<8|b<<16|a<<24; owner field inside object auto-calibrated by scanning 0..0x400 for viewmodel entity pointer (8-aligned) or entity handle (4-aligned), confirmed after 8 consecutive identical hits (src/hands.cpp:13-18, :47-51, :64-107)
- [works] SDL3 cursor control (menu cursor unlock): SDL3.dll exports SDL_GetKeyboardFocus() -> window, SDL_GetWindowRelativeMouseMode(window) -> bool, SDL_SetWindowRelativeMouseMode(window,bool); disable relative mode while menu open, restore on close; called each FrameStage (src/hooks.cpp:65-90)
- [works] WndProc handling: Subclass via SetWindowLongPtrW(desc.OutputWindow, GWLP_WNDPROC); INSERT (VK_INSERT keydown, lParam bit30 = repeat) toggles menu; while menu open, ImGui_ImplWin32_WndProcHandler then swallow WM_MOUSEMOVE, L/R/M/X button up/down/dblclk, WM_MOUSEWHEEL/HWHEEL, WM_KEYDOWN/UP, WM_SYSKEYDOWN/UP, WM_CHAR, WM_INPUT (src/hooks.cpp:108-144)
- [works] Interface lookup: GetProcAddress(module, "CreateInterface") called as void*(const char* name, int* rc=nullptr) (src/mem.cpp Interface())
- [unknown] Ref hook targets not used by current code (candidates for real LevelInit/Shutdown/OverrideView instead of polling): level_initialization = "client.dll:488D05*????????C6411000+B8~" (vtable idx 23); level_shutdown = "client.dll:4883EC??488B0D????????488D15????????4533C94533C0488B01FF50304885C074??488B0D????????488BD04C8B0141FF50404883C4??"; override_view = "client.dll:A8000000488D05*????????4C89742420+78~" (idx 15); process_input_event = "client.dll:CCCC48895C2408574883EC20C6410800488D05*????????488901488BD9+20~" (idx 4); read_frame_input = "client.dll:>E8????????4D8BC58BD3" (ref/patterns.cpp:316-324, :356-359, :451-459)
- [works] Ref PATTERN string grammar: "module:HEX" with ?? wildcards; '*' marks rel32 to resolve; '>' marks E8/E9 call to follow; '+N'/'-N' add hex offset; trailing '~' dereferences (vtable slot). Hash = FNV-1a 32 (0x811C9DC5, 0x01000193) (ref/game_addresses.hpp:27-34, ref/patterns.cpp)
- [works] Event types and payloads: FrameStage(arg=stage); CreateMove(data=CCSGOInput*, published BEFORE original CreateMove); Present(); Unload(); LevelInit(data=controller); LevelShutdown(); PawnChanged(arg=pawn!=null, data=pawn); MenuToggle(arg=open 0/1). Max 16 subscribers per type (src/core/events.h:7-18, events.cpp:17-18)
- [works] Settings file format: %APPDATA%\Internal-cs2\settings.ini; lines name=value; bool 0/1, int %d, float %.4f, color r,g,b,a (%.4f each), key vk,mode (0 Toggle/1 Hold/2 Always). Line buffer 256 chars (src/core/settings.cpp:46-58, :72-153)
- [works] Full settings key list (registration order): movement.bhop, movement.autostrafe, movement.strafe_mode, bind.bhop, bind.strafe, camera.thirdperson, camera.distance, bind.thirdperson, overlay.watermark, overlay.keybinds, aim.legit, bind.legit, aim.legit_fov, aim.legit_smooth, aim.legit_hitbox, aim.legit_rcs, aim.trigger, bind.trigger, aim.trigger_delay, aim.trigger_head, aim.trigger_neck, aim.trigger_chest, aim.trigger_stomach, aim.trigger_arms, aim.trigger_legs, aim.trigger_min_damage, aim.teammates, visuals.esp, visuals.box, visuals.name, visuals.health, visuals.weapon, visuals.distance, visuals.skeleton, visuals.snaplines, visuals.teammates, visuals.visible_color, visuals.hidden_color, visuals.team_color, visuals.glow, visuals.glow_team, visuals.glow_visibility, visuals.glow_color, visuals.glow_hidden_color, visuals.glow_team_color, visuals.fov_circle, skins.enabled, skins.paint_mode, skins.language, skins.knife_animations, hands.arms, hands.weapon, hands.arms_color, hands.weapon_color, ui.accent, ui.reduce_motion, patterns.loose_offsets, rage.enabled, bind.rage, rage.silent, rage.autofire, rage.fov, rage.prediction, rage.extra_ticks, rage.lock_ticks, rage.point_scale, rage.density, rage.min_damage, hitbox.head, hitbox.neck, hitbox.chest, hitbox.stomach, hitbox.arms, hitbox.legs, hitbox.multipoint, hitbox.head_scale, hitbox.body_scale, hitbox.prefer_body, spread.mode, spread.min_chance, spread.samples, nospread.enabled, nospread.max_search, nospread.iterations, jump.enabled, jump.trace_ground, jump.lead (src/core/wiring.cpp:28-98, src/ragebot.cpp:509-522, src/features/hitbox.cpp:115-127, spread.cpp:209-214, nospread.cpp:16-21, jumpcheck.cpp:68-73)
- [works] Default bind values: aim::legitKey {key=1 (VK_LBUTTON), Hold}; aim::triggerKey {key=6 (VK_XBUTTON2), Hold}; misc::thirdBind {'V', Toggle}; bhop/strafe/rage binds key 0 (= always active) (src/aim.h:8,15; src/misc.h:19-21; src/ragebot.h:9)
- [works] menu::open default: true (menu opens immediately after injection) (src/menu.h:5)
- [works] Log limits: 200 lines max, 10 min lifetime, 160 chars/line, levels Info/Success/Warning/Error (src/core/log.cpp:9-10, log.h:5-17)
- [works] Session tracker semantics (synthetic level events): On FrameStage stage 7 with game::Ready(): controller=game::LocalController(); pawn=game::Handle(controller+off.playerPawn). controller appears -> LevelInit; pawn pointer changes -> PawnChanged; controller disappears -> LevelShutdown (src/core/wiring.cpp:100-116)
- [works] Unload timing constants: wait for cleaned up to 100x10ms (only if oFrameStage), Sleep 150, MH_DisableHook, Sleep 300, MH_RemoveHook+MH_Uninitialize, Sleep 100, then restore WndProc, join threads, ImGui shutdown; MainThread Sleep 200 before FreeLibraryAndExitThread (src/hooks.cpp:288-315, src/dllmain.cpp:18-20)
- [works] Init retry: game::Init retried up to 50 times x 200 ms (10 s); MainThread first waits for client.dll with 100 ms polling (src/hooks.cpp:247-248, src/dllmain.cpp:6-7)

## Defects
- [high] Unload can persist feature-off state. Shutdown sets menu::open=false (hooks.cpp:290). The Present hook keeps running and detects the open->closed edge, publishing MenuToggle(0), whose Autosave subscriber calls settings::Save(). Present (render thread) races the FrameStage Unload publish (main thread). If Present runs after the Unload cleanups (visuals::Cleanup sets esp=fovCircle=false, glow::Cleanup sets glow::enabled=false), the config is saved with ESP, FOV circle and glow turned OFF. (src/hooks.cpp:183-187 + :290; src/core/wiring.cpp:128-132, :229, :232-233; src/visuals.cpp:228-231; src/glow.cpp:144-146) fix: Enter an 'unloading' state that stops all dispatch (including MenuToggle and autosave) before cleanup. Save settings exactly once, before any cleanup. Cleanup must never mutate user settings: keep a separate runtime 'active' flag.
- [high] No Unload event when the FrameStage hook is missing. If game::Ready() is false or Source2Client002 is not found, oFrameStage stays null and Shutdown skips the wait. Unload is then never published: no settings save, patterns::Shutdown never runs, no cleanup. If the pattern worker std::thread was started from the menu (Verify/Generate), it is still joinable when the DLL's static destructors run at detach, which calls std::terminate (the game crashes) or runs code in a freed module. (src/hooks.cpp:262-268, :292; src/core/patterns.cpp:49, :297-298) fix: Run the cleanup path from MainThread (or from the Present thread) when the game-thread hook is absent or times out. Always join all owned threads in Shutdown, not via an event subscriber.
- [medium] Third person can stay on after unload. misc::Cleanup clears thirdWanted and capturing but NOT misc::thirdperson. hkCreateMove keeps running for 150 ms or more after the Unload event, and misc::OnCreateMove recomputes want=Active(thirdperson, thirdBind), which re-enables input+0x229 and calls pawn vfunc 0x9d8. When hooks are then disabled, nothing restores it. (src/misc.cpp:110-139, :235-246; src/hooks.cpp:294-296) fix: Make hooks check a global 'unloading' flag at entry and pass straight through. Run feature cleanups only after dispatch has stopped, or after the hooks are disabled but on the game thread.
- [medium] Features keep running between the Unload event and MH_DisableHook: aim, ragebot and movement (hkMerge/hkCreateMove), the Present subscribers and menu, and DrawArray tinting. The hooks have no unload gate; only hkFrameStage checks hooks::unload. (src/movement.cpp:412-474; src/hooks.cpp:146-195; src/hands.cpp:139-153) fix: Use a single atomic state machine (Running/Unloading/Unloaded) checked at the top of every detour.
- [medium] Unload relies on Sleep() instead of in-flight tracking. A thread still inside a detour or trampoline when MH_RemoveHook/MH_Uninitialize free the trampolines, or when FreeLibrary unmaps the DLL, will crash. Examples: hkPresent blocked in oPresent with vsync, a long CreateMove, a DrawArray worker. The same applies to hkWndProc after the WndProc is restored on another thread. (src/hooks.cpp:293-303; src/dllmain.cpp:19) fix: Give every detour an RAII in-flight counter. After disable, spin until all counters are 0, then remove hooks and free.
- [medium] The FrameStage MH_CreateHook result is ignored and a missing FrameStage hook is not logged, while CreateMove and Camera failures are logged. Vtable index 36 is hardcoded with no validation that the target lies inside client.dll. (src/hooks.cpp:264-268; src/core/wiring.cpp:253-256) fix: Use a hook registry that resolves each target (ref sig frame_stage_notify or vtable idx), checks module bounds, records status, and reports every hook uniformly.
- [low] Init depends entirely on D3D11. If D3D11CreateDeviceAndSwapChain fails (for example CS2 launched with -vulkan, or no HW device), hooks::Init returns false and the whole DLL unloads, even though the game hooks would work. (src/hooks.cpp:251-253) fix: Make the render backend optional: install game hooks independently and treat the overlay as a separate subsystem.
- [medium] The Present gate is a hardcoded feature list. When `!menu::NeedsFrame() && !misc::watermark && !misc::keybinds && !visuals::Any()` holds, Present returns before publishing Present or MenuToggle. A new overlay feature is invisible unless added here, and MenuToggle edges are only detected on frames that pass the gate. (src/hooks.cpp:172) fix: Ask the subscribers (e.g. a wants_frame() query on each overlay feature) or always build the ImGui frame; it is cheap.
- [medium] The CreateMove gate is a hardcoded feature list: `!movement::bhop && !autostrafe && !ragebot::enabled && !aim::Any() && !JumpCheck::enabled` skips cmd capture. New cmd features must edit the hook. (src/movement.cpp:460) fix: Always capture; features decide for themselves.
- [medium] Merge/cmd features are called directly from the hook, not through the event bus: aim::OnInput, ragebot::OnInput, JumpCheck::Run, Bunnyhop/Autostrafe/SubtickStrafe, ragebot::OnCmd plus vfunc 6. There is no per-feature timing or fault isolation from the bus; instead ad-hoc SEH disables things based on a debug 'stage' integer (stage 4 disables JumpCheck, other stages disable bhop and autostrafe, and a SafeCmd fault disables ragebot). (src/movement.cpp:337-433) fix: Use typed events: CreateMovePre(input), SubtickInput(input*, Input*, state), UserCmd(cmd), each with an ordered subscriber list.
- [low] The CreateMove event is published before the original CreateMove, wrapped in an extra SafeMisc __try whose handler (misc::thirdperson=false) is effectively dead code, because events::Invoke already catches all exceptions. (src/movement.cpp:443-453, :459) fix: Separate pre and post CreateMove events; drop the redundant SEH.
- [medium] Subscription and registration are spread out. Spread subscribes itself to CreateMove inside Spread::Init (and only if its InputHistory sigs resolved), not in wiring. ragebot, Hitboxes, Spread, NoSpread and JumpCheck have their own Register() while every other setting is registered centrally in wiring. Some features are namespaces with globals, others are structs with static members. (src/features/spread.cpp:193-194; src/core/wiring.cpp:28-98) fix: Use one feature interface (init, register_settings, on_create_move, on_frame_stage_notify, on_present, on_unload) registered in a single list.
- [medium] Init entry points come in three different shapes. Install() returns void and sets flags (movement::installed, misc::cameraHooked, hands::debug.hooked) and is called from hooks::Init. Init() returns bool and is logged in wiring (ray, Hitboxes, Spread, JumpCheck, damage). Load() is used for skins (synchronous) and items (std::thread). game::Ready() is checked twice in a row. (src/hooks.cpp:262-278; src/core/wiring.cpp:236-256) fix: Use a single init pipeline that records status per feature.
- [medium] The magic stage number 7 is duplicated in 6 subscribers, and each filters it itself. skins::OnFrameStage runs Sync() (taking a lock and possibly writing inventory.txt to disk) on EVERY stage. (src/core/wiring.cpp:104; src/glow.cpp:130; src/hands.cpp:220; src/skins.cpp:655-663, :302-305; src/ragebot.cpp:531; src/visibility.cpp:54) fix: Use named stage constants and dispatch only the relevant stage (or pre/post variants). Move file IO off the game thread.
- [low] PawnChanged is published but has no subscriber; LevelInit and LevelShutdown only log. skins, hands, glow, visibility and misc each re-resolve controller->pawn themselves via game::Handle(mem::At<uint32_t>(controller, off.playerPawn)). (src/core/wiring.cpp:100-126; src/misc.cpp:42-46; src/movement.cpp:82-83, :125-126; src/hands.cpp:222-224; src/visibility.cpp:57-59) fix: Build one per-tick local-player snapshot in the core and pass it in event payloads.
- [medium] Key binds toggle only if registered. misc::OnKey hardcodes 6 binds and registers the rest lazily when ui::Feature draws the widget, so a Toggle bind whose UI was never shown does not toggle. Toggle via mouse1/mouse2 never works, because L/R button down produces a vk only while capturing. INSERT is intercepted before capture, so it cannot be bound. Hold uses GetAsyncKeyState, so it fires while the game is unfocused. (src/misc.cpp:90-108; src/ui/ui.cpp:316-318; src/hooks.cpp:110-128) fix: Register every bind with the settings registry. Use a key-state table updated from WndProc (including L/R buttons) and require focus.
- [low] Settings Load runs on the render thread (menu button) while game-thread features read the same plain globals; colors (float[4]) can be read torn. settings::Save is called from three threads (menu, MenuToggle on the render thread, Unload on the main thread) without a lock, and writes are not atomic: a crash mid-write truncates the config. (src/menu.cpp:1007-1010; src/core/settings.cpp:72-153) fix: Queue load/save to one thread. Write to a temp file and rename.
- [low] Every save logs 'Config saved' (Success), and Autosave fires on every menu close, so a toast appears each time the menu is closed. (src/core/settings.cpp:102; src/core/wiring.cpp:128-132) fix: Use a silent autosave and only toast on explicit saves.
- [low] The settings format has no version or sections, ignores unknown keys silently, has a 256-char line cap, uses atoi/atof without validation and has no schema for ranges. skins keeps a separate inventory.txt with its own APPDATA path code (duplicate of settings PathString). (src/core/settings.cpp:46-58, :106-153; src/skins.cpp:45-63) fix: Use one config service (JSON or INI with sections) that owns all persistence paths.
- [low] Event bus limits: untyped (int, void*) payload; fixed 16 subscribers per type with silent drop; dedup by handler pointer (one handler cannot serve two names); stats are mutated from multiple threads without atomics; no priority or ordering control besides call order; nested Publish runs synchronously inside the FrameStage dispatch. (src/core/events.cpp:17-79) fix: Use typed events (template on payload struct), std::vector subscribers with priority, atomic stats, and fault isolation kept as is.
- [low] Present init and teardown: OMSetRenderTargets overrides the game's bound RT without restoring it (the ImGui DX11 backend does not back up OM RTs). The RTV is created with a null desc, so an sRGB/typeless backbuffer would need an explicit format (guess). The `initialized` flag, read in WndProc, and menu::open are plain bools shared across threads. (src/hooks.cpp:148-170, :191; src/menu.h:5) fix: Save and restore OM targets; use atomics for cross-thread flags.
- [low] WndProc restore blindly writes the saved oWndProc back. If something else subclassed the window after us (e.g. an overlay), its subclass is lost. (src/hooks.cpp:302-303) fix: Restore only if GetWindowLongPtr still returns hkWndProc; otherwise leave the subclass in place and stay resident or chain.
- [low] Unused or dead code: hooks::cleaned is only used for the wait loop; gravity and maxSpeedVar convars are looked up in movement and never used; misc::cameraHooked, movement::installed and hands::debug.hooked are ad-hoc status flags instead of a registry. (src/movement.cpp:45-46, :490-491; src/hooks.h:6-7) fix: Remove the dead code; report hook status through the hook registry.
- [low] docs/architecture.md says numeric offsets live only in game.cpp, but raw offsets also live in misc.cpp (0x229, 0x230, 0x234, 0x238, 0x688, 0x68C, 0x6A8, vfunc 0x9D8/8), movement.cpp (0x228, 0xB50, vfunc 6), hooks.cpp (vtable 36) and hands.cpp (0x70, 0x18, 0x50). (docs/architecture.md; src/misc.cpp:32-39,123-138; src/movement.cpp:388,416,419; src/hooks.cpp:267; src/hands.cpp:13-18) fix: Centralise all offsets and indices in the pattern/offset table (ref patterns.hpp style).
- [low] Partial movement install can leave a stray hook. If the GetUserCmd hook is created but Merge or CreateMove creation fails, the earlier hook is still enabled by MH_ALL_HOOKS (harmless only because activeThread stays 0) and nothing reports which part failed. (src/movement.cpp:493-501) fix: Make hook groups transactional: create all, else remove the created ones and report.

## Recode notes
KEEP (proven working game knowledge and mechanisms):
- Swapchain vtable fetch through a dummy D3D11 device (Present=8, ResizeBuffers=13) and the ResizeBuffers handling: release the RTV, invalidate and recreate the ImGui device objects.
- The WndProc subclass approach, plus the SDL3 relative-mouse-mode toggle that frees the cursor while the menu is open. SDL3 is the only working way to free the cursor here.
- Message swallowing while the menu is open, including WM_INPUT.
- Source2Client002 vtable 36 for FrameStageNotify, or the ref frame_stage_notify sig.
- CCSGOInput RTTI vtable index 5 for CreateMove; it matches the ref create_move "+28~".
- The CCSGOInput slot layout: block at +0x228, stride 0x928, subtick::Input layout per input.h, third-person flag at +0x229, cam angles at +0x230/+0x234/+0x238, view angles at +0x688/+0x68C.
- The CameraThink sv_cheats-flip trick and pawn vfunc 0x9D8/8.
- The GetUserCmd and MergeSubtick sigs. They are not in the ref list; the owner must decide whether subtick merge editing stays or movement moves to ref-style on_create_move(usercmd*).
- The scenesystem draw-array hook with the mesh layout (stride 0x70, object +0x18, color +0x50).
- Per-subscriber SEH isolation with timing stats and an Enable button. This is good debugging UX; keep it.
- The thread-safe log ring that feeds toasts.
- The name=value settings registry semantics (same key names, so existing configs still load).
- /EHa (needed so RAII unwinds under SEH).

NEW CORE MUST PROVIDE:
1. A hook registry. Each entry holds: name, target resolver (ref PATTERN string, vtable+index, or interface+index), detour, original, module-bounds check, status, and an in-flight counter (RAII guard in each detour). It needs uniform create/enable/disable/remove and reports through the existing patterns/status UI. Targets to cover: Present, ResizeBuffers, FrameStageNotify, CreateMove, (GetUserCmd/Merge or a replacement), CameraThink or OverrideView (ref override_view idx 15), DrawSceneObject/array, plus the optional ref level_initialization (idx 23) and level_shutdown sigs. These replace the polling Session tracker.
2. A typed event bus with payload structs:
   - FrameStage{stage, pre or post}
   - CreateMove{input, cmd, local snapshot}
   - SubtickInput{Input*, state}
   - Present{draw context}
   - Key{vk, down, consumed}
   - LevelInit and LevelShutdown
   - LocalPawnChanged
   - MenuToggle
   - Unload
   Ordered priorities, no fixed caps, atomic stats. All features, including aim, ragebot, movement, the menu render and the cursor update, go through it. No direct calls from hooks. The ref expects feature methods on_create_move(systems::input::usercmd*) and on_frame_stage_notify(), so a feature interface with these virtuals dispatched by the core matches the owner's reference.
3. Named frame-stage constants. Dispatch per stage so features do not filter the magic 7.
4. One per-tick local-player snapshot (controller, pawn, handle, alive, movetype, flags, velocity) instead of 7 re-resolutions.
5. A unified feature lifecycle: register_settings, init (returns status), on_unload (restore game state only, never touch user settings), wants_overlay(). Remove the hardcoded gates (hooks.cpp:172, movement.cpp:460) and the hardcoded bind list (misc.cpp:99).
6. Unload state machine:
   1. Running to Unloading: an atomic flag checked at the top of every detour, so they pass straight through and stop autosave and MenuToggle.
   2. Save settings once.
   3. Run cleanups on the game thread, with a fallback to MainThread on timeout or a missing hook.
   4. Disable hooks and wait for in-flight counters to reach 0.
   5. Remove hooks and MH_Uninitialize.
   6. Restore the WndProc only if it is still ours.
   7. Join ALL owned threads (patterns worker, icons, items loader) explicitly.
   8. ImGui and D3D release, then FreeLibraryAndExitThread.
7. A settings service: typed registry plus binds (all binds registered with settings, so toggles work without the UI), load/save on one thread, atomic file write, optional version and sections. It should also own the skins inventory path and patterns export path.
8. An input service: a key-state table from WndProc (including mouse buttons), focus-aware Hold, and the menu hotkey as a configurable bind.
9. The render backend as an optional module, so a D3D11 failure (for example -vulkan) does not abort the game hooks.

DROP:
- Polling-based LevelInit/LevelShutdown (once the ref hooks exist).
- The redundant SafeMisc SEH and the debug.stage-based fault routing in movement.
- Sleep-based unload synchronisation.
- Unused convars (sv_gravity and sv_maxspeed in movement) and status bools such as movement::installed and misc::cameraHooked (move them into the hook registry).
- Raw offsets scattered in misc, movement and hands: move them into the central offsets/pattern table (ref patterns.hpp style, PATTERN() resolved at the use site per game_addresses.hpp guidance).

VERIFY DURING THE RECODE (marked unknown above):
- What stage 7 actually is.
- The meaning of the int at CCSGOInput+0xB50.
- The purpose of CCSGOInput vfunc 6 and pawn vfunc 315.
- Whether the current GetUserCmd sig and the ref get_usercmd sig point to the same function.
