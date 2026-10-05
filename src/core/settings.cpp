#include "settings.h"
#include "log.h"
#include <Windows.h>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace
{
    enum class kind
    {
        boolean,
        integer,
        bits,
        real,
        colour,
        binding
    };

    struct variable
    {
        const char* key;
        kind type;
        void* ptr;
    };

    constexpr int sticker_slots = 5;
    constexpr int max_bind_mode = static_cast<int>(keys::mode::off);
    constexpr std::size_t max_file_size = 4u << 20;

    std::vector<variable> g_variables;
    std::deque<std::string> g_key_storage;

    const char* group_key(const char* prefix, const char* name)
    {
        if (!prefix)
            return name;
        g_key_storage.push_back(std::string(prefix) + name);
        return g_key_storage.back().c_str();
    }

    constexpr const char* group_prefixes[settings::combat::wg_count] = {
        nullptr, "w.pistol.", "w.heavy_pistol.", "w.smg.", "w.rifle.", "w.shotgun.", "w.scout.", "w.awp.", "w.auto.", "w.machinegun."
    };
    std::mutex g_registry_mutex;
    std::mutex g_io_mutex;

    void add(const char* key, kind type, void* ptr)
    {
        for (auto& v : g_variables)
            if (!std::strcmp(v.key, key))
            {
                v.type = type;
                v.ptr = ptr;
                return;
            }
        g_variables.push_back({ key, type, ptr });
    }

    void add(const char* key, bool& value) { add(key, kind::boolean, &value); }
    void add(const char* key, int& value) { add(key, kind::integer, &value); }
    void add(const char* key, std::uint32_t& value) { add(key, kind::bits, &value); }
    void add(const char* key, float& value) { add(key, kind::real, &value); }
    void add(const char* key, settings::color& value) { add(key, kind::colour, &value); }

    void add(const char* key, keys::bind& value, const char* display)
    {
        add(key, kind::binding, &value);
        if (display)
            keys::register_bind(&value, display);
    }

    std::wstring directory()
    {
        static const std::wstring value = [] {
            wchar_t buffer[MAX_PATH]{};
            const DWORD length = GetEnvironmentVariableW(L"APPDATA", buffer, MAX_PATH);
            std::wstring dir = length > 0 && length < MAX_PATH ? std::wstring(buffer, length) : std::wstring(L".");
            dir += L"\\Internal-cs2";
            CreateDirectoryW(dir.c_str(), nullptr);
            return dir;
        }();
        return value;
    }

    std::wstring settings_file()
    {
        return directory() + L"\\settings.ini";
    }

    std::wstring inventory_file()
    {
        return directory() + L"\\inventory.ini";
    }

    std::string narrow(const std::wstring& text)
    {
        if (text.empty())
            return {};
        const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        if (size <= 0)
            return {};
        std::string out(static_cast<std::size_t>(size), '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
        return out;
    }

    bool write_atomic(const std::wstring& path, const std::string& content)
    {
        const std::wstring temporary = path + L".tmp";
        FILE* file = nullptr;
        if (_wfopen_s(&file, temporary.c_str(), L"wb") || !file)
            return false;
        const bool written = std::fwrite(content.data(), 1, content.size(), file) == content.size() && std::fflush(file) == 0;
        const bool closed = std::fclose(file) == 0;
        if (!written || !closed)
        {
            DeleteFileW(temporary.c_str());
            return false;
        }
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            DeleteFileW(temporary.c_str());
            return false;
        }
        return true;
    }

    bool read_file(const std::wstring& path, std::string& out)
    {
        FILE* file = nullptr;
        if (_wfopen_s(&file, path.c_str(), L"rb") || !file)
            return false;
        out.clear();
        char buffer[4096];
        std::size_t read = 0;
        while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0)
        {
            out.append(buffer, read);
            if (out.size() > max_file_size)
                break;
        }
        std::fclose(file);
        return out.size() <= max_file_size;
    }

    std::string_view trim(std::string_view s)
    {
        while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r'))
            s.remove_prefix(1);
        while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r'))
            s.remove_suffix(1);
        return s;
    }

    template <typename F>
    void for_each_line(std::string_view text, F&& fn)
    {
        while (!text.empty())
        {
            const auto end = text.find('\n');
            const auto line = end == std::string_view::npos ? text : text.substr(0, end);
            fn(line);
            if (end == std::string_view::npos)
                break;
            text.remove_prefix(end + 1);
        }
    }

    bool parse(std::string_view s, int& out)
    {
        s = trim(s);
        int value = 0;
        const auto r = std::from_chars(s.data(), s.data() + s.size(), value);
        if (r.ec != std::errc{} || r.ptr != s.data() + s.size())
            return false;
        out = value;
        return true;
    }

    bool parse(std::string_view s, std::uint32_t& out)
    {
        s = trim(s);
        std::uint32_t value = 0;
        const auto r = std::from_chars(s.data(), s.data() + s.size(), value);
        if (r.ec != std::errc{} || r.ptr != s.data() + s.size())
            return false;
        out = value;
        return true;
    }

    bool parse(std::string_view s, float& out)
    {
        s = trim(s);
        float value = 0.f;
        const auto r = std::from_chars(s.data(), s.data() + s.size(), value);
        if (r.ec != std::errc{} || r.ptr != s.data() + s.size() || !std::isfinite(value))
            return false;
        out = value;
        return true;
    }

    std::vector<std::string_view> split(std::string_view s, char separator)
    {
        std::vector<std::string_view> parts;
        while (true)
        {
            const auto at = s.find(separator);
            parts.push_back(s.substr(0, at));
            if (at == std::string_view::npos)
                break;
            s.remove_prefix(at + 1);
        }
        return parts;
    }

    std::string_view next_token(std::string_view& s)
    {
        while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
            s.remove_prefix(1);
        const auto end = s.find_first_of(" \t");
        const auto token = s.substr(0, end);
        s.remove_prefix(end == std::string_view::npos ? s.size() : end);
        return token;
    }

    void append(std::string& out, int value)
    {
        char buffer[16];
        const auto r = std::to_chars(buffer, buffer + sizeof(buffer), value);
        out.append(buffer, r.ptr);
    }

    void append(std::string& out, std::uint32_t value)
    {
        char buffer[16];
        const auto r = std::to_chars(buffer, buffer + sizeof(buffer), value);
        out.append(buffer, r.ptr);
    }

    void append(std::string& out, float value)
    {
        if (!std::isfinite(value))
            value = 0.f;
        char buffer[32];
        const auto r = std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::general, 7);
        out.append(buffer, r.ptr);
    }

    std::string serialize_settings()
    {
        std::string out;
        std::lock_guard lock(g_registry_mutex);
        for (const auto& v : g_variables)
        {
            out += v.key;
            out += '=';
            switch (v.type)
            {
            case kind::boolean:
                out += *static_cast<const bool*>(v.ptr) ? '1' : '0';
                break;
            case kind::integer:
                append(out, *static_cast<const int*>(v.ptr));
                break;
            case kind::bits:
                append(out, *static_cast<const std::uint32_t*>(v.ptr));
                break;
            case kind::real:
                append(out, *static_cast<const float*>(v.ptr));
                break;
            case kind::colour:
            {
                const auto* c = static_cast<const settings::color*>(v.ptr)->data();
                for (int i = 0; i < 4; ++i)
                {
                    if (i)
                        out += ',';
                    append(out, c[i]);
                }
                break;
            }
            case kind::binding:
            {
                const auto* b = static_cast<const keys::bind*>(v.ptr);
                append(out, b->key);
                out += ',';
                append(out, static_cast<int>(b->type));
                break;
            }
            }
            out += '\n';
        }
        return out;
    }

    void apply_setting(const variable& v, std::string_view value)
    {
        switch (v.type)
        {
        case kind::boolean:
        {
            int parsed = 0;
            if (parse(value, parsed))
                *static_cast<bool*>(v.ptr) = parsed != 0;
            break;
        }
        case kind::integer:
            parse(value, *static_cast<int*>(v.ptr));
            break;
        case kind::bits:
            parse(value, *static_cast<std::uint32_t*>(v.ptr));
            break;
        case kind::real:
            parse(value, *static_cast<float*>(v.ptr));
            break;
        case kind::colour:
        {
            const auto parts = split(value, ',');
            if (parts.size() != 4)
                break;
            float c[4]{};
            for (int i = 0; i < 4; ++i)
                if (!parse(parts[i], c[i]))
                    return;
            auto* target = static_cast<settings::color*>(v.ptr)->data();
            for (int i = 0; i < 4; ++i)
                target[i] = std::clamp(c[i], 0.f, 1.f);
            break;
        }
        case kind::binding:
        {
            const auto parts = split(value, ',');
            int key = 0, mode = 0;
            if (parts.size() != 2 || !parse(parts[0], key) || !parse(parts[1], mode))
                break;
            auto* b = static_cast<keys::bind*>(v.ptr);
            b->key = key > 0 && key < 256 ? key : 0;
            b->type = static_cast<keys::mode>(std::clamp(mode, 0, max_bind_mode));
            b->toggled = false;
            break;
        }
        }
    }

    void deserialize_settings(std::string_view text)
    {
        std::lock_guard lock(g_registry_mutex);
        for_each_line(text, [](std::string_view line) {
            const auto eq = line.find('=');
            if (eq == std::string_view::npos)
                return;
            const auto key = trim(line.substr(0, eq));
            const auto value = trim(line.substr(eq + 1));
            for (const auto& v : g_variables)
                if (key == v.key)
                {
                    apply_setting(v, value);
                    return;
                }
        });
    }

    std::string sanitize_nametag(const std::string& text)
    {
        std::string out = text;
        for (auto& c : out)
            if (c == '|' || c == '\n' || c == '\r' || c == '\t')
                c = ' ';
        const auto view = trim(out);
        return std::string(view);
    }

    std::string serialize_inventory()
    {
        std::lock_guard lock(settings::g_changer_mutex);
        const auto& changer = settings::g_changer;
        std::string out = "agents ";
        append(out, static_cast<int>(changer.agents.ct_def));
        out += ' ';
        append(out, static_cast<int>(changer.agents.t_def));
        out += '\n';
        for (const auto& e : changer.inventory)
        {
            append(out, e.uid);
            out += ' ';
            append(out, static_cast<int>(e.def_index));
            out += ' ';
            append(out, e.skin.paint_kit_id);
            out += ' ';
            append(out, e.skin.seed);
            out += ' ';
            append(out, e.skin.wear);
            out += ' ';
            append(out, e.skin.stattrak);
            out += ' ';
            out += e.equipped_t ? '1' : '0';
            out += ' ';
            out += e.equipped_ct ? '1' : '0';
            out += ' ';
            out += sanitize_nametag(e.skin.nametag);
            out += '|';
            for (int i = 0; i < sticker_slots; ++i)
            {
                if (i)
                    out += ' ';
                append(out, e.skin.stickers[i]);
            }
            out += '\n';
        }
        return out;
    }

    bool parse_def(std::string_view s, std::int16_t& out)
    {
        int value = 0;
        if (!parse(s, value) || value < 0 || value > 32767)
            return false;
        out = static_cast<std::int16_t>(value);
        return true;
    }

    bool parse_entry(std::string_view line, settings::changer::inventory_entry& e)
    {
        const auto bar = line.rfind('|');
        if (bar == std::string_view::npos)
            return false;

        auto head = line.substr(0, bar);
        auto tail = line.substr(bar + 1);

        int equipped_t = 0, equipped_ct = 0;
        if (!parse(next_token(head), e.uid) || !parse_def(next_token(head), e.def_index) ||
            !parse(next_token(head), e.skin.paint_kit_id) || !parse(next_token(head), e.skin.seed) ||
            !parse(next_token(head), e.skin.wear) || !parse(next_token(head), e.skin.stattrak) ||
            !parse(next_token(head), equipped_t) || !parse(next_token(head), equipped_ct))
            return false;

        if (e.def_index <= 0 || e.uid <= 0)
            return false;

        e.skin.paint_kit_id = std::max(e.skin.paint_kit_id, 0);
        e.skin.seed = std::max(e.skin.seed, 0);
        e.skin.wear = std::clamp(e.skin.wear, 0.f, 1.f);
        e.skin.stattrak = std::max(e.skin.stattrak, -1);
        e.equipped_t = equipped_t != 0;
        e.equipped_ct = equipped_ct != 0;
        e.skin.nametag = std::string(trim(head));

        for (int i = 0; i < sticker_slots; ++i)
        {
            int sticker = 0;
            if (!parse(next_token(tail), sticker))
                return false;
            e.skin.stickers[i] = std::max(sticker, 0);
        }
        return true;
    }

    void deserialize_inventory(std::string_view text)
    {
        std::vector<settings::changer::inventory_entry> inventory;
        settings::changer::agents agents{};
        int next_uid = 1;

        for_each_line(text, [&](std::string_view line) {
            line = trim(line);
            if (line.empty())
                return;
            if (line.substr(0, 7) == "agents ")
            {
                auto rest = line.substr(7);
                std::int16_t ct = 0, t = 0;
                if (parse_def(next_token(rest), ct) && parse_def(next_token(rest), t))
                {
                    agents.ct_def = ct;
                    agents.t_def = t;
                }
                return;
            }
            settings::changer::inventory_entry e{};
            if (!parse_entry(line, e))
                return;
            for (const auto& existing : inventory)
                if (existing.uid == e.uid)
                    return;
            next_uid = std::max(next_uid, e.uid + 1);
            inventory.push_back(std::move(e));
        });

        std::lock_guard lock(settings::g_changer_mutex);
        settings::g_changer.inventory = std::move(inventory);
        settings::g_changer.agents = agents;
        settings::g_changer.next_uid = std::max(settings::g_changer.next_uid, next_uid);
    }

    bool is_knife(std::int16_t def)
    {
        return def >= 500 && def < 600;
    }

    bool is_glove(std::int16_t def)
    {
        return def == 4725 || (def >= 5027 && def <= 5035);
    }

    bool equipped_for(const settings::changer::inventory_entry& e, int team)
    {
        if (team == 2)
            return e.equipped_t;
        if (team == 3)
            return e.equipped_ct;
        return false;
    }
}

std::uint32_t settings::color::abgr() const
{
    const auto channel = [](float v) {
        if (!std::isfinite(v))
            v = 0.f;
        return static_cast<std::uint32_t>(std::clamp(v, 0.f, 1.f) * 255.f + 0.5f);
    };
    return channel(r) | (channel(g) << 8) | (channel(b) << 16) | (channel(a) << 24);
}

void settings::changer_settings::rebuild(int team)
{
    std::lock_guard lock(g_changer_mutex);
    skins.data.clear();
    for (const auto& e : inventory)
        if (equipped_for(e, team))
            skins.data[e.def_index] = e.skin;
}

const settings::changer::applied_skin* settings::changer_settings::find(std::int16_t def_index, int team) const
{
    const changer::applied_skin* fallback = nullptr;
    for (const auto& e : inventory)
    {
        if (e.def_index != def_index)
            continue;
        if (equipped_for(e, team))
            return &e.skin;
        if (!fallback && (e.equipped_t || e.equipped_ct))
            fallback = &e.skin;
    }
    return fallback;
}

std::int16_t settings::changer_settings::knife(int team) const
{
    for (const auto& e : inventory)
        if (is_knife(e.def_index) && equipped_for(e, team))
            return e.def_index;
    return 0;
}

std::int16_t settings::changer_settings::glove(int team) const
{
    for (const auto& e : inventory)
        if (is_glove(e.def_index) && equipped_for(e, team))
            return e.def_index;
    return 0;
}

int settings::combat::weapon_group_of(std::uint16_t def)
{
    switch (def)
    {
    case 2:
    case 3:
    case 4:
    case 30:
    case 32:
    case 36:
    case 61:
    case 63:
        return wg_pistol;
    case 1:
    case 64:
        return wg_heavy_pistol;
    case 17:
    case 19:
    case 23:
    case 24:
    case 26:
    case 33:
    case 34:
        return wg_smg;
    case 7:
    case 8:
    case 10:
    case 13:
    case 16:
    case 39:
    case 60:
        return wg_rifle;
    case 25:
    case 27:
    case 29:
    case 35:
        return wg_shotgun;
    case 40:
        return wg_scout;
    case 9:
        return wg_awp;
    case 11:
    case 38:
        return wg_auto;
    case 14:
    case 28:
        return wg_machinegun;
    default:
        return wg_global;
    }
}

const char* settings::combat::weapon_group_name(int group)
{
    static constexpr const char* names[wg_count] = { "Global", "Pistols", "Deagle / R8", "SMG", "Rifles", "Shotguns", "Scout", "AWP", "Auto snipers", "Machine guns" };
    return group >= 0 && group < wg_count ? names[group] : names[wg_global];
}

const settings::combat::rage& settings::rage_for(int group)
{
    if (group > combat::wg_global && group < combat::wg_count && g_rage_groups[static_cast<std::size_t>(group)].override_global)
        return g_rage_groups[static_cast<std::size_t>(group)];
    return g_rage;
}

const settings::combat::legit& settings::legit_for(int group)
{
    if (group > combat::wg_global && group < combat::wg_count && g_legit_groups[static_cast<std::size_t>(group)].override_global)
        return g_legit_groups[static_cast<std::size_t>(group)];
    return g_legit;
}

const settings::combat::trigger& settings::trigger_for(int group)
{
    if (group > combat::wg_global && group < combat::wg_count && g_trigger_groups[static_cast<std::size_t>(group)].override_global)
        return g_trigger_groups[static_cast<std::size_t>(group)];
    return g_trigger;
}

void settings::register_all()
{
    std::lock_guard lock(g_registry_mutex);

    add("rage.enabled", g_rage.enabled);
    add("bind.rage", g_rage.key, "Ragebot");
    add("bind.damage_override", g_rage.damage_override_key, "Damage override");
    add("rage.doubletap", g_rage.doubletap);
    add("bind.doubletap", g_rage.doubletap_key, "Double tap");
    add("rage.doubletap_mode", g_rage.doubletap_mode);
    add("rage.tick_source", g_rage.tick_source);
    add("aim.legit", g_legit.enabled);
    add("bind.legit", g_legit.key, "Legitbot");
    add("bind.legit_random", g_legit.random_key, "Aim randomization");
    add("aim.trigger", g_trigger.enabled);
    add("bind.trigger", g_trigger.key, "Triggerbot");
    add("weapons.follow", g_edit_follow_weapon);

    for (int group = combat::wg_global; group < combat::wg_count; ++group)
    {
        const char* prefix = group_prefixes[group];
        auto& rage = g_rage_groups[static_cast<std::size_t>(group)];
        auto& legit = g_legit_groups[static_cast<std::size_t>(group)];
        auto& trigger = g_trigger_groups[static_cast<std::size_t>(group)];
        if (prefix)
        {
            add(group_key(prefix, "rage.override"), rage.override_global);
            add(group_key(prefix, "legit.override"), legit.override_global);
            add(group_key(prefix, "trigger.override"), trigger.override_global);
        }

        add(group_key(prefix, "rage.silent"), rage.silent);
        add(group_key(prefix, "rage.silent_smooth"), rage.silent_smooth);
        add(group_key(prefix, "rage.autofire"), rage.autofire);
        add(group_key(prefix, "rage.autowall"), rage.autowall);
        add(group_key(prefix, "rage.autostop"), rage.autostop);
        add(group_key(prefix, "rage.autostop_flags"), rage.autostop_flags);
        add(group_key(prefix, "rage.autoscope"), rage.autoscope);
        add(group_key(prefix, "rage.fov"), rage.fov);
        add(group_key(prefix, "rage.hitboxes"), rage.hitboxes);
        add(group_key(prefix, "rage.multipoint"), rage.multipoint);
        add(group_key(prefix, "hitbox.head_scale"), rage.head_scale);
        add(group_key(prefix, "hitbox.body_scale"), rage.body_scale);
        add(group_key(prefix, "rage.hitchance"), rage.hitchance);
        add(group_key(prefix, "rage.min_damage"), rage.minimum_damage);
        add(group_key(prefix, "rage.damage_override"), rage.damage_override);
        add(group_key(prefix, "rage.force_shot"), rage.force_shot);
        add(group_key(prefix, "rage.force_shot_iterations"), rage.force_shot_iterations);
        add(group_key(prefix, "rage.force_shot_min_spread"), rage.force_shot_min_spread);
        add(group_key(prefix, "hitbox.prefer_body"), rage.prefer_body);
        add(group_key(prefix, "nospread.enabled"), rage.nospread);
        add(group_key(prefix, "rage.seed_check"), rage.seed_check);
        add(group_key(prefix, "rage.teammates"), rage.teammates);

        add(group_key(prefix, "aim.legit_fov"), legit.fov);
        add(group_key(prefix, "aim.legit_speed"), legit.speed);
        add(group_key(prefix, "aim.legit_speed_mode"), legit.speed_mode);
        add(group_key(prefix, "aim.legit_randomization"), legit.randomization);
        add(group_key(prefix, "legit.hitboxes"), legit.hitboxes);
        add(group_key(prefix, "aim.legit_rcs"), legit.rcs);
        add(group_key(prefix, "legit.rcs_scale"), legit.rcs_scale);
        add(group_key(prefix, "legit.visible_only"), legit.visible_only);
        add(group_key(prefix, "legit.teammates"), legit.teammates);

        add(group_key(prefix, "aim.trigger_delay"), trigger.delay);
        add(group_key(prefix, "trigger.hitboxes"), trigger.hitboxes);
        add(group_key(prefix, "aim.trigger_min_damage"), trigger.minimum_damage);
        add(group_key(prefix, "trigger.hitchance"), trigger.hitchance);
        add(group_key(prefix, "trigger.seed_check"), trigger.seed_check);
        add(group_key(prefix, "trigger.teammates"), trigger.teammates);
    }

    add("movement.bhop", g_movement.bhop);
    add("bind.bhop", g_movement.bhop_key, "Bunnyhop");
    add("movement.autostrafe", g_movement.airstrafe);
    add("movement.airstrafe_fully_directional", g_movement.airstrafe_fully_directional);
    add("movement.strafe_mode", g_movement.airstrafe_mode);
    add("bind.strafe", g_movement.airstrafe_key, "Airstrafe");
    add("movement.jumpbug", g_movement.jumpbug);
    add("bind.jumpbug", g_movement.jumpbug_key, "Jumpbug");
    add("movement.fastladder", g_movement.fastladder);
    add("movement.quickstop", g_movement.quickstop);

    add("visuals.esp", g_visuals.esp);
    add("visuals.box", g_visuals.box);
    add("visuals.name", g_visuals.name);
    add("visuals.health", g_visuals.health);
    add("visuals.weapon", g_visuals.weapon);
    add("visuals.distance", g_visuals.distance);
    add("visuals.skeleton", g_visuals.skeleton);
    add("visuals.snaplines", g_visuals.snaplines);
    add("visuals.teammates", g_visuals.teammates);
    add("visuals.visible_color", g_visuals.visible);
    add("visuals.hidden_color", g_visuals.hidden);
    add("visuals.team_color", g_visuals.team);
    add("visuals.glow", g_visuals.glow);
    add("visuals.glow_team", g_visuals.glow_teammates);
    add("visuals.glow_visibility", g_visuals.glow_by_visibility);
    add("visuals.glow_color", g_visuals.glow_visible);
    add("visuals.glow_hidden_color", g_visuals.glow_hidden);
    add("visuals.glow_team_color", g_visuals.glow_team);
    add("visuals.fov_circle", g_visuals.fov_circle);
    add("visuals.grenade_prediction", g_visuals.grenade_prediction);
    add("visuals.grenade_color", g_visuals.grenade_color);
    add("hands.arms", g_visuals.hands_tint);
    add("hands.arms_color", g_visuals.hands_color);
    add("hands.weapon", g_visuals.weapon_tint);
    add("hands.weapon_color", g_visuals.weapon_color);

    add("camera.thirdperson", g_misc.thirdperson);
    add("bind.thirdperson", g_misc.thirdperson_key, "Thirdperson");
    add("camera.distance", g_misc.thirdperson_distance);
    add("overlay.watermark", g_misc.watermark);
    add("overlay.keybinds", g_misc.keybinds);
    add("overlay.shot_logs", g_misc.shot_logs);
    add("overlay.keybinds_x", g_misc.keybinds_x);
    add("overlay.keybinds_y", g_misc.keybinds_y);

    add("skins.enabled", g_changer.enabled);
    add("skins.knife_animations", g_changer.knife_animations);
    add("skins.language", g_changer.language);

    add("ui.accent", g_ui.accent);
    add("ui.reduce_motion", g_ui.reduce_motion);
    add("ui.scale", g_ui.scale);
    add("bind.menu", g_ui.menu_key, nullptr);
}

bool settings::save()
{
    std::lock_guard lock(g_io_mutex);
    const bool settings_ok = write_atomic(settings_file(), serialize_settings());
    const bool inventory_ok = write_atomic(inventory_file(), serialize_inventory());
    if (!settings_ok)
        logs::Add(logs::Error, "Config: cannot write %s", path());
    if (!inventory_ok)
        logs::Add(logs::Error, "Config: cannot write inventory.ini");
    return settings_ok && inventory_ok;
}

bool settings::load()
{
    std::lock_guard lock(g_io_mutex);
    std::string text;
    const bool settings_ok = read_file(settings_file(), text);
    if (settings_ok)
        deserialize_settings(text);
    if (read_file(inventory_file(), text))
        deserialize_inventory(text);
    if (settings_ok)
        logs::Add(logs::Info, "Config loaded");
    return settings_ok;
}

const char* settings::path()
{
    static const std::string value = narrow(settings_file());
    return value.c_str();
}
