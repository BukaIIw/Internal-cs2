#include "features.h"
#include "changer/changer.h"
#include "combat/combat.h"
#include "misc/misc.h"
#include "movement/movement.h"
#include "visuals/visuals.h"
#include "../core/addresses.h"
#include "../core/events.h"
#include "../core/settings.h"
#include "../core/cstypes.h"
#include "../systems/systems.h"
#include "../ui/menu.h"
#include "imgui.h"

namespace
{
    using namespace systems;
    using events::type;
    namespace prio = events::priority;

    input::frame& frame_of(void* args)
    {
        return *static_cast<input::frame*>(args);
    }

    input::usercmd& cmd_of(void* args)
    {
        return *static_cast<input::usercmd*>(args);
    }

    int stage_of(void* args)
    {
        return static_cast<events::frame_stage_args*>(args)->stage;
    }
}

void features::initialize()
{
    changer::g_econ_item_system.initialize();
}

void features::register_all()
{
    events::subscribe(type::frame_stage, "local", [](void* a) { if (stage_of(a) == cstypes::frame_stage::update) g_local.update(); }, prio::first);
    events::subscribe(type::frame_stage, "entities", [](void* a) { if (stage_of(a) == cstypes::frame_stage::update) g_entities.update(settings::g_visuals.esp, settings::g_visuals.teammates); }, prio::systems);
    events::subscribe(type::frame_stage, "changer", [](void* a) { changer::on_frame_stage(stage_of(a)); }, prio::normal);
    events::subscribe(type::frame_stage, "rage", [](void* a) { combat::g_rage.on_frame_stage(stage_of(a)); }, prio::normal);
    events::subscribe(type::frame_stage, "esp", [](void* a) { visuals::g_esp.on_frame_stage(stage_of(a)); }, prio::normal);
    events::subscribe(type::frame_stage, "grenade prediction", [](void* a) { visuals::g_grenade.on_frame_stage(stage_of(a)); }, prio::normal);
    events::subscribe(type::frame_stage, "glow", [](void* a) { visuals::g_glow.on_frame_stage(stage_of(a)); }, prio::normal);

    events::subscribe(type::create_move, "view begin", [](void* a) {
        auto& f = frame_of(a);
        g_view.begin(f);
        movement::g_airstrafe.store_angles(f.view());
    }, prio::first);
    events::subscribe(type::create_move, "combat context", [](void*) { combat::g_shared.update(); }, prio::systems);
    events::subscribe(type::create_move, "jumpbug", [](void* a) { movement::g_jumpbug.on_create_move(frame_of(a)); }, prio::movement_pre);
    events::subscribe(type::create_move, "rage", [](void* a) { combat::g_rage.on_create_move(frame_of(a)); }, prio::combat);
    events::subscribe(type::create_move, "legit", [](void* a) { combat::g_legit.on_create_move(frame_of(a)); }, prio::combat + 1);
    events::subscribe(type::create_move, "trigger", [](void* a) { combat::g_trigger.on_create_move(frame_of(a)); }, prio::combat + 2);
    events::subscribe(type::create_move, "bhop", [](void* a) { movement::g_bhop.on_create_move(frame_of(a)); }, prio::movement);
    events::subscribe(type::create_move, "airstrafe", [](void* a) { movement::g_airstrafe.on_create_move(frame_of(a)); }, prio::movement + 1);
    events::subscribe(type::create_move, "subtick strafer", [](void* a) { movement::g_test_strafer.on_create_move(frame_of(a)); }, prio::movement + 2);
    events::subscribe(type::create_move, "quickstop", [](void* a) { movement::g_quickstop.on_create_move(frame_of(a)); }, prio::movement + 3);
    events::subscribe(type::create_move, "fastladder", [](void* a) { movement::g_fastladder.on_create_move(frame_of(a)); }, prio::movement + 4);
    events::subscribe(type::create_move, "view apply", [](void* a) { g_view.apply(frame_of(a)); }, prio::view);
    events::subscribe(type::create_move, "thirdperson", [](void*) { misc::g_thirdperson.on_create_move(); }, prio::normal);

    events::subscribe(type::create_move_post, "rage", [](void* a) { combat::g_rage.on_create_move_post(cmd_of(a)); }, prio::combat);
    events::subscribe(type::create_move_post, "view apply", [](void* a) { g_view.apply(cmd_of(a)); }, prio::view);
    events::subscribe(type::create_move_post, "doubletap", [](void* a) { combat::g_doubletap.on_create_move_post(cmd_of(a)); }, prio::view + 1);
    events::subscribe(type::create_move_post, "rage late", [](void* a) { combat::g_rage.on_create_move_late(cmd_of(a)); }, prio::view + 2);
    events::subscribe(type::create_move_post, "view end", [](void*) { g_view.end(); }, prio::last);

    events::subscribe(type::override_view, "thirdperson", [](void* a) {
        auto* args = static_cast<events::override_view_args*>(a);
        misc::g_thirdperson.on_override_view(args->client_mode, args->view_setup);
    }, prio::normal);

    events::subscribe(type::present, "esp", [](void*) { visuals::g_esp.on_present(ImGui::GetBackgroundDrawList()); }, prio::normal);
    events::subscribe(type::present, "grenade prediction", [](void*) { visuals::g_grenade.on_present(ImGui::GetBackgroundDrawList()); }, prio::normal + 1);
    events::subscribe(type::present, "overlay", [](void*) { misc::g_overlay.on_present(ImGui::GetForegroundDrawList()); }, prio::normal + 1);
    events::subscribe(type::present, "menu", [](void*) { menu::render(); }, prio::last);

    events::subscribe(type::level_init, "rage reset", [](void*) { combat::g_rage.reset(); combat::g_doubletap.reset(); }, prio::normal);
    events::subscribe(type::level_shutdown, "rage reset", [](void*) { combat::g_rage.reset(); }, prio::normal);
    events::subscribe(type::local_pawn_changed, "rage reset", [](void*) { combat::g_rage.reset(); }, prio::normal);
    events::subscribe(type::menu_toggle, "settings", [](void* a) { if (!*static_cast<bool*>(a)) settings::save(); }, prio::normal);

    events::subscribe(type::unload, "settings", [](void*) { settings::save(); }, prio::first);
    events::subscribe(type::unload, "changer", [](void*) { changer::on_unload(); }, prio::normal);
    events::subscribe(type::unload, "glow", [](void*) { visuals::g_glow.restore(); }, prio::normal);
    events::subscribe(type::unload, "thirdperson", [](void*) { misc::g_thirdperson.restore(); }, prio::normal);
    events::subscribe(type::unload, "addresses", [](void*) { addresses::shutdown(); }, prio::last);
}

bool features::wants_overlay()
{
    return menu::needs_frame() || misc::g_overlay.wants_frame() || visuals::any();
}
