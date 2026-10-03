#include "icons.h"
#include "vpk.h"
#include <Windows.h>
#include <d3d11.h>
#include <wincodec.h>
#include <unordered_map>
#include <vector>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <algorithm>
#include <cstring>

namespace
{
    struct Image
    {
        std::string path;
        int w = 0, h = 0;
        DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
        UINT pitch = 0;
        std::vector<uint8_t> pixels;
        bool ok = false;
    };

    struct Entry
    {
        ID3D11ShaderResourceView* srv = nullptr;
        uint64_t used = 0;
        bool pending = false;
        bool failed = false;
    };

    ID3D11Device* device = nullptr;
    std::unordered_map<std::string, Entry> cache;
    std::mutex lock;
    std::condition_variable wake;
    std::deque<std::string> requests;
    std::vector<Image> done;
    std::thread worker;
    std::atomic<bool> running{ false };
    IWICImagingFactory* wic = nullptr;
    uint64_t frame = 0;
    constexpr size_t kLimit = 400;
    constexpr int kTarget = 160;

    bool Lz4(const uint8_t* src, size_t srcLen, uint8_t* dst, size_t dstLen)
    {
        const uint8_t* ip = src;
        const uint8_t* iend = src + srcLen;
        uint8_t* op = dst;
        uint8_t* oend = dst + dstLen;
        while (ip < iend)
        {
            const unsigned token = *ip++;
            size_t lit = token >> 4;
            if (lit == 15)
            {
                uint8_t b;
                do
                {
                    if (ip >= iend)
                        return false;
                    b = *ip++;
                    lit += b;
                } while (b == 255);
            }
            if (static_cast<size_t>(iend - ip) < lit || static_cast<size_t>(oend - op) < lit)
                return false;
            memcpy(op, ip, lit);
            op += lit;
            ip += lit;
            if (ip >= iend)
                break;
            if (iend - ip < 2)
                return false;
            const size_t offset = ip[0] | ip[1] << 8;
            ip += 2;
            if (!offset || offset > static_cast<size_t>(op - dst))
                return false;
            size_t len = token & 15;
            if (len == 15)
            {
                uint8_t b;
                do
                {
                    if (ip >= iend)
                        return false;
                    b = *ip++;
                    len += b;
                } while (b == 255);
            }
            len += 4;
            if (static_cast<size_t>(oend - op) < len)
                return false;
            const uint8_t* match = op - offset;
            for (size_t i = 0; i < len; ++i)
                op[i] = match[i];
            op += len;
        }
        return op == oend;
    }

    void Half(Image& img)
    {
        const int nw = img.w / 2, nh = img.h / 2;
        std::vector<uint8_t> out(static_cast<size_t>(nw) * nh * 4);
        const uint8_t* src = img.pixels.data();
        const size_t stride = static_cast<size_t>(img.w) * 4;
        for (int y = 0; y < nh; ++y)
        {
            for (int x = 0; x < nw; ++x)
            {
                const uint8_t* p[4] = {
                    src + (y * 2) * stride + x * 8,
                    src + (y * 2) * stride + x * 8 + 4,
                    src + (y * 2 + 1) * stride + x * 8,
                    src + (y * 2 + 1) * stride + x * 8 + 4,
                };
                unsigned a = 0, c[3] = {};
                for (auto q : p)
                {
                    a += q[3];
                    for (int k = 0; k < 3; ++k)
                        c[k] += q[k] * q[3];
                }
                uint8_t* d = &out[(static_cast<size_t>(y) * nw + x) * 4];
                for (int k = 0; k < 3; ++k)
                    d[k] = a ? static_cast<uint8_t>(c[k] / a) : 0;
                d[3] = static_cast<uint8_t>(a / 4);
            }
        }
        img.pixels.swap(out);
        img.w = nw;
        img.h = nh;
    }

    bool DecodePng(const uint8_t* data, size_t size, Image& img)
    {
        if (!wic)
            return false;
        IWICStream* stream = nullptr;
        IWICBitmapDecoder* decoder = nullptr;
        IWICBitmapFrameDecode* frameDecode = nullptr;
        IWICFormatConverter* converter = nullptr;
        bool ok = false;
        UINT w = 0, h = 0;
        if (SUCCEEDED(wic->CreateStream(&stream)) &&
            SUCCEEDED(stream->InitializeFromMemory(const_cast<BYTE*>(data), static_cast<DWORD>(size))) &&
            SUCCEEDED(wic->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand, &decoder)) &&
            SUCCEEDED(decoder->GetFrame(0, &frameDecode)) &&
            SUCCEEDED(wic->CreateFormatConverter(&converter)) &&
            SUCCEEDED(converter->Initialize(frameDecode, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)) &&
            SUCCEEDED(converter->GetSize(&w, &h)) && w && h && w <= 4096 && h <= 4096)
        {
            img.pixels.resize(static_cast<size_t>(w) * h * 4);
            ok = SUCCEEDED(converter->CopyPixels(nullptr, w * 4, static_cast<UINT>(img.pixels.size()), img.pixels.data()));
            img.w = static_cast<int>(w);
            img.h = static_cast<int>(h);
        }
        if (converter)
            converter->Release();
        if (frameDecode)
            frameDecode->Release();
        if (decoder)
            decoder->Release();
        if (stream)
            stream->Release();
        return ok;
    }

    bool Decode(const std::vector<uint8_t>& f, Image& img)
    {
        auto u16 = [&](size_t o) { uint16_t v; memcpy(&v, &f[o], 2); return v; };
        auto u32 = [&](size_t o) { uint32_t v; memcpy(&v, &f[o], 4); return v; };
        if (f.size() < 16)
            return false;
        const uint32_t fileSize = u32(0);
        const uint32_t blockOffset = u32(8), blockCount = u32(12);
        size_t dataPos = 0;
        for (uint32_t i = 0; i < blockCount && i < 16; ++i)
        {
            const size_t p = 8 + static_cast<size_t>(blockOffset) + i * 12;
            if (p + 12 > f.size())
                return false;
            if (!memcmp(&f[p], "DATA", 4))
                dataPos = p + 4 + u32(p + 4);
        }
        if (!dataPos || dataPos + 40 > f.size() || fileSize > f.size())
            return false;
        int w = u16(dataPos + 20), h = u16(dataPos + 22);
        const uint8_t format = f[dataPos + 26];
        const int mips = std::max<int>(1, f[dataPos + 27]);
        const size_t extraPos = dataPos + 32 + u32(dataPos + 32);
        const uint32_t extraCount = u32(dataPos + 36);
        std::vector<uint32_t> sizes;
        bool compressed = false;
        for (uint32_t i = 0; i < extraCount && i < 16; ++i)
        {
            const size_t p = extraPos + i * 12;
            if (p + 12 > f.size())
                return false;
            if (u32(p) != 4)
                continue;
            const size_t q = p + 4 + u32(p + 4);
            if (q + 12 > f.size())
                return false;
            compressed = u32(q) == 1;
            const uint32_t count = u32(q + 8);
            const size_t arr = q + 4 + u32(q + 4);
            if (count == static_cast<uint32_t>(mips) && arr + count * 4 <= f.size())
                for (uint32_t k = 0; k < count; ++k)
                    sizes.push_back(u32(arr + k * 4));
        }

        size_t pos = fileSize;
        if (format == 16)
        {
            if (!DecodePng(f.data() + pos, f.size() - pos, img))
                return false;
            img.format = DXGI_FORMAT_B8G8R8A8_UNORM;
        }
        else
        {
            size_t block = format == 1 ? 8 : format == 2 ? 16 : 0;
            if (format != 28 && !block)
                return false;
            auto bytes = [&](int level) {
                const size_t mw = std::max(1, w >> level), mh = std::max(1, h >> level);
                return block ? ((mw + 3) / 4) * ((mh + 3) / 4) * block : mw * mh * 4;
            };
            int level = 0;
            while (level + 1 < mips && (w >> level) > 256)
                ++level;
            for (int m = mips - 1; m > level; --m)
                pos += sizes.empty() ? bytes(m) : sizes[m];
            const size_t need = bytes(level);
            const size_t have = sizes.empty() ? need : sizes[level];
            if (pos + have > f.size())
                return false;
            img.pixels.resize(need);
            if (compressed && have < need)
            {
                if (!Lz4(&f[pos], have, img.pixels.data(), need))
                    return false;
            }
            else
                memcpy(img.pixels.data(), &f[pos], need);
            img.w = std::max(1, w >> level);
            img.h = std::max(1, h >> level);
            if (block)
            {
                if (img.w % 4 || img.h % 4)
                    return false;
                img.format = format == 1 ? DXGI_FORMAT_BC1_UNORM : DXGI_FORMAT_BC3_UNORM;
                img.pitch = static_cast<UINT>(((img.w + 3) / 4) * block);
                return true;
            }
            img.format = DXGI_FORMAT_B8G8R8A8_UNORM;
        }
        while (img.w > kTarget && img.w % 2 == 0 && img.h % 2 == 0)
            Half(img);
        img.pitch = static_cast<UINT>(img.w * 4);
        return true;
    }

    void Work()
    {
        const bool com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
        std::vector<uint8_t> file;
        while (true)
        {
            std::string path;
            {
                std::unique_lock l(lock);
                wake.wait(l, [] { return !requests.empty() || !running; });
                if (!running)
                    break;
                path = std::move(requests.back());
                requests.pop_back();
            }
            Image img;
            img.path = path;
            img.ok = vpk::Read(path, file) && Decode(file, img);
            if (!img.ok)
                img.pixels.clear();
            std::lock_guard l(lock);
            done.push_back(std::move(img));
        }
        if (wic)
        {
            wic->Release();
            wic = nullptr;
        }
        if (com)
            CoUninitialize();
    }

    ID3D11ShaderResourceView* Upload(const Image& img)
    {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = img.w;
        desc.Height = img.h;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = img.format;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA data{ img.pixels.data(), img.pitch, 0 };
        ID3D11Texture2D* tex = nullptr;
        if (FAILED(device->CreateTexture2D(&desc, &data, &tex)))
            return nullptr;
        ID3D11ShaderResourceView* srv = nullptr;
        device->CreateShaderResourceView(tex, nullptr, &srv);
        tex->Release();
        return srv;
    }

    void Evict()
    {
        if (cache.size() <= kLimit)
            return;
        std::vector<std::pair<uint64_t, const std::string*>> old;
        for (auto& [k, e] : cache)
            if (!e.pending && e.used + 2 < frame)
                old.emplace_back(e.used, &k);
        std::sort(old.begin(), old.end());
        size_t remove = cache.size() - kLimit * 3 / 4;
        std::vector<std::string> keys;
        for (size_t i = 0; i < old.size() && i < remove; ++i)
            keys.push_back(*old[i].second);
        for (auto& k : keys)
        {
            auto it = cache.find(k);
            if (it->second.srv)
                it->second.srv->Release();
            cache.erase(it);
        }
    }
}

void icons::Init(ID3D11Device* dev)
{
    device = dev;
    running = true;
    worker = std::thread(Work);
}

void icons::Shutdown()
{
    {
        std::lock_guard l(lock);
        running = false;
        requests.clear();
    }
    wake.notify_all();
    if (worker.joinable())
        worker.join();
    for (auto& [k, e] : cache)
        if (e.srv)
            e.srv->Release();
    cache.clear();
    done.clear();
}

void icons::Frame()
{
    ++frame;
    std::vector<Image> ready;
    {
        std::lock_guard l(lock);
        const size_t n = std::min<size_t>(done.size(), 24);
        for (size_t i = 0; i < n; ++i)
        {
            ready.push_back(std::move(done.back()));
            done.pop_back();
        }
    }
    for (auto& img : ready)
    {
        auto it = cache.find(img.path);
        if (it == cache.end())
            continue;
        it->second.pending = false;
        it->second.srv = img.ok ? Upload(img) : nullptr;
        it->second.failed = !it->second.srv;
    }
    Evict();
}

uint64_t icons::Get(const std::string& path)
{
    if (!device || path.empty())
        return 0;
    Entry& e = cache[path];
    e.used = frame;
    if (e.srv)
        return reinterpret_cast<uint64_t>(e.srv);
    if (!e.pending && !e.failed)
    {
        e.pending = true;
        {
            std::lock_guard l(lock);
            requests.push_back(path);
        }
        wake.notify_one();
    }
    return 0;
}
