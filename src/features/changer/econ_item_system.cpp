#include "changer.h"
#include "changer_detail.h"
#include "../../core/addresses.h"
#include "../../core/hooks.h"
#include "../../core/log.h"
#include "../../core/memory.h"
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
    constexpr int max_wait_attempts = 300;
    constexpr auto wait_step = std::chrono::milliseconds(100);
    constexpr int max_entries = 10000;

    constexpr std::uintptr_t schema_pointer_offset = 0x8;
    constexpr std::uintptr_t defs_count_offset = 0x128;
    constexpr std::uintptr_t defs_data_offset = 0x130;
    constexpr std::uintptr_t paints_count_offset = 0x2F0;
    constexpr std::uintptr_t paints_data_offset = 0x2F8;
    constexpr std::uintptr_t entry_stride = 32;
    constexpr std::uintptr_t entry_key_offset = 16;
    constexpr std::uintptr_t entry_pointer_offset = 24;

    constexpr std::uintptr_t def_index_offset = 0x10;
    constexpr std::uintptr_t def_rarity_offset = 0x42;
    constexpr std::uintptr_t def_name_token_offset = 0x70;
    constexpr std::uintptr_t def_image_offset = 0xA8;
    constexpr std::uintptr_t def_model_offset = 0x148;
    constexpr std::uintptr_t def_class_offset = 0x260;
    constexpr std::uintptr_t def_slot_offset = 0x338;
    constexpr std::uintptr_t def_classes_offset = 0x368;
    constexpr std::size_t def_size = def_classes_offset + sizeof(std::uint32_t);

    constexpr std::uintptr_t paint_id_offset = 0x00;
    constexpr std::uintptr_t paint_name_offset = 0x08;
    constexpr std::uintptr_t paint_desc_offset = 0x10;
    constexpr std::uintptr_t paint_token_offset = 0x18;
    constexpr std::uintptr_t paint_rarity_offset = 0x44;
    constexpr std::uintptr_t paint_wear_min_offset = 0x6C;
    constexpr std::uintptr_t paint_wear_max_offset = 0x70;
    constexpr std::uintptr_t paint_legacy_offset = 0xAE;
    constexpr std::size_t paint_size = paint_legacy_offset + sizeof(bool);

    constexpr std::size_t localize_find_safe = 17;
    constexpr std::size_t max_localized_length = 512;

    constexpr std::uint32_t vpk_signature = 0x55AA1234u;
    constexpr std::uint32_t vpk_version = 2;
    constexpr std::size_t vpk_header_size = 28;
    constexpr std::size_t vpk_entry_size = 18;
    constexpr std::size_t vpk_preload_offset = 4;
    constexpr std::uint32_t vpk_max_tree = 256u * 1024u * 1024u;
    constexpr std::string_view vpk_extension = "vtex_c";
    constexpr std::string_view vpk_generated_dir = "panorama/images/econ/default_generated";
    constexpr std::string_view light_suffix = "_light_png";

    constexpr int agent_slot = 38;
    constexpr int glove_slot = 41;
    constexpr std::int16_t knife_first = 500;
    constexpr std::int16_t knife_last = 599;
    constexpr int knife_glove_rarity = 6;
    constexpr int max_rarity = 7;
    constexpr int capped_rarity = 6;

    constexpr std::string_view excluded_weapons[] = {
        "weapon_flashbang",
        "weapon_hegrenade",
        "weapon_smokegrenade",
        "weapon_molotov",
        "weapon_decoy",
        "weapon_incgrenade",
        "weapon_tagrenade",
        "weapon_c4",
        "weapon_healthshot",
        "weapon_taser"
    };

    std::thread g_worker;
    std::atomic<bool> g_stop{ false };
    std::vector<std::string> g_generated;
    std::unordered_map<std::int16_t, std::string> g_icon_codes;

    struct worker_guard
    {
        ~worker_guard()
        {
            if (g_worker.joinable())
            {
                g_stop.store(true);
                g_worker.detach();
            }
        }
    };

    worker_guard g_worker_guard{};

    bool stopping()
    {
        return g_stop.load() || hooks::unloading.load();
    }

    void run_guarded(void (*function)(void*), void* context)
    {
        __try
        {
            function(context);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            logs::Add(logs::Error, "[changer] econ worker faulted");
        }
    }

    const char* find_localized(std::uintptr_t localize, const char* token)
    {
        __try
        {
            return memory::call_vfunc<const char*>(localize, localize_find_safe, token);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    std::string localize(const std::string& token)
    {
        const std::uintptr_t localize = addresses::globals::localize();
        if (!localize || token.empty() || !features::changer::detail::has_vfunc(localize, localize_find_safe))
            return {};
        const char* text = find_localized(localize, token.c_str());
        if (!text)
            return {};
        std::string out = memory::read_string(reinterpret_cast<std::uintptr_t>(text), max_localized_length);
        if (out.empty() || out == token)
            return {};
        return out;
    }

    std::string read_text(std::uintptr_t base, std::uintptr_t offset)
    {
        const std::uintptr_t text = memory::safe_read<std::uintptr_t>(base + offset).value_or(0);
        return text ? memory::read_string(text) : std::string{};
    }

    std::string image_stem(const std::string& image)
    {
        const auto slash = image.find_last_of('/');
        return slash == std::string::npos ? image : image.substr(slash + 1);
    }

    float sanitize_wear_bound(float value, float fallback)
    {
        return std::isfinite(value) && value >= 0.f && value <= 1.f ? value : fallback;
    }

    std::uintptr_t wait_for_schema()
    {
        for (int attempt = 0; attempt < max_wait_attempts && !stopping(); ++attempt)
        {
            const std::uintptr_t system = addresses::globals::item_system();
            const std::uintptr_t schema = system ? memory::safe_read<std::uintptr_t>(system + schema_pointer_offset).value_or(0) : 0;
            if (schema)
            {
                const int defs = memory::safe_read<int>(schema + defs_count_offset).value_or(0);
                const auto defs_data = memory::safe_read<std::uintptr_t>(schema + defs_data_offset).value_or(0);
                const int paints = memory::safe_read<int>(schema + paints_count_offset).value_or(0);
                const auto paints_data = memory::safe_read<std::uintptr_t>(schema + paints_data_offset).value_or(0);
                if (defs > 0 && defs <= max_entries && defs_data && paints > 0 && paints <= max_entries && paints_data)
                    return schema;
            }
            if (attempt == 0)
                logs::Add(logs::Info, "[changer] waiting for item schema");
            std::this_thread::sleep_for(wait_step);
        }
        return 0;
    }

    std::filesystem::path csgo_directory()
    {
        const HMODULE client = GetModuleHandleW(L"client.dll");
        if (!client)
            return {};
        std::wstring buffer(32768, L'\0');
        const DWORD length = GetModuleFileNameW(client, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (!length || length >= buffer.size())
            return {};
        buffer.resize(length);
        return std::filesystem::path(buffer).parent_path().parent_path().parent_path();
    }

    bool next_string(const std::vector<char>& tree, std::size_t& position, std::string_view& out)
    {
        if (position >= tree.size())
            return false;
        const void* end = std::memchr(tree.data() + position, 0, tree.size() - position);
        if (!end)
            return false;
        const std::size_t length = static_cast<std::size_t>(static_cast<const char*>(end) - (tree.data() + position));
        out = std::string_view(tree.data() + position, length);
        position += length + 1;
        return true;
    }

    bool scan_generated_icons()
    {
        g_generated.clear();

        const std::filesystem::path directory = csgo_directory();
        if (directory.empty())
            return false;

        std::ifstream file(directory / L"pak01_dir.vpk", std::ios::binary);
        if (!file.is_open())
            return false;

        std::uint32_t header[vpk_header_size / sizeof(std::uint32_t)]{};
        if (!file.read(reinterpret_cast<char*>(header), sizeof(header)))
            return false;
        if (header[0] != vpk_signature || header[1] != vpk_version || !header[2] || header[2] > vpk_max_tree)
            return false;

        std::vector<char> tree(header[2]);
        if (!file.read(tree.data(), static_cast<std::streamsize>(tree.size())))
            return false;

        std::size_t position = 0;
        while (!stopping())
        {
            std::string_view extension;
            if (!next_string(tree, position, extension))
                return false;
            if (extension.empty())
                return true;

            while (true)
            {
                std::string_view directory_name;
                if (!next_string(tree, position, directory_name))
                    return false;
                if (directory_name.empty())
                    break;

                const bool wanted = extension == vpk_extension && directory_name == vpk_generated_dir;
                while (true)
                {
                    std::string_view file_name;
                    if (!next_string(tree, position, file_name))
                        return false;
                    if (file_name.empty())
                        break;
                    if (position + vpk_entry_size > tree.size())
                        return false;
                    std::uint16_t preload = 0;
                    std::memcpy(&preload, tree.data() + position + vpk_preload_offset, sizeof(preload));
                    position += vpk_entry_size;
                    if (position + preload > tree.size())
                        return false;
                    position += preload;
                    if (wanted && file_name.size() > light_suffix.size() && file_name.ends_with(light_suffix))
                        g_generated.emplace_back(file_name.substr(0, file_name.size() - light_suffix.size()));
                }
            }
        }
        return false;
    }

    const std::string& empty_text()
    {
        static const std::string text;
        return text;
    }
}

namespace features::changer
{
    namespace detail
    {
        void shutdown_econ()
        {
            g_stop.store(true);
            if (g_worker.joinable() && g_worker.get_id() != std::this_thread::get_id())
                g_worker.join();
        }
    }

    bool econ_item_system::initialize()
    {
        if (m_ready.load() || g_worker.joinable())
            return false;

        g_stop.store(false);

        const auto load = [](void* context) {
            auto& self = *static_cast<econ_item_system*>(context);

            const std::uintptr_t schema = wait_for_schema();
            if (!schema)
            {
                if (!stopping())
                    logs::Add(logs::Warning, "[changer] timed out waiting for item schema");
                return;
            }

            if (!self.parse_item_defs(schema))
            {
                logs::Add(logs::Error, "[changer] failed to parse item definitions");
                return;
            }

            if (!self.parse_paint_kits(schema))
            {
                logs::Add(logs::Error, "[changer] failed to parse paint kits");
                return;
            }

            if (stopping())
                return;

            if (!scan_generated_icons())
            {
                g_generated.clear();
                logs::Add(logs::Warning, "[changer] VPK skin index unavailable");
            }

            if (stopping())
                return;

            self.build_skin_index();
            self.build_indices();
            self.resolve_localized_names();
            self.m_ready.store(true);

            logs::Add(logs::Success, "[changer] econ ready: %d items, %d paint kits, %d skins",
                static_cast<int>(self.m_item_defs.size()), static_cast<int>(self.m_paint_kits.size()), static_cast<int>(self.m_skins.size()));
        };

        try
        {
            g_worker = std::thread(run_guarded, static_cast<void (*)(void*)>(load), static_cast<void*>(this));
        }
        catch (...)
        {
            logs::Add(logs::Error, "[changer] failed to start econ worker");
            return false;
        }
        return true;
    }

    std::vector<int> econ_item_system::skins_for(std::int16_t def_index) const
    {
        if (!ready())
            return {};
        const auto it = m_skins_by_def.find(def_index);
        return it == m_skins_by_def.end() ? std::vector<int>{} : it->second;
    }

    const econ_item_system::item_def* econ_item_system::find_def(std::int16_t def_index) const
    {
        if (!ready())
            return nullptr;
        const auto it = m_def_index_map.find(def_index);
        return it == m_def_index_map.end() ? nullptr : &m_item_defs[it->second];
    }

    const econ_item_system::paint_kit* econ_item_system::find_paint_kit(int id) const
    {
        if (!ready())
            return nullptr;
        const auto it = m_paint_kit_map.find(id);
        return it == m_paint_kit_map.end() ? nullptr : &m_paint_kits[it->second];
    }

    int econ_item_system::combined_rarity(std::int16_t def_index, int paint_kit_id) const
    {
        const auto* def = find_def(def_index);
        if (def && (def->category == item_category::knife || def->category == item_category::glove))
            return knife_glove_rarity;
        const int base = def && def->rarity ? static_cast<int>(def->rarity) : 1;
        const auto* pk = paint_kit_id ? find_paint_kit(paint_kit_id) : nullptr;
        const int paint = pk ? std::clamp(static_cast<int>(pk->rarity), 0, max_rarity) : 0;
        return std::clamp(base + paint - 1, 0, paint == max_rarity ? max_rarity : capped_rarity);
    }

    std::string econ_item_system::image_path(std::int16_t def_index, int paint_kit_id, float wear) const
    {
        const auto* def = find_def(def_index);
        if (!def)
            return {};

        const auto* pk = paint_kit_id ? find_paint_kit(paint_kit_id) : nullptr;
        if (!pk || !pk->id || pk->name.empty())
            return def->image_inventory.empty() ? std::string{} : "panorama/images/" + def->image_inventory + "_png.vtex_c";

        std::string code;
        if (const auto it = g_icon_codes.find(def_index); it != g_icon_codes.end())
            code = it->second;
        else if (!def->name.empty())
            code = def->name;
        else
            code = image_stem(def->image_inventory);
        if (code.empty())
            return {};

        const char* variant = wear < 0.15f ? "_light" : wear < 0.45f ? "_medium" : "_heavy";
        return "panorama/images/econ/default_generated/" + code + "_" + pk->name + variant + "_png.vtex_c";
    }

    bool econ_item_system::parse_item_defs(std::uintptr_t schema)
    {
        const int count = memory::safe_read<int>(schema + defs_count_offset).value_or(0);
        const std::uintptr_t data = memory::safe_read<std::uintptr_t>(schema + defs_data_offset).value_or(0);
        if (count <= 0 || count > max_entries || !data || !memory::is_readable(data, static_cast<std::size_t>(count) * entry_stride))
            return false;

        m_item_defs.clear();
        m_item_defs.reserve(static_cast<std::size_t>(count));
        std::unordered_set<std::int16_t> seen;

        for (int i = 0; i < count && !stopping(); ++i)
        {
            const std::uintptr_t entry = data + static_cast<std::uintptr_t>(i) * entry_stride;
            if (memory::read<int>(entry + entry_key_offset) < 0)
                continue;
            const std::uintptr_t pointer = memory::read<std::uintptr_t>(entry + entry_pointer_offset);
            if (!pointer || !memory::is_readable(pointer, def_size))
                continue;

            item_def item{};
            item.def_index = memory::read<std::int16_t>(pointer + def_index_offset);
            if (item.def_index < 0 || !seen.insert(item.def_index).second)
                continue;
            item.loadout_slot = memory::read<int>(pointer + def_slot_offset);
            item.used_by_classes = memory::read<std::uint32_t>(pointer + def_classes_offset);
            item.rarity = memory::read<std::uint8_t>(pointer + def_rarity_offset);
            item.name = read_text(pointer, def_class_offset);
            item.item_class = item.name;
            item.model_player = read_text(pointer, def_model_offset);
            item.image_inventory = read_text(pointer, def_image_offset);
            item.localized_name = localize(read_text(pointer, def_name_token_offset));
            if (item.localized_name.empty())
                item.localized_name = item.name;
            m_item_defs.push_back(std::move(item));
        }

        return !m_item_defs.empty();
    }

    bool econ_item_system::parse_paint_kits(std::uintptr_t schema)
    {
        const int count = memory::safe_read<int>(schema + paints_count_offset).value_or(0);
        const std::uintptr_t data = memory::safe_read<std::uintptr_t>(schema + paints_data_offset).value_or(0);
        if (count <= 0 || count > max_entries || !data || !memory::is_readable(data, static_cast<std::size_t>(count) * entry_stride))
            return false;

        m_paint_kits.clear();
        m_paint_kits.reserve(static_cast<std::size_t>(count));
        std::unordered_set<int> seen;

        for (int i = 0; i < count && !stopping(); ++i)
        {
            const std::uintptr_t pointer = memory::read<std::uintptr_t>(data + static_cast<std::uintptr_t>(i) * entry_stride + entry_pointer_offset);
            if (!pointer || !memory::is_readable(pointer, paint_size))
                continue;

            paint_kit pk{};
            pk.id = memory::read<int>(pointer + paint_id_offset);
            if (pk.id < 0 || !seen.insert(pk.id).second)
                continue;
            pk.wear_min = sanitize_wear_bound(memory::read<float>(pointer + paint_wear_min_offset), 0.f);
            pk.wear_max = sanitize_wear_bound(memory::read<float>(pointer + paint_wear_max_offset), 1.f);
            if (pk.wear_max < pk.wear_min)
                pk.wear_max = pk.wear_min;
            pk.legacy_model = memory::read<std::uint8_t>(pointer + paint_legacy_offset) != 0;
            pk.rarity = static_cast<std::uint8_t>(memory::read<int>(pointer + paint_rarity_offset) & 0xFF);
            pk.name = read_text(pointer, paint_name_offset);
            pk.desc_token = read_text(pointer, paint_desc_offset);
            pk.name_token = read_text(pointer, paint_token_offset);
            m_paint_kits.push_back(std::move(pk));
        }

        return !m_paint_kits.empty();
    }

    void econ_item_system::build_skin_index()
    {
        m_skins.clear();
        m_skins_by_def.clear();
        g_icon_codes.clear();

        std::unordered_map<std::string, int> paint_by_name;
        paint_by_name.reserve(m_paint_kits.size());
        for (const auto& pk : m_paint_kits)
            if (pk.id && !pk.name.empty())
                paint_by_name.emplace(pk.name, pk.id);

        std::unordered_map<std::string, std::int16_t> def_by_code;
        def_by_code.reserve(m_item_defs.size() * 2);
        for (const auto& def : m_item_defs)
            if (!def.name.empty())
                def_by_code.emplace(def.name, def.def_index);
        for (const auto& def : m_item_defs)
            if (const std::string stem = image_stem(def.image_inventory); !stem.empty())
                def_by_code.emplace(stem, def.def_index);

        std::unordered_set<std::uint64_t> seen;
        for (const std::string& stem : g_generated)
        {
            std::int16_t def_index = -1;
            int paint_id = 0;
            std::size_t split = std::string::npos;
            for (std::size_t i = stem.find('_', 1); i != std::string::npos; i = stem.find('_', i + 1))
            {
                const auto def_it = def_by_code.find(stem.substr(0, i));
                if (def_it == def_by_code.end())
                    continue;
                const auto paint_it = paint_by_name.find(stem.substr(i + 1));
                if (paint_it == paint_by_name.end())
                    continue;
                def_index = def_it->second;
                paint_id = paint_it->second;
                split = i;
            }
            if (def_index < 0 || split == std::string::npos)
                continue;

            const std::uint64_t key = (static_cast<std::uint64_t>(static_cast<std::uint16_t>(def_index)) << 32) | static_cast<std::uint32_t>(paint_id);
            if (!seen.insert(key).second)
                continue;
            m_skins.push_back({ def_index, paint_id });
            m_skins_by_def[def_index].push_back(paint_id);
            g_icon_codes.try_emplace(def_index, stem.substr(0, split));
        }
    }

    void econ_item_system::build_indices()
    {
        m_def_index_map.clear();
        m_paint_kit_map.clear();
        m_knives.clear();
        m_gloves.clear();
        m_agents.clear();
        m_guns.clear();

        for (std::size_t i = 0; i < m_paint_kits.size(); ++i)
            m_paint_kit_map.try_emplace(m_paint_kits[i].id, i);

        for (std::size_t i = 0; i < m_item_defs.size(); ++i)
        {
            auto& def = m_item_defs[i];
            m_def_index_map.try_emplace(def.def_index, i);
            def.category = classify(def);
        }

        for (const auto& def : m_item_defs)
        {
            switch (def.category)
            {
            case item_category::knife:
                if (def.def_index >= knife_first && def.def_index <= knife_last)
                {
                    m_knives.push_back(&def);
                    m_skins_by_def[def.def_index].insert(m_skins_by_def[def.def_index].begin(), 0);
                }
                break;
            case item_category::glove:
                if (m_skins_by_def.contains(def.def_index))
                    m_gloves.push_back(&def);
                break;
            case item_category::agent:
                if (!def.model_player.empty())
                    m_agents.push_back(&def);
                break;
            case item_category::gun:
                m_guns.push_back(&def);
                break;
            default:
                break;
            }
        }
    }

    void econ_item_system::resolve_localized_names()
    {
        for (auto& pk : m_paint_kits)
        {
            if (stopping())
                return;
            pk.localized_name = localize(pk.name_token);
            if (pk.localized_name.empty())
                pk.localized_name = pk.name;
        }

        const auto paint_of = [this](int id) -> const paint_kit* {
            const auto it = m_paint_kit_map.find(id);
            return it == m_paint_kit_map.end() ? nullptr : &m_paint_kits[it->second];
        };

        for (auto& [def_index, ids] : m_skins_by_def)
        {
            std::sort(ids.begin(), ids.end(), [&paint_of](int a, int b) {
                if ((a == 0) != (b == 0))
                    return a == 0;
                const auto* pa = paint_of(a);
                const auto* pb = paint_of(b);
                const int ra = pa ? pa->rarity : 0;
                const int rb = pb ? pb->rarity : 0;
                if (ra != rb)
                    return ra > rb;
                const std::string& na = pa ? pa->localized_name : empty_text();
                const std::string& nb = pb ? pb->localized_name : empty_text();
                if (na != nb)
                    return na < nb;
                return a < b;
            });
        }
    }

    econ_item_system::item_category econ_item_system::classify(const item_def& def) const
    {
        if (def.loadout_slot == agent_slot)
            return item_category::agent;
        if (def.loadout_slot == glove_slot)
            return item_category::glove;

        const std::string_view name = def.name;
        if ((def.def_index >= knife_first && def.def_index <= knife_last) || name == "weapon_bayonet" || name.starts_with("weapon_knife"))
            return item_category::knife;
        if (!name.starts_with("weapon_"))
            return item_category::other;
        if (std::find(std::begin(excluded_weapons), std::end(excluded_weapons), name) != std::end(excluded_weapons))
            return item_category::other;
        if (g_generated.empty())
            return item_category::gun;
        return m_skins_by_def.contains(def.def_index) ? item_category::gun : item_category::other;
    }
}
