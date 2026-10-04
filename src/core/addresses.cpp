#include "addresses.h"
#include "log.h"
#include "patterns.h"
#include "../../deps/minhook/src/hde/hde64.h"
#include <Windows.h>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace
{
    constexpr std::uint64_t retry_interval_ms = 500;
    constexpr int generator_max_instructions = 40;
    constexpr std::size_t generator_max_bytes = 96;
    constexpr std::size_t generator_min_bytes = 6;
    constexpr std::size_t decode_window = 16;
    constexpr std::uint32_t max_disp_back = 10;
    constexpr std::size_t npos = static_cast<std::size_t>(-1);

    struct slot
    {
        std::atomic<std::uintptr_t> location{ 0 };
        std::atomic<std::uint64_t> retry_at{ 0 };
        std::atomic<bool> retryable{ false };
    };

    struct registry
    {
        std::span<const addresses::entry* const> table;
        std::unordered_map<const addresses::entry*, std::size_t> index;
        std::unique_ptr<slot[]> slots;
    };

    struct image
    {
        std::vector<std::uint8_t> text;
        std::uint32_t rva = 0;
        bool loaded = false;
    };

    enum class job_kind : std::uint8_t
    {
        verify,
        generate,
        custom
    };

    struct job
    {
        job_kind kind = job_kind::verify;
        int index = -1;
        std::string module;
        std::uint32_t rva = 0;
    };

    std::mutex g_resolve_lock;
    std::mutex g_lock;
    std::vector<addresses::record> g_records;
    std::deque<job> g_jobs;
    std::condition_variable g_wake;
    std::thread g_worker;
    std::atomic<bool> g_stop{ false };
    std::atomic<int> g_running{ 0 };
    std::string g_custom;
    bool g_custom_pending = false;
    std::map<std::string, image> g_images;

    registry& get_registry()
    {
        static registry r = [] {
            registry out;
            out.table = patterns::all();
            out.slots = std::make_unique<slot[]>(out.table.size());
            for (std::size_t i = 0; i < out.table.size(); ++i)
                if (out.table[i])
                    out.index.emplace(out.table[i], i);
            return out;
        }();
        return r;
    }

    std::string module_of(const char* spec)
    {
        if (!spec)
            return {};
        const char* colon = std::strchr(spec, ':');
        return colon ? std::string(spec, colon) : std::string();
    }

    void ensure_records_locked()
    {
        const registry& reg = get_registry();
        if (g_records.size() == reg.table.size())
            return;
        g_records.clear();
        g_records.reserve(reg.table.size());
        for (const addresses::entry* e : reg.table)
        {
            addresses::record r;
            if (e)
            {
                r.name = e->name ? e->name : "";
                r.spec = e->spec ? e->spec : "";
                r.module = module_of(e->spec);
            }
            g_records.push_back(std::move(r));
        }
    }

    template <typename F>
    void update_record(std::size_t index, F&& f)
    {
        if (index == npos)
            return;
        std::lock_guard lock(g_lock);
        ensure_records_locked();
        if (index < g_records.size())
            f(g_records[index]);
    }

    std::uint32_t rva_of(std::uintptr_t address, const memory::module_info& m)
    {
        return m.contains(address) ? static_cast<std::uint32_t>(address - m.base) : 0;
    }

    std::uintptr_t attempt(const addresses::entry& e, slot* s, std::size_t index)
    {
        const std::uint64_t now = GetTickCount64();
        const auto fail = [&](bool retry) {
            if (s)
            {
                s->retry_at.store(now + retry_interval_ms, std::memory_order_relaxed);
                s->retryable.store(retry, std::memory_order_release);
            }
            e.attempted.store(true, std::memory_order_release);
            return std::uintptr_t{ 0 };
        };

        const memory::pattern p = memory::parse_pattern(e.spec);
        if (!p.valid)
        {
            update_record(index, [](addresses::record& r) { r.state = addresses::status::missing; r.matches = 0; });
            logs::Add(logs::Error, "Pattern invalid: %s", e.name ? e.name : "?");
            return fail(false);
        }

        std::uintptr_t location = s ? s->location.load(std::memory_order_acquire) : 0;
        if (!location)
        {
            const memory::module_info& m = memory::get_module(p.module.c_str());
            if (!m)
                return fail(true);
            int matches = 0;
            const std::uintptr_t match = memory::scan(m.text, p, &matches, 2);
            memory::pattern located = p;
            located.deref = false;
            location = match ? memory::apply(match, located) : 0;
            const addresses::status state = !location ? addresses::status::missing : matches > 1 ? addresses::status::ambiguous : addresses::status::unique;
            const std::uint32_t rva = match ? rva_of(match, m) : 0;
            update_record(index, [&](addresses::record& r) {
                r.matches = matches;
                r.rva = rva;
                r.state = state;
                r.address = 0;
            });
            if (!location)
            {
                logs::Add(logs::Warning, "Pattern not found: %s", e.name ? e.name : "?");
                return fail(false);
            }
            if (state == addresses::status::ambiguous)
                logs::Add(logs::Warning, "Pattern ambiguous: %s", e.name ? e.name : "?");
            if (s)
                s->location.store(location, std::memory_order_release);
        }

        std::uintptr_t result = location;
        if (p.deref)
        {
            result = memory::is_readable(location) ? memory::read<std::uintptr_t>(location) : 0;
            if (!result)
                return fail(true);
        }
        update_record(index, [result](addresses::record& r) { r.address = result; });
        e.cached.store(result, std::memory_order_release);
        e.attempted.store(true, std::memory_order_release);
        return result;
    }

    image* load_image(const std::string& module)
    {
        image& img = g_images[module];
        if (img.loaded)
            return img.text.empty() ? nullptr : &img;
        img.loaded = true;
        const memory::module_info& m = memory::get_module(module.c_str());
        char path[MAX_PATH]{};
        if (!m || !GetModuleFileNameA(reinterpret_cast<HMODULE>(m.base), path, MAX_PATH))
            return nullptr;
        const HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return nullptr;
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(m.base);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(m.base + dos->e_lfanew);
        const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
        for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec)
        {
            if (std::memcmp(sec->Name, ".text", 6))
                continue;
            const DWORD size = sec->SizeOfRawData < sec->Misc.VirtualSize ? sec->SizeOfRawData : sec->Misc.VirtualSize;
            img.text.resize(size);
            img.rva = sec->VirtualAddress;
            LARGE_INTEGER at{};
            at.QuadPart = sec->PointerToRawData;
            DWORD read = 0;
            if (!SetFilePointerEx(file, at, nullptr, FILE_BEGIN) || !ReadFile(file, img.text.data(), size, &read, nullptr) || read != size)
                img.text.clear();
            break;
        }
        CloseHandle(file);
        return img.text.empty() ? nullptr : &img;
    }

    memory::section image_section(const image& img)
    {
        return { reinterpret_cast<std::uintptr_t>(img.text.data()), img.text.size() };
    }

    unsigned immediate_size(const hde64s& hs)
    {
        unsigned size = 0;
        if (hs.flags & F_IMM8)
            size += 1;
        if (hs.flags & F_IMM16)
            size += 2;
        if (hs.flags & F_IMM32)
            size += 4;
        if (hs.flags & F_IMM64)
            size += 8;
        return size;
    }

    bool build(const image& img, std::uint32_t rva, std::size_t min_len, memory::pattern& p)
    {
        p = {};
        p.valid = true;
        if (rva < img.rva || rva - img.rva >= img.text.size())
            return false;
        const std::size_t start = rva - img.rva;
        const std::uint8_t* code = img.text.data() + start;
        const std::size_t avail = img.text.size() - start;
        const memory::section whole = image_section(img);
        const std::size_t need = min_len > generator_min_bytes ? min_len : generator_min_bytes;
        std::size_t at = 0;
        for (int insn = 0; insn < generator_max_instructions && at < generator_max_bytes && at + decode_window <= avail && !g_stop; ++insn)
        {
            hde64s hs{};
            const unsigned len = hde64_disasm(code + at, &hs);
            if (!len || (hs.flags & F_ERROR))
                break;
            const std::size_t first = p.bytes.size();
            for (unsigned i = 0; i < len; ++i)
            {
                p.bytes.push_back(code[at + i]);
                p.mask.push_back(1);
            }
            const unsigned imm = immediate_size(hs);
            if ((hs.flags & F_RELATIVE) && imm <= len)
                for (unsigned i = len - imm; i < len; ++i)
                    p.mask[first + i] = 0;
            if ((hs.flags & F_DISP32) && imm + 4 <= len)
                for (unsigned i = len - imm - 4; i < len - imm; ++i)
                    p.mask[first + i] = 0;
            at += len;
            if (at >= need)
            {
                int matches = 0;
                memory::scan(whole, p, &matches, 2);
                if (matches == 1)
                {
                    while (p.bytes.size() > need && !p.mask.back())
                    {
                        p.bytes.pop_back();
                        p.mask.pop_back();
                    }
                    return true;
                }
            }
            if (hs.opcode == 0xC3 || hs.opcode == 0xCC || (hs.opcode == 0xE9 && !(hs.flags & F_PREFIX_ANY)))
                break;
        }
        return false;
    }

    bool find_disp_instruction(const image& img, std::uint32_t disp_rva, std::uint32_t& start, int& disp_index)
    {
        for (std::uint32_t k = 1; k <= max_disp_back; ++k)
        {
            if (disp_rva < img.rva + k)
                return false;
            const std::uint32_t s = disp_rva - k;
            const std::size_t offset = s - img.rva;
            if (offset + decode_window > img.text.size())
                continue;
            hde64s hs{};
            const unsigned len = hde64_disasm(img.text.data() + offset, &hs);
            if (!len || (hs.flags & F_ERROR))
                continue;
            const unsigned imm = immediate_size(hs);
            const bool rip = (hs.flags & F_DISP32) && hs.modrm_mod == 0 && hs.modrm_rm == 5 && imm + 4 <= len && len - imm - 4 == k;
            const bool relative = (hs.flags & F_RELATIVE) && (hs.flags & F_IMM32) && len >= 4 && len - 4 == k;
            if (rip || relative)
            {
                start = s;
                disp_index = static_cast<int>(k);
                return true;
            }
        }
        return false;
    }

    std::string suffix_of(const memory::pattern& p)
    {
        std::string out;
        if (p.offset)
        {
            char buffer[24]{};
            const auto magnitude = static_cast<unsigned long long>(p.offset < 0 ? -p.offset : p.offset);
            std::snprintf(buffer, sizeof(buffer), "%c%llX", p.offset < 0 ? '-' : '+', magnitude);
            out += buffer;
        }
        if (p.deref)
            out += '~';
        return out;
    }

    std::string format(const std::string& module, const memory::pattern& p, int marker, char marker_kind, const std::string& suffix)
    {
        std::string out = module + ':';
        char hex[4]{};
        for (std::size_t i = 0; i < p.bytes.size(); ++i)
        {
            if (static_cast<int>(i) == marker)
                out += marker_kind;
            if (p.mask[i])
            {
                std::snprintf(hex, sizeof(hex), "%02X", p.bytes[i]);
                out += hex;
            }
            else
                out += "??";
        }
        if (marker >= 0 && static_cast<std::size_t>(marker) >= p.bytes.size())
            out += marker_kind;
        return out + suffix;
    }

    std::string generate_entry(const std::string& module, const std::string& spec, std::uint32_t match_rva)
    {
        image* img = load_image(module);
        if (!img)
            return {};
        const memory::pattern original = memory::parse_pattern(spec.c_str());
        if (!original.valid)
            return {};
        std::uint32_t start = match_rva;
        std::size_t min_len = 0;
        int marker = -1;
        if (original.marker >= 0)
        {
            const std::uint32_t marker_rva = match_rva + static_cast<std::uint32_t>(original.marker);
            if (original.marker_kind == '>')
            {
                start = marker_rva;
                marker = 0;
                min_len = 5;
            }
            else
            {
                int disp_index = 0;
                if (!find_disp_instruction(*img, marker_rva, start, disp_index))
                    return {};
                marker = disp_index;
                min_len = static_cast<std::size_t>(disp_index) + 4;
            }
        }
        memory::pattern p;
        if (!build(*img, start, min_len, p))
            return {};
        return format(module, p, marker, original.marker_kind, suffix_of(original));
    }

    std::string generate_custom(const std::string& module, std::uint32_t rva)
    {
        image* img = load_image(module);
        if (!img)
            return {};
        memory::pattern p;
        if (!build(*img, rva, 0, p))
            return {};
        return format(module, p, -1, 0, {});
    }

    int verify_entry(const std::string& module, const std::string& spec)
    {
        image* img = load_image(module);
        if (!img)
            return -1;
        const memory::pattern p = memory::parse_pattern(spec.c_str());
        if (!p.valid)
            return 0;
        int matches = 0;
        memory::scan(image_section(*img), p, &matches, 2);
        return matches;
    }

    void run(const job& j)
    {
        if (j.kind == job_kind::custom)
        {
            std::string out = generate_custom(j.module, j.rva);
            std::lock_guard lock(g_lock);
            g_custom = out.empty() ? "no unique pattern" : std::move(out);
            g_custom_pending = false;
            return;
        }
        std::string module, spec;
        std::uint32_t rva = 0;
        addresses::status state = addresses::status::pending;
        {
            std::lock_guard lock(g_lock);
            ensure_records_locked();
            if (j.index < 0 || static_cast<std::size_t>(j.index) >= g_records.size())
                return;
            const addresses::record& r = g_records[static_cast<std::size_t>(j.index)];
            module = r.module;
            spec = r.spec;
            rva = r.rva;
            state = r.state;
        }
        const bool found = state == addresses::status::unique || state == addresses::status::ambiguous;
        if (j.kind == job_kind::verify)
        {
            const int matches = verify_entry(module, spec);
            std::lock_guard lock(g_lock);
            addresses::record& r = g_records[static_cast<std::size_t>(j.index)];
            if (matches < 0)
                return;
            r.matches = matches;
            if (r.state == addresses::status::unique || r.state == addresses::status::ambiguous)
                r.state = matches == 0 ? addresses::status::missing : matches == 1 ? addresses::status::unique : addresses::status::ambiguous;
            return;
        }
        std::string generated = found && rva ? generate_entry(module, spec, rva) : std::string();
        std::lock_guard lock(g_lock);
        g_records[static_cast<std::size_t>(j.index)].generated = generated.empty() ? "-" : std::move(generated);
    }

    void loop()
    {
        for (;;)
        {
            job j;
            {
                std::unique_lock lock(g_lock);
                g_wake.wait(lock, [] { return g_stop.load() || !g_jobs.empty(); });
                if (g_stop)
                    return;
                j = std::move(g_jobs.front());
                g_jobs.pop_front();
                ++g_running;
            }
            run(j);
            --g_running;
        }
    }

    void push(job j)
    {
        std::lock_guard lock(g_lock);
        if (g_stop)
            return;
        if (!g_worker.joinable())
            g_worker = std::thread(loop);
        g_jobs.push_back(std::move(j));
        g_wake.notify_one();
    }

    std::string export_path()
    {
        char buffer[MAX_PATH]{};
        const DWORD n = GetEnvironmentVariableA("APPDATA", buffer, MAX_PATH);
        if (!n || n >= MAX_PATH)
            return {};
        const std::string dir = std::string(buffer) + "\\Internal-cs2";
        CreateDirectoryA(dir.c_str(), nullptr);
        return dir + "\\patterns.txt";
    }

    const char* status_name(addresses::status s)
    {
        switch (s)
        {
        case addresses::status::missing:
            return "missing";
        case addresses::status::unique:
            return "unique";
        case addresses::status::ambiguous:
            return "ambiguous";
        default:
            return "pending";
        }
    }

    std::uintptr_t read_pointer(std::uintptr_t address)
    {
        return address && memory::is_readable(address) ? memory::read<std::uintptr_t>(address) : 0;
    }

    std::uintptr_t call_getter(std::uintptr_t function)
    {
        __try
        {
            return memory::call<std::uintptr_t>(function);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    std::uintptr_t call_view_matrix(std::uintptr_t function)
    {
        __try
        {
            return memory::call<std::uintptr_t>(function, 0);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }
}

namespace addresses
{
    std::uintptr_t resolve(const entry& e)
    {
        if (const std::uintptr_t v = e.cached.load(std::memory_order_acquire))
            return v;
        registry& reg = get_registry();
        const auto it = reg.index.find(&e);
        const std::size_t index = it != reg.index.end() ? it->second : npos;
        slot* s = index != npos ? &reg.slots[index] : nullptr;
        if (e.attempted.load(std::memory_order_acquire))
        {
            if (!s || !s->retryable.load(std::memory_order_acquire))
                return 0;
            const std::uint64_t now = GetTickCount64();
            std::uint64_t at = s->retry_at.load(std::memory_order_relaxed);
            if (now < at || !s->retry_at.compare_exchange_strong(at, now + retry_interval_ms))
                return 0;
            std::unique_lock lock(g_resolve_lock, std::try_to_lock);
            if (!lock.owns_lock())
                return 0;
            if (const std::uintptr_t v = e.cached.load(std::memory_order_acquire))
                return v;
            return attempt(e, s, index);
        }
        std::lock_guard lock(g_resolve_lock);
        if (const std::uintptr_t v = e.cached.load(std::memory_order_acquire))
            return v;
        if (e.attempted.load(std::memory_order_acquire))
            return 0;
        return attempt(e, s, index);
    }

    void resolve_all()
    {
        for (const entry* e : patterns::all())
            if (e)
                resolve(*e);
        int unique = 0, ambiguous = 0, missing = 0, waiting = 0, pending = 0;
        for (const record& r : snapshot())
        {
            switch (r.state)
            {
            case status::unique:
                ++unique;
                break;
            case status::ambiguous:
                ++ambiguous;
                break;
            case status::missing:
                ++missing;
                break;
            default:
                ++pending;
                break;
            }
            if ((r.state == status::unique || r.state == status::ambiguous) && !r.address)
                ++waiting;
        }
        logs::Add(missing ? logs::Warning : logs::Success, "Patterns: %d unique, %d ambiguous, %d missing, %d waiting, %d pending", unique, ambiguous, missing, waiting, pending);
    }

    std::vector<record> snapshot()
    {
        std::lock_guard lock(g_lock);
        ensure_records_locked();
        return g_records;
    }

    void verify()
    {
        const int n = static_cast<int>(snapshot().size());
        for (int i = 0; i < n; ++i)
            push({ job_kind::verify, i, {}, 0 });
    }

    void generate(int index)
    {
        push({ job_kind::generate, index, {}, 0 });
    }

    void generate_at(const char* module, std::uint32_t rva)
    {
        if (!module || !*module)
            return;
        {
            std::lock_guard lock(g_lock);
            if (g_stop)
                return;
            g_custom_pending = true;
            g_custom.clear();
        }
        push({ job_kind::custom, -1, module, rva });
    }

    std::string generated_custom(bool* pending)
    {
        std::lock_guard lock(g_lock);
        if (pending)
            *pending = g_custom_pending;
        return g_custom;
    }

    bool busy()
    {
        std::lock_guard lock(g_lock);
        return !g_jobs.empty() || g_running.load() > 0;
    }

    bool export_list()
    {
        const std::string path = export_path();
        if (path.empty())
            return false;
        FILE* f = nullptr;
        if (fopen_s(&f, path.c_str(), "w") || !f)
        {
            logs::Add(logs::Error, "Patterns: cannot write %s", path.c_str());
            return false;
        }
        for (const record& r : snapshot())
        {
            std::fprintf(f, "[%s]\nspec = %s\nmodule = %s\nrva = 0x%X\naddress = 0x%llX\nstatus = %s\nmatches = %d\n", r.name.c_str(), r.spec.c_str(), r.module.c_str(), r.rva, static_cast<unsigned long long>(r.address), status_name(r.state), r.matches);
            if (!r.generated.empty())
                std::fprintf(f, "generated = %s\n", r.generated.c_str());
            std::fputc('\n', f);
        }
        std::fclose(f);
        logs::Add(logs::Success, "Patterns exported to %s", path.c_str());
        return true;
    }

    void shutdown()
    {
        {
            std::lock_guard lock(g_lock);
            g_stop = true;
            g_jobs.clear();
            g_custom_pending = false;
        }
        g_wake.notify_all();
        if (g_worker.joinable())
            g_worker.join();
        g_images.clear();
    }

    namespace globals
    {
        std::uintptr_t schema_system()
        {
            return INTERFACE_("schemasystem.dll:SchemaSystem_001");
        }

        std::uintptr_t cvar()
        {
            return INTERFACE_("tier0.dll:VEngineCvar007");
        }

        std::uintptr_t localize()
        {
            return INTERFACE_("localize.dll:Localize_001");
        }

        std::uintptr_t engine_client()
        {
            return INTERFACE_("engine2.dll:Source2EngineToClient001");
        }

        std::uintptr_t source2_client()
        {
            return INTERFACE_("client.dll:Source2Client002");
        }

        std::uintptr_t entity_system()
        {
            if (const std::uintptr_t system = read_pointer(PATTERN(patterns::entity_list_legacy)))
                return system;
            return read_pointer(PATTERN(patterns::game_entity_system));
        }

        std::uintptr_t global_vars()
        {
            return read_pointer(PATTERN(patterns::global_vars));
        }

        std::uintptr_t csgo_input()
        {
            return PATTERN(patterns::csgo_input);
        }

        std::uintptr_t item_system()
        {
            static std::atomic<std::uintptr_t> value{ 0 };
            std::uintptr_t v = value.load(std::memory_order_acquire);
            if (v)
                return v;
            const std::uintptr_t function = PATTERN(patterns::item_system);
            if (!function)
                return 0;
            v = call_getter(function);
            if (v && memory::is_readable(v))
                value.store(v, std::memory_order_release);
            else
                v = 0;
            return v;
        }

        std::uintptr_t trace_manager()
        {
            if (const std::uintptr_t manager = read_pointer(PATTERN(patterns::game_trace_manager_legacy)))
                return manager;
            return PATTERN(patterns::game_trace_manager);
        }

        std::uintptr_t view_matrix()
        {
            if (const std::uintptr_t matrix = PATTERN(patterns::view_matrix))
                return matrix;
            static std::atomic<std::uintptr_t> value{ 0 };
            std::uintptr_t v = value.load(std::memory_order_acquire);
            if (v)
                return v;
            const std::uintptr_t function = PATTERN(patterns::view_matrix_fn);
            if (!function)
                return 0;
            v = call_view_matrix(function);
            if (v && memory::is_readable(v, sizeof(float) * 16))
                value.store(v, std::memory_order_release);
            else
                v = 0;
            return v;
        }
    }
}
