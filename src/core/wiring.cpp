#include "wiring.h"
#include "events.h"
#include "log.h"
#include "patterns.h"
#include "settings.h"
#include "../aim.h"
#include "../game.h"
#include "../items.h"
#include "../mem.h"
#include "../misc.h"
#include "../movement.h"
#include "../skins.h"
#include "../visuals.h"
#include "../hands.h"
#include "../glow.h"
#include "../ragebot.h"
#include "../damage.h"
#include "../visibility.h"
#include "ray.h"
#include "../features/hitbox.h"
#include "../features/nospread.h"
#include "../features/spread.h"
#include "../features/jumpcheck.h"
#include "../ui/ui.h"

namespace
{
    void RegisterSettings()
    {
        settings::Bool("movement.bhop", &movement::bhop);
        settings::Bool("movement.autostrafe", &movement::autostrafe);
        settings::Int("movement.strafe_mode", &movement::strafeMode);
        settings::Key("bind.bhop", &misc::bhopBind);
        settings::Key("bind.strafe", &misc::strafeBind);

        settings::Bool("camera.thirdperson", &misc::thirdperson);
        settings::Float("camera.distance", &misc::thirdDistance);
        settings::Key("bind.thirdperson", &misc::thirdBind);
        settings::Bool("overlay.watermark", &misc::watermark);
        settings::Bool("overlay.keybinds", &misc::keybinds);

        settings::Bool("aim.legit", &aim::legit);
        settings::Key("bind.legit", &aim::legitKey);
        settings::Float("aim.legit_fov", &aim::legitFov);
        settings::Float("aim.legit_smooth", &aim::legitSmooth);
        settings::Int("aim.legit_hitbox", &aim::legitHitbox);
        settings::Bool("aim.legit_rcs", &aim::legitRcs);
        settings::Bool("aim.trigger", &aim::trigger);
        settings::Key("bind.trigger", &aim::triggerKey);
        settings::Int("aim.trigger_delay", &aim::triggerDelay);
        settings::Bool("aim.trigger_head", &aim::triggerHead);
        settings::Bool("aim.trigger_neck", &aim::triggerNeck);
        settings::Bool("aim.trigger_chest", &aim::triggerChest);
        settings::Bool("aim.trigger_stomach", &aim::triggerStomach);
        settings::Bool("aim.trigger_arms", &aim::triggerArms);
        settings::Bool("aim.trigger_legs", &aim::triggerLegs);
        settings::Int("aim.trigger_min_damage", &aim::triggerMinDamage);
        settings::Bool("aim.teammates", &aim::teammates);

        settings::Bool("visuals.esp", &visuals::esp);
        settings::Bool("visuals.box", &visuals::box);
        settings::Bool("visuals.name", &visuals::name);
        settings::Bool("visuals.health", &visuals::health);
        settings::Bool("visuals.weapon", &visuals::weapon);
        settings::Bool("visuals.distance", &visuals::distance);
        settings::Bool("visuals.skeleton", &visuals::skeleton);
        settings::Bool("visuals.snaplines", &visuals::snaplines);
        settings::Bool("visuals.teammates", &visuals::teammates);
        settings::Color("visuals.visible_color", visuals::visibleColor);
        settings::Color("visuals.hidden_color", visuals::hiddenColor);
        settings::Color("visuals.team_color", visuals::teamColor);
        settings::Bool("visuals.glow", &glow::enabled);
        settings::Bool("visuals.glow_team", &glow::teammates);
        settings::Bool("visuals.glow_visibility", &glow::byVisibility);
        settings::Color("visuals.glow_color", glow::enemyColor);
        settings::Color("visuals.glow_hidden_color", glow::hiddenColor);
        settings::Color("visuals.glow_team_color", glow::teamColor);
        settings::Bool("visuals.fov_circle", &visuals::fovCircle);

        settings::Bool("skins.enabled", &skins::enabled);
        settings::Int("skins.paint_mode", &skins::paintMode);
        settings::Int("skins.language", &items::language);
        settings::Bool("skins.knife_animations", &skins::knifeAnimations);

        settings::Bool("hands.arms", &hands::arms);
        settings::Bool("hands.weapon", &hands::weapon);
        settings::Color("hands.arms_color", hands::armsColor);
        settings::Color("hands.weapon_color", hands::weaponColor);
        settings::Color("ui.accent", ui::accent);
        settings::Bool("ui.reduce_motion", &ui::reduceMotion);
        settings::Bool("patterns.loose_offsets", &patterns::looseOffsets);

        ragebot::Register();
        Hitboxes::Register();
        Spread::Register();
        NoSpread::Register();
        JumpCheck::Register();
    }

    void OnSessionStage(int stage, void*)
    {
        static void* lastController = nullptr;
        static void* lastPawn = nullptr;
        if (stage != 7 || !game::Ready())
            return;
        void* controller = game::LocalController();
        void* pawn = controller ? game::Handle(mem::At<uint32_t>(controller, game::off.playerPawn)) : nullptr;
        if (controller && !lastController)
            events::Publish(events::LevelInit, 0, controller);
        if (pawn != lastPawn)
            events::Publish(events::PawnChanged, pawn ? 1 : 0, pawn);
        if (!controller && lastController)
            events::Publish(events::LevelShutdown);
        lastController = controller;
        lastPawn = pawn;
    }

    void OnLevelInit(int, void*)
    {
        logs::Add(logs::Info, "Level loaded");
    }

    void OnLevelShutdown(int, void*)
    {
        logs::Add(logs::Info, "Level unloaded");
    }

    void OnMenuToggle(int open, void*)
    {
        if (!open)
            settings::Save();
    }

    void OnUnloadPatterns(int, void*)
    {
        patterns::Shutdown();
    }

    void OnSkinsStage(int stage, void*)
    {
        skins::OnFrameStage(stage);
    }

    void OnHandsStage(int stage, void*)
    {
        hands::OnFrameStage(stage);
    }

    void OnUnloadHands(int, void*)
    {
        hands::Cleanup();
    }

    void OnVisibilityStage(int stage, void*)
    {
        visibility::OnFrameStage(stage);
    }

    void OnRageStage(int stage, void*)
    {
        ragebot::OnFrameStage(stage);
    }

    void OnGlowStage(int stage, void*)
    {
        glow::OnFrameStage(stage);
    }

    void OnUnloadGlow(int, void*)
    {
        glow::Cleanup();
        visibility::Cleanup();
    }

    void OnCreateMove(int, void* input)
    {
        misc::OnCreateMove(input);
    }

    void OnPresentVisuals(int, void*)
    {
        visuals::Render();
    }

    void OnPresentHud(int, void*)
    {
        misc::RenderHud();
    }

    void OnUnloadMisc(int, void*)
    {
        misc::Cleanup();
    }

    void OnUnloadVisuals(int, void*)
    {
        visuals::Cleanup();
    }

    void OnUnloadSkins(int, void*)
    {
        skins::Cleanup();
    }

    void OnUnloadSettings(int, void*)
    {
        settings::Save();
    }
}

void wiring::Install()
{
    RegisterSettings();
    settings::Load();

    events::Subscribe(events::FrameStage, "Session tracker", OnSessionStage);
    events::Subscribe(events::FrameStage, "Skin changer", OnSkinsStage);
    events::Subscribe(events::FrameStage, "Visibility", OnVisibilityStage);
    events::Subscribe(events::FrameStage, "Glow", OnGlowStage);
    events::Subscribe(events::FrameStage, "Ragebot tracker", OnRageStage);
    events::Subscribe(events::FrameStage, "Hands", OnHandsStage);
    events::Subscribe(events::CreateMove, "Camera", OnCreateMove);
    events::Subscribe(events::Present, "ESP", OnPresentVisuals);
    events::Subscribe(events::Present, "Overlay", OnPresentHud);
    events::Subscribe(events::LevelInit, "Log", OnLevelInit);
    events::Subscribe(events::LevelShutdown, "Log", OnLevelShutdown);
    events::Subscribe(events::MenuToggle, "Autosave", OnMenuToggle);
    events::Subscribe(events::Unload, "Pattern worker", OnUnloadPatterns);
    events::Subscribe(events::Unload, "Settings", OnUnloadSettings);
    events::Subscribe(events::Unload, "Hands cleanup", OnUnloadHands);
    events::Subscribe(events::Unload, "Camera cleanup", OnUnloadMisc);
    events::Subscribe(events::Unload, "Visuals cleanup", OnUnloadVisuals);
    events::Subscribe(events::Unload, "Glow cleanup", OnUnloadGlow);
    events::Subscribe(events::Unload, "Skin cleanup", OnUnloadSkins);

    if (game::Ready() && !ray::Init())
        logs::Add(logs::Warning, "Ray trace not found, visibility uses radar");
    if (game::Ready())
    {
        if (!Hitboxes::Init())
            logs::Add(logs::Warning, "Hitboxes not found, aim uses bones");
        if (!Spread::Init())
            logs::Add(logs::Warning, "Spread seed not found, hitchance uses estimate");
        if (!JumpCheck::Init())
            logs::Add(logs::Warning, "Modern jump fields not found");
        if (!damage::Init())
            logs::Add(logs::Warning, "Weapon damage fields not found, min damage off");
    }
    if (!game::Ready())
        logs::Add(logs::Error, "Game data missing: %s", game::Error().c_str());
    else
        logs::Add(logs::Success, "Loaded. INSERT opens the menu");
    if (!movement::installed)
        logs::Add(logs::Warning, "CreateMove hook not found");
    if (!misc::cameraHooked)
        logs::Add(logs::Warning, "Camera hook not found");
}
