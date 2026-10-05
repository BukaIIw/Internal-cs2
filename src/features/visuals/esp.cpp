#include "visuals.h"
#include "visuals_detail.h"
#include "../changer/changer.h"
#include "../../core/addresses.h"
#include "../../core/cstypes.h"
#include "../../core/patterns.h"
#include "../../core/schema.h"
#include "../../core/settings.h"
#include "../../systems/entities.h"
#include "../../systems/game_reads.h"
#include "../../systems/local.h"
#include "../../ui/render.h"
#include "../../items.h"
#include "imgui.h"
#include <Windows.h>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

namespace
{
    constexpr std::uint32_t bone_array_in_model_state = 0x80;
    constexpr std::int32_t max_bone_array_offset = 0x4000;
    constexpr float max_bone_distance = 200.f;
    constexpr float default_height = 72.f;
    constexpr float min_height = 30.f;
    constexpr float max_height = 90.f;
    constexpr float head_padding = 6.f;
    constexpr float units_to_meters = 0.0254f;
    constexpr float box_aspect = 0.45f;
    constexpr float max_fov_circle = 89.f;
    constexpr int max_players = 64;
    constexpr int max_skeleton_bones = 192;
    constexpr int min_skeleton_bones = 8;
    constexpr int model_slots = 16;
    constexpr int bone_name_length = 48;
    constexpr float max_segment = 40.f;
    constexpr float max_origin_drift = 128.f;
    constexpr std::uintptr_t utl_vector_data = 8;
    constexpr std::uintptr_t model_bases[] = { 0x8, 0x0, 0x10 };
    constexpr const char* excluded_bones[] = {
        "ik_", "_ik", "lean", "cam", "weapon", "root", "attach", "aim", "prop", "driver", "eye", "jaw", "lid", "brow",
        "cheek", "lip", "tongue", "nose", "mouth", "twist", "helper", "offset", "clip", "hold", "scale", "physics"
    };

    struct bone_pair
    {
        int a;
        int b;
    };

    constexpr bone_pair skeleton_pairs[] = {
        { 6, 5 }, { 5, 4 }, { 4, 2 }, { 2, 0 },
        { 5, 8 }, { 8, 9 }, { 9, 10 },
        { 5, 13 }, { 13, 14 }, { 14, 15 },
        { 0, 22 }, { 22, 23 }, { 23, 24 },
        { 0, 25 }, { 25, 26 }, { 26, 27 },
    };

    struct model_skeleton
    {
        std::uintptr_t model = 0;
        bool resolved = false;
        int pairs = 0;
        std::array<std::uint8_t, max_skeleton_bones * 2> pair{};
    };

    model_skeleton g_models[model_slots]{};
    int g_model_next = 0;

    struct esp_player
    {
        std::uintptr_t pawn = 0;
        math::vector3 origin{};
        float height = default_height;
        float distance = 0.f;
        int health = 0;
        bool team = false;
        bool visible = false;
        int pairs = 0;
        std::array<std::uint8_t, max_skeleton_bones * 2> pair{};
        char name[32]{};
        char weapon[64]{};
    };

    struct esp_snapshot
    {
        bool valid = false;
        math::view_matrix matrix{};
        int count = 0;
        std::array<esp_player, max_players> players{};
    };

    std::mutex g_mutex;
    esp_snapshot g_snapshot{};
    esp_snapshot g_building{};

    std::uint32_t bone_array_offset()
    {
        const std::uintptr_t at = PATTERN(patterns::skeleton_bone_array);
        if (at && systems::reads::readable(at + 3, sizeof(std::int32_t)))
        {
            const auto disp = memory::read<std::int32_t>(at + 3);
            if (disp > 0 && disp < max_bone_array_offset)
                return static_cast<std::uint32_t>(disp);
        }
        const std::uint32_t model_state = SCHEMA("CSkeletonInstance", "m_modelState"_hash);
        return model_state ? model_state + bone_array_in_model_state : 0;
    }

    std::uintptr_t model_of(std::uintptr_t pawn)
    {
        const std::uintptr_t node = systems::reads::scene_node(pawn);
        const std::uint32_t state = SCHEMA("CSkeletonInstance", "m_modelState"_hash);
        const std::uint32_t handle = SCHEMA("CModelState", "m_hModel"_hash);
        if (!node || !state || !handle)
            return 0;
        return systems::reads::pointer(systems::reads::pointer(node + state + handle));
    }

    bool excluded(const char* name)
    {
        char lower[bone_name_length]{};
        for (int i = 0; i < bone_name_length - 1 && name[i]; ++i)
            lower[i] = static_cast<char>(name[i] >= 'A' && name[i] <= 'Z' ? name[i] - 'A' + 'a' : name[i]);
        for (const char* token : excluded_bones)
            if (std::strstr(lower, token))
                return true;
        return false;
    }

    bool read_parents(std::uintptr_t vector, std::array<std::int16_t, max_skeleton_bones>& out, int& count)
    {
        if (!systems::reads::readable(vector, utl_vector_data + sizeof(std::uintptr_t)))
            return false;
        count = memory::read<int>(vector);
        const std::uintptr_t data = systems::reads::pointer(vector + utl_vector_data);
        if (count < min_skeleton_bones || count > max_skeleton_bones || !data || !systems::reads::readable(data, static_cast<std::size_t>(count) * sizeof(std::int16_t)))
            return false;
        for (int i = 0; i < count; ++i)
        {
            out[i] = memory::read<std::int16_t>(data + static_cast<std::uintptr_t>(i) * sizeof(std::int16_t));
            if (i == 0 ? out[i] >= 0 : (out[i] < -1 || out[i] >= i))
                return false;
        }
        return true;
    }

    bool read_kept(std::uintptr_t vector, int count, std::array<bool, max_skeleton_bones>& kept)
    {
        if (!systems::reads::readable(vector, utl_vector_data + sizeof(std::uintptr_t)) || memory::read<int>(vector) != count)
            return false;
        const std::uintptr_t data = systems::reads::pointer(vector + utl_vector_data);
        if (!data || !systems::reads::readable(data, static_cast<std::size_t>(count) * sizeof(std::uintptr_t)))
            return false;
        for (int i = 0; i < count; ++i)
        {
            const std::uintptr_t name = memory::read<std::uintptr_t>(data + static_cast<std::uintptr_t>(i) * sizeof(std::uintptr_t));
            if (!name || !systems::reads::readable(name, 1))
                return false;
            const std::string text = memory::read_string(name, bone_name_length - 1);
            if (text.empty())
                return false;
            kept[i] = !excluded(text.c_str());
        }
        return true;
    }

    void resolve_skeleton(model_skeleton& out)
    {
        out.resolved = true;
        out.pairs = 0;
        const std::uint32_t skeleton = SCHEMA("PermModelData_t", "m_modelSkeleton"_hash);
        const std::uint32_t parents = SCHEMA("ModelSkeletonData_t", "m_nParent"_hash);
        const std::uint32_t names = SCHEMA("ModelSkeletonData_t", "m_boneName"_hash);
        if (!skeleton || !parents)
            return;
        for (const std::uintptr_t base : model_bases)
        {
            std::array<std::int16_t, max_skeleton_bones> parent{};
            int count = 0;
            if (!read_parents(out.model + base + skeleton + parents, parent, count))
                continue;
            std::array<bool, max_skeleton_bones> kept{};
            kept.fill(true);
            if (!names || !read_kept(out.model + base + skeleton + names, count, kept))
                kept.fill(true);
            for (int i = 1; i < count && out.pairs < max_skeleton_bones; ++i)
            {
                if (!kept[i])
                    continue;
                int a = parent[i];
                while (a >= 0 && !kept[a])
                    a = parent[a];
                if (a < 0)
                    continue;
                out.pair[out.pairs * 2] = static_cast<std::uint8_t>(a);
                out.pair[out.pairs * 2 + 1] = static_cast<std::uint8_t>(i);
                ++out.pairs;
            }
            return;
        }
    }

    const model_skeleton* skeleton_of(std::uintptr_t model)
    {
        if (!model)
            return nullptr;
        for (const model_skeleton& m : g_models)
            if (m.model == model && m.resolved)
                return m.pairs > 0 ? &m : nullptr;
        model_skeleton& slot = g_models[g_model_next];
        g_model_next = (g_model_next + 1) % model_slots;
        slot = {};
        slot.model = model;
        resolve_skeleton(slot);
        return slot.pairs > 0 ? &slot : nullptr;
    }

    void fill_skeleton(esp_player& e, std::uintptr_t pawn)
    {
        e.pairs = 0;
        if (const model_skeleton* m = skeleton_of(model_of(pawn)))
        {
            e.pairs = m->pairs;
            e.pair = m->pair;
            return;
        }
        for (const auto& p : skeleton_pairs)
        {
            e.pair[e.pairs * 2] = static_cast<std::uint8_t>(p.a);
            e.pair[e.pairs * 2 + 1] = static_cast<std::uint8_t>(p.b);
            ++e.pairs;
        }
    }

    struct live_player
    {
        math::vector3 origin{};
        std::uintptr_t bones = 0;
    };

    bool read_live(std::uintptr_t pawn, const math::vector3& reference, live_player& out)
    {
        math::vector3 origin{};
        if (!systems::reads::abs_origin(pawn, origin) || origin.distance(reference) > max_origin_drift)
            return false;
        out.origin = origin;
        const std::uintptr_t node = systems::reads::scene_node(pawn);
        const std::uint32_t offset = bone_array_offset();
        out.bones = node && offset ? systems::reads::pointer(node + offset) : 0;
        return true;
    }

    bool read_live_guarded(std::uintptr_t pawn, const math::vector3& reference, live_player& out)
    {
        __try
        {
            return read_live(pawn, reference, out);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool read_bone(std::uintptr_t bones, int index, const math::vector3& origin, math::vector3& out)
    {
        const std::uintptr_t at = bones + static_cast<std::uintptr_t>(index) * sizeof(math::bone);
        if (!systems::reads::readable(at, sizeof(math::vector3)))
            return false;
        out = memory::read<math::vector3>(at);
        return systems::reads::sane(out, systems::reads::world_limit) && (out - origin).length_sqr() <= max_bone_distance * max_bone_distance;
    }

    void weapon_name(std::uint16_t def, char* out, std::size_t size)
    {
        out[0] = '\0';
        if (!def)
            return;
        const auto& econ = features::changer::g_econ_item_system;
        if (econ.ready())
        {
            if (const auto* item = econ.find_def(static_cast<std::int16_t>(def)))
            {
                const std::string& name = item->localized_name.empty() ? item->name : item->localized_name;
                if (!name.empty())
                {
                    std::snprintf(out, size, "%s", name.c_str());
                    return;
                }
            }
        }
        if (items::state.load() == 1)
            if (const items::Item* item = items::Find(def))
                if (const char* name = items::Name(item->name); name && *name)
                    std::snprintf(out, size, "%s", name);
    }

    bool read_view_matrix(math::view_matrix& out)
    {
        const std::uintptr_t matrix = addresses::globals::view_matrix();
        if (!systems::reads::readable(matrix, sizeof(math::view_matrix)))
            return false;
        std::memcpy(&out, reinterpret_cast<const void*>(matrix), sizeof(math::view_matrix));
        for (const auto& row : out.m)
            for (const float v : row)
                if (!std::isfinite(v))
                    return false;
        return true;
    }

    void build_snapshot(esp_snapshot& snap)
    {
        snap.valid = false;
        snap.count = 0;

        const auto& cfg = settings::g_visuals;
        if (!cfg.esp && !cfg.fov_circle)
            return;

        const auto local = systems::g_local.get();
        if (!local.controller || !read_view_matrix(snap.matrix))
            return;
        snap.valid = true;

        if (!cfg.esp)
            return;

        const auto players = systems::g_entities.players();
        for (const auto& p : players)
        {
            if (snap.count >= max_players)
                break;
            if (!p.valid || !p.alive || p.dormant || !p.pawn || p.health <= 0)
                continue;
            if (p.pawn == local.pawn || p.index == local.index)
                continue;
            const bool team = !p.enemy;
            if (team && !cfg.teammates)
                continue;
            if (!systems::reads::sane(p.origin, systems::reads::world_limit))
                continue;

            auto& e = snap.players[snap.count];
            e.pawn = p.pawn;
            e.origin = p.origin;
            e.height = std::isfinite(p.maxs.z) && p.maxs.z > min_height && p.maxs.z < max_height ? p.maxs.z : default_height;
            e.distance = (p.origin - local.eye).length() * units_to_meters;
            e.health = p.health > 100 ? 100 : p.health;
            e.team = team;
            e.visible = p.visible;
            if (cfg.skeleton)
                fill_skeleton(e, p.pawn);
            else
                e.pairs = 0;
            std::memcpy(e.name, p.name, sizeof(e.name));
            e.name[sizeof(e.name) - 1] = '\0';
            if (cfg.weapon)
                weapon_name(p.weapon_def, e.weapon, sizeof(e.weapon));
            else
                e.weapon[0] = '\0';
            ++snap.count;
        }
    }

    ImU32 to_u32(const settings::color& c)
    {
        return static_cast<ImU32>(c.abgr());
    }

    bool project(const math::view_matrix& matrix, const math::vector3& world, const ImVec2& screen, ImVec2& out)
    {
        math::vector2 p{};
        if (!math::helpers::world_to_screen(matrix, world, screen.x, screen.y, p))
            return false;
        out = ImVec2(p.x, p.y);
        return true;
    }

    void outlined_text(ImDrawList* draw, ImVec2 pos, ImU32 color, const char* text, bool center)
    {
        ImFont* font = ImGui::GetFont();
        const float size = ImGui::GetFontSize();
        if (!font || !text || !*text)
            return;
        if (center)
            pos.x -= font->CalcTextSizeA(size, FLT_MAX, 0.f, text).x * 0.5f;
        render::Text(draw, font, size, ImVec2(pos.x + 1.f, pos.y + 1.f), IM_COL32(0, 0, 0, 200), text);
        render::Text(draw, font, size, pos, color, text);
    }

    void draw_fov_circle(ImDrawList* draw, const ImVec2& screen)
    {
        const auto& rage = settings::g_rage;
        const auto& legit = settings::g_legit;
        if (!legit.enabled && !rage.enabled)
            return;
        const float fov = legit.enabled ? legit.fov : rage.fov;
        if (!std::isfinite(fov) || fov <= 0.f || fov >= max_fov_circle)
            return;
        const float aspect = screen.x / screen.y;
        const float radius = std::tan(math::deg2rad(fov)) / (aspect * 0.75f) * screen.x * 0.5f;
        if (radius > 0.f && radius < screen.x)
            draw->AddCircle(ImVec2(screen.x * 0.5f, screen.y * 0.5f), radius, IM_COL32(255, 255, 255, 90), 64);
    }

    void draw_player(ImDrawList* draw, const esp_snapshot& snap, const esp_player& e, const ImVec2& screen)
    {
        const auto& cfg = settings::g_visuals;
        live_player live{};
        if (!read_live_guarded(e.pawn, e.origin, live))
            live.origin = e.origin;
        ImVec2 feet, head;
        if (!project(snap.matrix, live.origin, screen, feet))
            return;
        if (!project(snap.matrix, math::vector3{ live.origin.x, live.origin.y, live.origin.z + e.height + head_padding }, screen, head))
            return;
        const float h = feet.y - head.y;
        if (h < 4.f || h > 4000.f)
            return;
        const float w = h * box_aspect;
        const ImVec2 a(feet.x - w * 0.5f, head.y), b(feet.x + w * 0.5f, feet.y);
        const ImU32 color = to_u32(e.team ? cfg.team : e.visible ? cfg.visible : cfg.hidden);
        const ImU32 shadow = IM_COL32(0, 0, 0, 180);
        const float line = ImGui::GetFontSize();

        if (cfg.box)
        {
            render::Border(draw, ImVec2(a.x - 1.f, a.y - 1.f), ImVec2(b.x + 1.f, b.y + 1.f), shadow);
            render::Border(draw, ImVec2(a.x + 1.f, a.y + 1.f), ImVec2(b.x - 1.f, b.y - 1.f), shadow);
            render::Border(draw, a, b, color);
        }

        if (cfg.health)
        {
            const int hp = e.health < 0 ? 0 : e.health;
            const float frac = static_cast<float>(hp) / 100.f;
            const ImVec2 ba(a.x - 6.f, a.y), bb(a.x - 3.f, b.y);
            render::Rect(draw, ImVec2(ba.x - 1.f, ba.y - 1.f), ImVec2(bb.x + 1.f, bb.y + 1.f), shadow);
            const ImU32 health_color = IM_COL32(static_cast<int>(255.f * (1.f - frac)), static_cast<int>(255.f * frac), 40, 255);
            const float top = bb.y - (bb.y - ba.y) * frac;
            render::Rect(draw, ImVec2(ba.x, top), bb, health_color);
            if (hp < 100)
            {
                char t[8];
                std::snprintf(t, sizeof(t), "%d", hp);
                outlined_text(draw, ImVec2(ba.x - 2.f, top - line * 0.5f), IM_COL32(255, 255, 255, 255), t, true);
            }
        }

        if (cfg.name && e.name[0])
            outlined_text(draw, ImVec2(feet.x, a.y - line - 2.f), IM_COL32(255, 255, 255, 255), e.name, true);

        float below = b.y + 2.f;
        if (cfg.weapon && e.weapon[0])
        {
            outlined_text(draw, ImVec2(feet.x, below), IM_COL32(220, 220, 220, 255), e.weapon, true);
            below += line;
        }

        if (cfg.distance)
        {
            char t[16];
            std::snprintf(t, sizeof(t), "%.0fm", e.distance);
            outlined_text(draw, ImVec2(feet.x, below), IM_COL32(200, 200, 200, 255), t, true);
        }

        if (cfg.skeleton && live.bones)
            for (int i = 0; i < e.pairs; ++i)
            {
                math::vector3 wa{};
                math::vector3 wb{};
                if (!read_bone(live.bones, e.pair[i * 2], live.origin, wa) || !read_bone(live.bones, e.pair[i * 2 + 1], live.origin, wb))
                    continue;
                if ((wa - wb).length_sqr() > max_segment * max_segment)
                    continue;
                ImVec2 sa, sb;
                if (project(snap.matrix, wa, screen, sa) && project(snap.matrix, wb, screen, sb))
                    draw->AddLine(sa, sb, color, 1.2f);
            }

        if (cfg.snaplines)
            draw->AddLine(ImVec2(screen.x * 0.5f, screen.y), feet, color);
    }
}

namespace features::visuals
{
    void esp::on_frame_stage(int stage)
    {
        if (stage != cstypes::frame_stage::update)
            return;

        detail::update_chams_targets();

        build_snapshot(g_building);
        std::lock_guard lock(g_mutex);
        g_snapshot.valid = g_building.valid;
        g_snapshot.matrix = g_building.matrix;
        g_snapshot.count = g_building.count;
        for (int i = 0; i < g_building.count; ++i)
            g_snapshot.players[i] = g_building.players[i];
    }

    void esp::on_present(ImDrawList* draw)
    {
        if (!draw || !any())
            return;

        static esp_snapshot view{};
        {
            std::lock_guard lock(g_mutex);
            view.valid = g_snapshot.valid;
            view.matrix = g_snapshot.matrix;
            view.count = g_snapshot.count;
            for (int i = 0; i < g_snapshot.count; ++i)
                view.players[i] = g_snapshot.players[i];
        }
        if (!view.valid)
            return;
        math::view_matrix live{};
        if (read_view_matrix(live))
            view.matrix = live;

        const ImVec2 screen = ImGui::GetIO().DisplaySize;
        if (screen.x < 1.f || screen.y < 1.f)
            return;

        if (settings::g_visuals.fov_circle)
            draw_fov_circle(draw, screen);

        if (!settings::g_visuals.esp)
            return;

        for (int i = 0; i < view.count; ++i)
            draw_player(draw, view, view.players[i], screen);
    }
}
