#include "vpk.h"
#include <Windows.h>
#include <unordered_map>
#include <mutex>
#include <cstring>

namespace
{
    struct Entry
    {
        uint32_t offset;
        uint32_t length;
        uint32_t preload;
        uint16_t preloadSize;
        uint16_t archive;
    };

    std::wstring base;
    uint32_t dataOffset = 0;
    std::unordered_map<std::string, Entry> entries;
    std::unordered_map<std::string, std::vector<std::string>> dirs;
    std::unordered_map<uint16_t, HANDLE> files;
    std::mutex lock;
    const std::vector<std::string> empty;

    bool Wanted(const std::string& dir)
    {
        return !dir.compare(0, 21, "panorama/images/econ/") || dir == "scripts/items" || dir == "resource";
    }

    HANDLE File(uint16_t archive)
    {
        auto it = files.find(archive);
        if (it != files.end())
            return it->second;
        wchar_t name[16];
        if (archive == 0x7fff)
            wcscpy_s(name, L"dir.vpk");
        else
            swprintf_s(name, L"%03u.vpk", archive);
        HANDLE h = CreateFileW((base + name).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        files[archive] = h;
        return h;
    }

    bool ReadAt(HANDLE h, uint64_t offset, void* dst, uint32_t size)
    {
        if (h == INVALID_HANDLE_VALUE)
            return false;
        LARGE_INTEGER li;
        li.QuadPart = static_cast<LONGLONG>(offset);
        DWORD read = 0;
        return SetFilePointerEx(h, li, nullptr, FILE_BEGIN) && ReadFile(h, dst, size, &read, nullptr) && read == size;
    }
}

bool vpk::Open(const std::wstring& dirFile)
{
    Close();
    std::lock_guard l(lock);
    const size_t cut = dirFile.rfind(L"dir.vpk");
    if (cut == std::wstring::npos)
        return false;
    base = dirFile.substr(0, cut);

    HANDLE h = CreateFileW(dirFile.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    LARGE_INTEGER size{};
    GetFileSizeEx(h, &size);
    std::vector<char> buf(static_cast<size_t>(size.QuadPart));
    DWORD read = 0;
    const bool ok = ReadFile(h, buf.data(), static_cast<DWORD>(buf.size()), &read, nullptr) && read == buf.size();
    files[0x7fff] = h;
    if (!ok || buf.size() < 28)
        return false;

    uint32_t header[7];
    memcpy(header, buf.data(), sizeof(header));
    if (header[0] != 0x55aa1234)
        return false;
    const uint32_t headerSize = header[1] == 1 ? 12 : 28;
    dataOffset = headerSize + header[2];

    const char* p = buf.data() + headerSize;
    const char* end = buf.data() + buf.size();
    auto str = [&]() -> const char* {
        const char* s = p;
        while (p < end && *p)
            ++p;
        ++p;
        return s;
    };

    while (p < end)
    {
        std::string ext = str();
        if (ext.empty())
            break;
        while (p < end)
        {
            std::string dir = str();
            if (dir.empty())
                break;
            const bool wanted = Wanted(dir);
            std::vector<std::string>* list = wanted ? &dirs[dir] : nullptr;
            while (p < end)
            {
                const char* name = str();
                if (!*name)
                    break;
                if (p + 18 > end)
                    return false;
                Entry e{};
                uint16_t preloadSize, archive;
                uint32_t offset, length;
                memcpy(&preloadSize, p + 4, 2);
                memcpy(&archive, p + 6, 2);
                memcpy(&offset, p + 8, 4);
                memcpy(&length, p + 12, 4);
                p += 18;
                e.preload = static_cast<uint32_t>(p - buf.data());
                e.preloadSize = preloadSize;
                e.archive = archive;
                e.offset = offset;
                e.length = length;
                p += preloadSize;
                if (!wanted)
                    continue;
                std::string file = std::string(name) + (ext == " " ? "" : "." + ext);
                list->push_back(file);
                entries.emplace(dir + "/" + file, e);
            }
        }
    }
    return true;
}

void vpk::Close()
{
    std::lock_guard l(lock);
    for (auto& [k, h] : files)
        if (h != INVALID_HANDLE_VALUE)
            CloseHandle(h);
    files.clear();
    entries.clear();
    dirs.clear();
}

bool vpk::Read(const std::string& path, std::vector<uint8_t>& out)
{
    std::lock_guard l(lock);
    auto it = entries.find(path);
    if (it == entries.end())
        return false;
    const Entry& e = it->second;
    out.resize(static_cast<size_t>(e.preloadSize) + e.length);
    if (e.preloadSize && !ReadAt(File(0x7fff), e.preload, out.data(), e.preloadSize))
        return false;
    if (!e.length)
        return true;
    const uint64_t offset = e.archive == 0x7fff ? static_cast<uint64_t>(dataOffset) + e.offset : e.offset;
    return ReadAt(File(e.archive), offset, out.data() + e.preloadSize, e.length);
}

const std::vector<std::string>& vpk::List(const std::string& dir)
{
    auto it = dirs.find(dir);
    return it == dirs.end() ? empty : it->second;
}
