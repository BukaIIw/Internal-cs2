#include "menu.h"
#include "menu_pages.h"
#include "ui.h"
#include "catalog.h"
#include "render.h"
#include "svg.h"
#include "icons_svg.h"
#include "../core/cstypes.h"
#include "../core/settings.h"
#include "../features/combat/combat.h"
#include "../features/movement/movement.h"
#include "../systems/local.h"
#include <algorithm>
#include <cmath>
#include <iterator>

namespace
{
    using ui::font_role;
    using ui::theme::token;
    namespace theme = ui::theme;

    struct page_info
    {
        const char* name;
        const char* subtitle;
        const char* icon;
        const char* const* tabs;
        int tab_count;
    };

    enum page_index : int
    {
        page_rage,
        page_legit,
        page_visuals,
        page_movement,
        page_skins,
        page_misc,
        page_settings,
        page_count
    };

    const char* const skin_tabs[] = { "Inventory", "Add", "Options" };
    const char* const settings_tabs[] = { "General", "Theme", "Events", "Runtime", "Patterns", "Log" };

    const page_info pages[page_count] = {
        { "Rage", "Automatic targeting and firing", icon::Target, nullptr, 0 },
        { "Legit", "Assisted aim and triggerbot", icon::Sliders, nullptr, 0 },
        { "Visuals", "Player overlays and highlights", icon::Eye, nullptr, 0 },
        { "Movement", "Jumping and strafing", icon::Run, nullptr, 0 },
        { "Skins", "Inventory and skin changer", icon::Box, skin_tabs, static_cast<int>(std::size(skin_tabs)) },
        { "Misc", "Camera and overlay", icon::List, nullptr, 0 },
        { "Settings", "Config, theme and diagnostics", icon::Gear, settings_tabs, static_cast<int>(std::size(settings_tabs)) },
    };

    const char* const hitbox_names[] = { "Head", "Neck", "Chest", "Stomach", "Pelvis", "Arms", "Legs", "Feet" };
    const std::uint32_t hitbox_bits[] = {
        settings::combat::hb_head,
        settings::combat::hb_neck,
        settings::combat::hb_chest,
        settings::combat::hb_stomach,
        settings::combat::hb_pelvis,
        settings::combat::hb_arms,
        settings::combat::hb_legs,
        settings::combat::hb_feet,
    };
    constexpr int hitbox_count = static_cast<int>(std::size(hitbox_names));

    const char* const autostop_names[] = { "Between shots", "Lethal", "In air", "Before landing" };
    const std::uint32_t autostop_bits[] = {
        settings::combat::as_between_shots,
        settings::combat::as_lethal,
        settings::combat::as_air,
        settings::combat::as_landing,
    };

    int g_page = page_skins;
    int g_tabs[page_count]{};
    float g_open_anim = 0.f;
    float g_page_anim = 1.f;
    int g_shown_key = -1;
    ImVec2 g_base_pos(-1.f, -1.f);

    const char* hitgroup_name(int group)
    {
        switch (group)
        {
        case 0:
            return "generic";
        case 1:
            return "head";
        case 2:
            return "chest";
        case 3:
            return "stomach";
        case 4:
        case 5:
            return "arm";
        case 6:
        case 7:
            return "leg";
        case 8:
            return "neck";
        default:
            return "-";
        }
    }

    bool hitbox_flags(const char* label, std::uint32_t* mask)
    {
        return ui::flags(label, mask, hitbox_names, hitbox_bits, hitbox_count);
    }

    void weapon_group_card(bool* override_flag)
    {
        using namespace settings::combat;
        if (settings::g_edit_follow_weapon)
        {
            const auto& ctx = features::combat::g_shared.ctx();
            if (ctx.valid && ctx.gun)
                settings::g_edit_group = ctx.group;
        }
        settings::g_edit_group = std::clamp(settings::g_edit_group, 0, static_cast<int>(wg_count) - 1);
        const char* names[wg_count]{};
        for (int i = 0; i < wg_count; ++i)
            names[i] = weapon_group_name(i);
        ui::begin_card("Weapon##group", icon::Sliders);
        ui::combo("Config##group", &settings::g_edit_group, names, wg_count);
        ui::toggle("Follow current weapon", &settings::g_edit_follow_weapon);
        if (override_flag && settings::g_edit_group != wg_global)
            ui::toggle("Override global", override_flag);
        ui::end_card();
    }

    void rage_page()
    {
        auto& global = settings::g_rage;
        auto& rage = settings::g_rage_groups[std::clamp(settings::g_edit_group, 0, static_cast<int>(settings::combat::wg_count) - 1)];
        if (!ui::begin_columns("##rage"))
            return;
        ui::next_column();
        weapon_group_card(&rage.override_global);
        ui::begin_card("Ragebot", icon::Target);
        ui::feature("Enabled##rage", &global.enabled, &global.key);
        ui::slider("Field of view##rage", &rage.fov, 1.f, 180.f, "%.0f\xC2\xB0");
        ui::slider("Hit chance##rage", &rage.hitchance, 0, 100, "%d%%");
        ui::slider("Minimum damage##rage", &rage.minimum_damage, 1, 120, "%d hp");
        ui::slider("Damage override##rage", &rage.damage_override, 1, 120, "%d hp");
        ui::key_button("Override key", &global.damage_override_key);
        ui::feature("Double tap", &global.doubletap, &global.doubletap_key);
        static const char* const doubletap_modes[] = { "Instant", "Alternate", "Split" };
        ui::combo("Double tap mode", &global.doubletap_mode, doubletap_modes, static_cast<int>(std::size(doubletap_modes)));
        static const char* const tick_sources[] = { "Client", "Server" };
        ui::combo("Tick source", &global.tick_source, tick_sources, static_cast<int>(std::size(tick_sources)));
        ui::end_card();

        ui::begin_card("Hitboxes##rage", icon::Target);
        hitbox_flags("Targets##rage_hitboxes", &rage.hitboxes);
        hitbox_flags("Multipoint##rage", &rage.multipoint);
        ui::slider("Head scale", &rage.head_scale, 0.f, 1.f, "%.2f");
        ui::slider("Body scale", &rage.body_scale, 0.f, 1.f, "%.2f");
        ui::slider("Safe zone##rage", &rage.safe_scale, 0.3f, 1.f, "%.2f");
        ui::toggle("Prefer body", &rage.prefer_body);
        ui::end_card();

        ui::next_column();
        ui::begin_card("Firing##rage", icon::Sliders);
        ui::toggle("Silent aim", &rage.silent);
        ui::toggle("Autofire", &rage.autofire);
        ui::toggle("Autowall", &rage.autowall);
        ui::toggle("Autostop", &rage.autostop);
        ui::flags("Autostop options", &rage.autostop_flags, autostop_names, autostop_bits, static_cast<int>(std::size(autostop_names)));
        ui::toggle("Autoscope", &rage.autoscope);
        ui::toggle("No spread", &rage.nospread);
        ui::toggle("Seed check##rage", &rage.seed_check);
        ui::end_card();

        ui::begin_card("Force shot##rage", icon::Sliders);
        ui::toggle("Enabled##force_shot", &rage.force_shot);
        ui::slider("Iterations##force_shot", &rage.force_shot_iterations, 1, 8, "%d");
        ui::slider("Minimum spread##force_shot", &rage.force_shot_min_spread, 0.f, 5.f, "%.2f");
        ui::end_card();

        ui::begin_card("Targets##rage_targets", icon::Eye);
        ui::toggle("Teammates##rage", &rage.teammates);
        ui::end_card();

        ui::begin_card("State##rage", icon::Bug);
        const auto debug = features::combat::g_rage.debug;
        ui::value("Target", "%s", debug.target ? "yes" : "no");
        ui::value("Hitgroup", "%s", hitgroup_name(debug.hitgroup));
        ui::value("Points", "%d", debug.points);
        ui::value("Damage", "%.0f", debug.damage);
        ui::value("Hit chance", "%.0f%%", debug.hitchance);
        ui::value("Fired", "%s", debug.fired ? "yes" : "no");
        ui::value("No spread", "%s", debug.nospread == 1 ? "ok" : debug.nospread == 2 ? "failed" : "-");
        ui::value("Seed", "%s", debug.seed == 1 ? "hit" : debug.seed == 0 ? "miss" : "-");
        ui::value("Seed tick delta", "%d", debug.seed_delta);
        ui::end_card();
        ui::end_columns();
    }

    void legit_page()
    {
        const int group = std::clamp(settings::g_edit_group, 0, static_cast<int>(settings::combat::wg_count) - 1);
        auto& global_legit = settings::g_legit;
        auto& global_trigger = settings::g_trigger;
        auto& legit = settings::g_legit_groups[group];
        auto& trigger = settings::g_trigger_groups[group];
        if (!ui::begin_columns("##legit"))
            return;
        ui::next_column();
        weapon_group_card(nullptr);
        if (group != settings::combat::wg_global)
        {
            ui::begin_card("Override##legit_group", icon::Sliders);
            ui::toggle("Override aimbot", &legit.override_global);
            ui::toggle("Override triggerbot", &trigger.override_global);
            ui::end_card();
        }
        static const char* const speed_modes[]{ "Linear", "Exponential" };
        ui::begin_card("Aimbot##legit", icon::Target);
        ui::feature("Enabled##legit", &global_legit.enabled, &global_legit.key);
        ui::slider("Field of view##legit", &legit.fov, 0.5f, 30.f, "%.1f\xC2\xB0");
        ui::slider("Speed##legit", &legit.speed, 1.f, 100.f, "%.0f%%");
        ui::combo("Speed mode##legit", &legit.speed_mode, speed_modes, 2);
        ui::slider("Randomization##legit", &legit.randomization, 0, 100, "%d%%");
        ui::key_button("Randomization key", &global_legit.random_key);
        hitbox_flags("Hitboxes##legit", &legit.hitboxes);
        ui::toggle("Visible only##legit", &legit.visible_only);
        ui::toggle("Teammates##legit", &legit.teammates);
        ui::end_card();

        ui::begin_card("Recoil##legit", icon::Sliders);
        ui::toggle("Recoil control", &legit.rcs);
        ui::slider("Recoil scale", &legit.rcs_scale, 0.f, 2.f, "%.2f");
        ui::end_card();

        ui::next_column();
        ui::begin_card("Triggerbot", icon::Target);
        ui::feature("Enabled##trigger", &global_trigger.enabled, &global_trigger.key);
        ui::slider("Delay##trigger", &trigger.delay, 0, 300, "%d ms");
        ui::slider("Minimum damage##trigger", &trigger.minimum_damage, 1, 100, "%d hp");
        ui::slider("Hit chance##trigger", &trigger.hitchance, 0, 100, "%d%%");
        ui::toggle("Seed check##trigger", &trigger.seed_check);
        ui::slider("Safe zone##trigger", &trigger.safe_scale, 0.3f, 1.f, "%.2f");
        hitbox_flags("Hitboxes##trigger", &trigger.hitboxes);
        ui::toggle("Teammates##trigger", &trigger.teammates);
        ui::end_card();

        ui::begin_card("Overlay##legit", icon::Eye);
        ui::toggle("Draw FOV circle", &settings::g_visuals.fov_circle);
        ui::toggle("Spread circle", &settings::g_visuals.spread_circle);
        ui::end_card();
        ui::end_columns();
    }

    void visuals_page()
    {
        auto& v = settings::g_visuals;
        if (!ui::begin_columns("##visuals"))
            return;
        ui::next_column();
        ui::begin_card("Players", icon::Eye);
        ui::toggle("Enabled##esp", &v.esp);
        ui::toggle("Box", &v.box);
        ui::toggle("Name", &v.name);
        ui::toggle("Health bar", &v.health);
        ui::toggle("Weapon", &v.weapon);
        ui::toggle("Distance", &v.distance);
        ui::toggle("Skeleton", &v.skeleton);
        ui::toggle("All bones", &v.skeleton_all);
        ui::toggle("Hitbox zones", &v.hitbox_zones);
        ui::toggle("Snaplines", &v.snaplines);
        ui::toggle("Teammates##esp", &v.teammates);
        ui::toggle("Grenade prediction", &v.grenade_prediction);
        ui::color_edit("Grenade path", v.grenade_color.data());
        ui::end_card();

        ui::next_column();
        ui::begin_card("Colors", icon::Palette);
        ui::color_edit("Visible##esp", v.visible.data());
        ui::color_edit("Hidden##esp", v.hidden.data());
        ui::color_edit("Teammates##esp_color", v.team.data());
        ui::end_card();

        ui::begin_card("Glow", icon::Eye);
        ui::toggle("Enabled##glow", &v.glow);
        ui::toggle("Teammates##glow", &v.glow_teammates);
        ui::toggle("Color by visibility##glow", &v.glow_by_visibility);
        ui::color_edit("Visible##glow", v.glow_visible.data());
        ui::color_edit("Behind wall##glow", v.glow_hidden.data());
        ui::color_edit("Teammates##glow_color", v.glow_team.data());
        ui::end_card();

        ui::begin_card("Viewmodel", icon::Palette);
        ui::toggle("Arms color", &v.hands_tint);
        ui::color_edit("Arms##tint", v.hands_color.data());
        ui::toggle("Weapon color", &v.weapon_tint);
        ui::color_edit("Weapon##tint", v.weapon_color.data());
        ui::end_card();
        ui::end_columns();
    }

    void strafe_options()
    {
        static const char* const modes[] = { "Directional", "Subtick" };
        ui::combo("Mode##strafe", &settings::g_movement.airstrafe_mode, modes, static_cast<int>(std::size(modes)));
        ui::toggle("Fully directional", &settings::g_movement.airstrafe_fully_directional);
    }

    void movement_page()
    {
        auto& mv = settings::g_movement;
        if (!ui::begin_columns("##movement"))
            return;
        ui::next_column();
        ui::begin_card("Movement##card", icon::Run);
        ui::feature("Bunnyhop", &mv.bhop, &mv.bhop_key);
        ui::feature("Air strafe", &mv.airstrafe, &mv.airstrafe_key, strafe_options);
        ui::feature("Jump bug", &mv.jumpbug, &mv.jumpbug_key);
        ui::toggle("Fast ladder", &mv.fastladder);
        ui::toggle("Quick stop", &mv.quickstop);
        ui::end_card();

        ui::next_column();
        ui::begin_card("State##movement", icon::Bug);
        const systems::local_player::data local = systems::g_local.get();
        if (!local.pawn || !local.is_alive)
            ui::note("Spawn in a match to see movement state.");
        else
        {
            const float speed = std::sqrt(local.velocity.x * local.velocity.x + local.velocity.y * local.velocity.y);
            ui::value("Speed", "%.0f u/s", speed);
            ui::value("On ground", "%s", (local.flags & cstypes::entity_flags::on_ground) ? "yes" : "no");
            ui::value("Jump bug", "%s", features::movement::g_jumpbug.active_this_tick() ? "active" : "idle");
            ui::value("Landing", "%.2f", features::movement::g_jumpbug.landing_fraction());
        }
        ui::end_card();
        ui::end_columns();
    }

    void misc_page()
    {
        auto& misc = settings::g_misc;
        if (!ui::begin_columns("##misc"))
            return;
        ui::next_column();
        ui::begin_card("Thirdperson", icon::Camera);
        ui::feature("Enabled##thirdperson", &misc.thirdperson, &misc.thirdperson_key);
        ui::slider("Distance##thirdperson", &misc.thirdperson_distance, 50.f, 300.f, "%.0f");
        ui::end_card();

        ui::next_column();
        ui::begin_card("Overlay##misc", icon::List);
        ui::toggle("Watermark", &misc.watermark);
        ui::toggle("Keybind list", &misc.keybinds);
        ui::toggle("Shot logs", &misc.shot_logs);
        ui::toggle("Shot log file", &misc.shot_file);
        ui::end_card();
        ui::end_columns();
    }

    void content()
    {
        const int tab = g_tabs[g_page];
        switch (g_page)
        {
        case page_rage:
            rage_page();
            break;
        case page_legit:
            legit_page();
            break;
        case page_visuals:
            visuals_page();
            break;
        case page_movement:
            movement_page();
            break;
        case page_skins:
            menu::pages::skins(tab);
            break;
        case page_misc:
            misc_page();
            break;
        default:
            menu::pages::settings(tab);
            break;
        }
    }

    void logo(ImDrawList* dl, ImVec2 pos, float size)
    {
        svg::Fill(dl, icon::LogoOuter, pos, size, theme::accent());
        svg::Fill(dl, icon::LogoInner, pos, size, theme::color(token::rail));
        svg::Fill(dl, icon::LogoNotch, pos, size, theme::color(token::rail));
        svg::Fill(dl, icon::LogoCore, pos, size, theme::accent());
    }

    void nav_item(ImDrawList* dl, int index, ImVec2 center)
    {
        const ui::metrics& mt = ui::m();
        const float s = mt.nav_item;
        ImGui::SetCursorScreenPos(ImVec2(center.x - s * 0.5f, center.y - s * 0.5f));
        ImGui::PushID(index);
        if (ImGui::InvisibleButton("##nav", ImVec2(s, s)))
            g_page = index;
        const bool hovered = ImGui::IsItemHovered();
        const float sel = ui::animate("##sel", g_page == index ? 1.f : 0.f, 16.f);
        const float hv = ui::animate("##hv", hovered ? 1.f : 0.f, 20.f);
        ImGui::PopID();
        const ImVec2 a(center.x - s * 0.5f, center.y - s * 0.5f), b(center.x + s * 0.5f, center.y + s * 0.5f);
        if (hv > 0.01f)
            dl->AddRectFilled(a, b, theme::color(token::control_hover, hv * (1.f - sel)), mt.nav_radius);
        if (sel > 0.01f)
        {
            dl->AddRectFilled(a, b, theme::accent(0.14f * sel), mt.nav_radius);
            const float bar = mt.nav_bar_height * sel;
            const float x = center.x - mt.rail * 0.5f;
            dl->AddRectFilled(ImVec2(x, center.y - bar * 0.5f), ImVec2(x + mt.nav_bar_width, center.y + bar * 0.5f), theme::accent(sel), mt.space_xxs);
        }
        const ImU32 col = theme::mix(theme::mix(theme::color(token::text_faint), theme::color(token::text), hv), theme::accent(), sel);
        svg::Stroke(dl, pages[index].icon, ImVec2(center.x - mt.icon_nav * 0.5f, center.y - mt.icon_nav * 0.5f), mt.icon_nav, col, mt.stroke_nav);
        if (hv <= 0.01f)
            return;
        ImDrawList* fg = ImGui::GetForegroundDrawList();
        const char* name = pages[index].name;
        const ImVec2 ts = ui::text_size(font_role::body, name);
        const float half = mt.tooltip_height * 0.5f;
        const ImVec2 ta(b.x + mt.tooltip_offset + (1.f - hv) * mt.tooltip_slide, center.y - half);
        const ImVec2 tb(ta.x + ts.x + mt.tooltip_pad * 2.f, center.y + half);
        ui::soft_shadow(fg, ImVec2(ta.x, ta.y + mt.space_xxs), ImVec2(tb.x, tb.y + mt.space_xxs), mt.radius_frame, mt.slider_glow, 0.4f * hv);
        render::Gradient(fg, ta, tb, theme::color(token::control_hover, hv), theme::color(token::surface, hv), mt.radius_frame);
        fg->AddRect(ta, tb, theme::color(token::border, hv), mt.radius_frame, 0, mt.hairline);
        ui::text(fg, font_role::body, ImVec2(ta.x + mt.tooltip_pad, center.y - ui::font_size(font_role::body) * 0.5f), theme::color(token::text, hv), name);
    }

    void frame()
    {
        const ui::metrics& mt = ui::m();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
        const ImVec2 we(wp.x + ws.x, wp.y + ws.y);
        ui::chrome(wp, ws);
        render::Gradient(dl, wp, we, theme::mix(theme::color(token::background), theme::color(token::surface), 0.45f), theme::color(token::background), mt.window_radius);
        dl->AddRectFilled(wp, ImVec2(wp.x + mt.rail, we.y), theme::color(token::rail), mt.window_radius, ImDrawFlags_RoundCornersLeft);
        dl->AddLine(ImVec2(wp.x + mt.rail, wp.y), ImVec2(wp.x + mt.rail, we.y), theme::color(token::border), mt.hairline);
        logo(dl, ImVec2(wp.x + mt.logo_x, wp.y + mt.logo_y), mt.logo);
        const float rail_center = wp.x + mt.rail * 0.5f;
        for (int i = 0; i < page_settings; ++i)
            nav_item(dl, i, ImVec2(rail_center, wp.y + mt.nav_top + i * mt.nav_step));
        nav_item(dl, page_settings, ImVec2(rail_center, we.y - mt.nav_bottom));

        const page_info& pg = pages[g_page];
        const float x0 = wp.x + mt.rail + mt.space_xl;
        render::Gradient(dl, ImVec2(wp.x + mt.rail + mt.hairline, wp.y + mt.hairline), ImVec2(we.x - mt.hairline, wp.y + mt.header), theme::accent(0.08f), theme::accent(0.f), 0.f, render::Horizontal);
        float tabs_width = 0.f;
        if (pg.tab_count)
        {
            tabs_width = mt.tab_width * static_cast<float>(pg.tab_count);
            ImGui::SetCursorScreenPos(ImVec2(we.x - mt.space_xl - tabs_width, wp.y + mt.tabs_y));
            ui::segmented("##tabs", &g_tabs[g_page], pg.tabs, pg.tab_count, tabs_width);
        }
        const float title_right = we.x - mt.space_xl - tabs_width - mt.space_md;
        dl->PushClipRect(ImVec2(x0, wp.y), ImVec2(std::fmax(x0, title_right), wp.y + mt.header), true);
        ui::text(dl, font_role::display, ImVec2(x0, wp.y + mt.title_y), theme::color(token::text), pg.name);
        ui::text(dl, font_role::compact, ImVec2(x0, wp.y + mt.subtitle_y), theme::color(token::text_dim), pg.subtitle);
        dl->PopClipRect();
        dl->AddLine(ImVec2(wp.x + mt.rail, wp.y + mt.header), ImVec2(we.x, wp.y + mt.header), theme::color(token::border), mt.hairline);

        const int key = g_page * 8 + g_tabs[g_page];
        if (key != g_shown_key)
        {
            g_shown_key = key;
            g_page_anim = 0.f;
        }
        g_page_anim = ui::approach(g_page_anim, 1.f, 7.f);
        const float e = ui::ease(g_page_anim);

        const float offset = (1.f - e) * mt.page_slide;
        ImGui::SetCursorScreenPos(ImVec2(wp.x + mt.rail + mt.hairline + offset, wp.y + mt.header + mt.hairline));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(mt.content_pad_x, mt.content_pad_y));
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * e);
        if (ImGui::BeginChild("##content", ImVec2(ws.x - mt.rail - mt.hairline * 2.f, ws.y - mt.header - mt.hairline * 2.f), ImGuiChildFlags_AlwaysUseWindowPadding))
            content();
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
    }
}

void menu::initialize()
{
    g_open_anim = 0.f;
    g_page_anim = 1.f;
    g_shown_key = -1;
    g_base_pos = ImVec2(-1.f, -1.f);
    ui::catalog::update();
}

bool menu::needs_frame()
{
    return open || g_open_anim > 0.001f || ui::toasts_active();
}

void menu::render()
{
    ui::theme::refresh();
    ui::catalog::update();
    if (!open && keys::capturing)
        keys::capturing = nullptr;
    g_open_anim = ui::approach(g_open_anim, open ? 1.f : 0.f, 7.f);
    ui::font_scope body(font_role::body);
    ui::toasts();
    if (g_open_anim <= 0.001f)
        return;

    const ui::metrics& mt = ui::m();
    const float e = ui::ease(g_open_anim);
    const ImVec2 size(mt.window_width, mt.window_height);
    const ImVec2 screen = ImGui::GetIO().DisplaySize;
    if (g_base_pos.x < 0.f && screen.x > 0.f && screen.y > 0.f)
        g_base_pos = ImVec2(std::fmax(0.f, std::floor((screen.x - size.x) * 0.5f)), std::fmax(0.f, std::floor((screen.y - size.y) * 0.5f)));
    if (g_base_pos.x >= 0.f)
        ImGui::SetNextWindowPos(ImVec2(g_base_pos.x, g_base_pos.y + (1.f - e) * mt.window_slide), g_open_anim < 1.f ? ImGuiCond_Always : ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, e);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground;
    if (!open)
        flags |= ImGuiWindowFlags_NoInputs;
    const bool visible = ImGui::Begin("##internal", nullptr, flags);
    ImGui::PopStyleVar();
    if (visible)
    {
        if (g_open_anim >= 1.f)
            g_base_pos = ImGui::GetWindowPos();
        frame();
    }
    ImGui::End();
    ImGui::PopStyleVar();
}
