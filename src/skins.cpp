#include "skins.h"
#include "game.h"
#include "items.h"
#include "mem.h"
#include "core/log.h"
#include <Windows.h>
#include <mutex>
#include <atomic>
#include <string>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <algorithm>

namespace
{
    std::mutex lock;
    std::vector<skins::Entry> entries;
    uint32_t nextUid = 1;
    uint64_t nextId = 0;
    bool dirty = false;
    ULONGLONG inventorySeen = 0;
    ULONGLONG agentTime = 0;
    void* agentPawn = nullptr;
    int agentDef = 0;
    std::string agentModel;
    uint32_t activeHandle = 0;
    std::atomic<bool> refresh{ false };
    int validateTick = 0;
    struct Regen
    {
        ULONGLONG last = 0;
        bool pending = false;
        bool reload = false;
        uint64_t mask = 0;
        void* attach = nullptr;
    };
    std::unordered_map<void*, Regen> regens;
    std::unordered_map<void*, int> attachModels;
    std::unordered_map<void*, uint32_t> originalSubclass;
    std::unordered_map<int, uint64_t> originalLoadout;
    std::unordered_map<void*, int> regenCount;

    std::string Path()
    {
        char buf[MAX_PATH]{};
        GetEnvironmentVariableA("APPDATA", buf, MAX_PATH);
        std::string dir = std::string(buf) + "\\Internal-cs2";
        CreateDirectoryA(dir.c_str(), nullptr);
        return dir + "\\inventory.txt";
    }

    void Save()
    {
        FILE* f = nullptr;
        if (fopen_s(&f, Path().c_str(), "wb") || !f)
            return;
        for (auto& e : entries)
            if (!e.removed)
                fprintf(f, "%d %d %.6f %d %d %d %d %d %d %d %d %s\n", e.def, e.paint, e.wear, e.seed, e.stattrak, e.equip,
                    e.stickers[0], e.stickers[1], e.stickers[2], e.stickers[3], e.stickers[4], e.tag);
        fclose(f);
    }

    uint32_t Bits(float v)
    {
        uint32_t b;
        memcpy(&b, &v, 4);
        return b;
    }

    int Group(int def)
    {
        auto it = items::Find(def);
        if (!it)
            return def;
        switch (it->category)
        {
        case items::Knife: return -1;
        case items::Glove: return -2;
        case items::Agent: return -3;
        default: return def;
        }
    }

    void Exclusive(const skins::Entry& e)
    {
        const int group = Group(e.def);
        for (auto& o : entries)
            if (o.uid != e.uid && Group(o.def) == group)
                o.equip &= ~e.equip;
    }

    bool Taken(const skins::Entry& e, int slot, uint8_t bit)
    {
        for (auto& o : entries)
        {
            if (o.uid == e.uid || o.removed || !o.econ || !(o.equip & bit))
                continue;
            auto it = items::Find(o.def);
            if (it && it->slot == slot)
                return true;
        }
        return false;
    }

    bool InCache(void* cache, void* object)
    {
        for (int i = 0, n = game::CacheCount(cache); i < n; ++i)
            if (game::CacheObject(cache, i) == object)
                return true;
        return false;
    }

    void Materialize(skins::Entry& e, void* inv, void* cache)
    {
        const items::Item* item = items::Find(e.def);
        if (!item)
            return;
        uint64_t maxId = 0;
        uint32_t maxPos = 0;
        for (int i = 0, n = game::CacheCount(cache); i < n; ++i)
        {
            void* o = game::CacheObject(cache, i);
            if (!o)
                continue;
            const uint64_t id = mem::At<uint64_t>(o, 0x10);
            if (id >> 60)
                continue;
            maxId = std::max(maxId, id);
            const uint32_t pos = mem::At<uint32_t>(o, 0x2c);
            if (!(pos & 0xC0000000))
                maxPos = std::max(maxPos, pos);
        }
        nextId = std::max(nextId, maxId + 1);

        void* econ = game::CreateItem();
        if (!econ)
            return;
        const bool weapon = item->category <= items::Heavy;
        const bool stattrak = e.stattrak >= 0 && (weapon || item->category == items::Knife);
        mem::At<uint64_t>(econ, 0x10) = nextId;
        mem::At<uint64_t>(econ, 0x18) = nextId;
        mem::At<uint32_t>(econ, 0x28) = static_cast<uint32_t>(game::Owner(inv));
        mem::At<uint32_t>(econ, 0x2c) = maxPos + 1;
        mem::At<uint16_t>(econ, 0x30) = static_cast<uint16_t>(e.def);
        uint16_t& bits = mem::At<uint16_t>(econ, 0x32);
        const int quality = items::Quality(*item, stattrak);
        const int rarity = items::Rarity(*item, e.paint);
        bits = static_cast<uint16_t>((bits & 0x1F) | (quality & 0xF) << 5 | 1 << 9 | (rarity & 0xF) << 11);

        if (e.paint > 0)
        {
            game::SetAttribute(econ, 6, Bits(static_cast<float>(e.paint)));
            game::SetAttribute(econ, 7, Bits(static_cast<float>(e.seed)));
            game::SetAttribute(econ, 8, Bits(e.wear));
        }
        if (stattrak)
        {
            game::SetAttribute(econ, 80, static_cast<uint32_t>(e.stattrak));
            game::SetAttribute(econ, 81, 0);
        }
        if (weapon)
            for (int i = 0; i < 5; ++i)
                if (e.stickers[i] > 0)
                {
                    game::SetAttribute(econ, 113 + i * 4, static_cast<uint32_t>(e.stickers[i]));
                    game::SetAttribute(econ, 114 + i * 4, Bits(0.f));
                }
        if (e.tag[0] && (weapon || item->category == items::Knife))
            game::SetAttributeString(econ, 111, e.tag);

        if (!game::AddItem(inv, econ))
            return;
        e.econ = econ;
        e.id = nextId++;
        e.applied = 0;
    }

    bool Ours(uint64_t id)
    {
        if (!id)
            return false;
        for (auto& o : entries)
            if (o.id == id)
                return true;
        return false;
    }

    bool SlotAccepts(const skins::Entry& e, const items::Item& item, int team)
    {
        if (item.category == items::Knife || item.category == items::Glove || item.category == items::Agent)
            return true;
        void* current = game::LoadoutItem(team, item.slot);
        const int def = game::ViewDef(current);
        return !def || def == e.def || Ours(game::ViewItemId(current));
    }

    void RememberSlot(int team, int slot)
    {
        const int key = team * 64 + slot;
        if (originalLoadout.count(key))
            return;
        const uint64_t id = game::ViewItemId(game::LoadoutItem(team, slot));
        originalLoadout[key] = Ours(id) ? 0 : id;
    }

    void RestoreSlot(int team, int slot)
    {
        const int key = team * 64 + slot;
        auto it = originalLoadout.find(key);
        game::Equip(team, slot, it != originalLoadout.end() ? it->second : 0);
        if (it != originalLoadout.end())
            originalLoadout.erase(it);
    }

    void ApplyEquip(skins::Entry& e)
    {
        const items::Item* item = items::Find(e.def);
        if (!item)
            return;
        for (int team = 2; team <= 3; ++team)
        {
            const uint8_t bit = team == 2 ? 1 : 2;
            if ((e.equip & bit) && !(e.applied & bit))
            {
                if (SlotAccepts(e, *item, team))
                {
                    RememberSlot(team, item->slot);
                    game::Equip(team, item->slot, e.id);
                    e.blocked &= ~bit;
                }
                else if (!(e.blocked & bit))
                {
                    e.blocked |= bit;
                    logs::Add(logs::Warning, "%s: other weapon in loadout slot (%s), loadout kept", items::Name(item->name), team == 2 ? "T" : "CT");
                }
            }
            else if (!(e.equip & bit) && (e.applied & bit))
            {
                if (!(e.blocked & bit) && !Taken(e, item->slot, bit))
                    RestoreSlot(team, item->slot);
                e.blocked &= ~bit;
            }
        }
        e.applied = e.equip;
    }

    void Sync()
    {
        void* inv = game::Inventory();
        void* cache = inv && game::Owner(inv) ? game::ItemCache(inv) : nullptr;
        if (!cache)
        {
            skins::inventoryReady = false;
            inventorySeen = 0;
            for (auto& e : entries)
            {
                e.econ = nullptr;
                e.applied = 0;
            }
            return;
        }
        const ULONGLONG now = GetTickCount64();
        if (!inventorySeen)
            inventorySeen = now;
        if (!game::CacheCount(cache) && now - inventorySeen < 8000)
            return;
        skins::inventoryReady = true;

        if (++validateTick >= 64)
        {
            validateTick = 0;
            for (auto& e : entries)
            {
                if (e.econ && !InCache(cache, e.econ))
                {
                    e.econ = nullptr;
                    e.applied = 0;
                }
                else if (e.blocked)
                    e.applied &= ~e.blocked;
            }
        }

        for (auto it = entries.begin(); it != entries.end();)
        {
            auto& e = *it;
            if (e.removed)
            {
                if (e.econ)
                    game::RemoveItem(inv, e.econ);
                it = entries.erase(it);
                continue;
            }
            if (!e.econ)
                Materialize(e, inv, cache);
            if (e.econ && e.applied != e.equip)
                ApplyEquip(e);
            ++it;
        }
        if (dirty)
        {
            Save();
            dirty = false;
        }
    }

    void WriteView(uint8_t* view, const skins::Entry& e, const items::Item& item, uint64_t steam)
    {
        const auto& o = game::off;
        mem::At<uint16_t>(view, o.defIndex) = static_cast<uint16_t>(e.def);
        mem::At<uint64_t>(view, o.itemId) = e.id;
        mem::At<uint32_t>(view, o.itemIdHigh) = static_cast<uint32_t>(e.id >> 32);
        mem::At<uint32_t>(view, o.itemIdLow) = static_cast<uint32_t>(e.id);
        mem::At<uint32_t>(view, o.accountId) = static_cast<uint32_t>(steam);
        mem::At<int>(view, o.quality) = items::Quality(item, e.stattrak >= 0);
        mem::At<bool>(view, o.disallowSoc) = false;
        mem::At<bool>(view, o.initialized) = true;
    }

    void WriteFallback(void* weapon, uint8_t* view, const skins::Entry& e)
    {
        const auto& o = game::off;
        if (!o.fallbackPaint)
            return;
        mem::At<int>(weapon, o.fallbackPaint) = e.paint;
        mem::At<int>(weapon, o.fallbackSeed) = e.seed;
        mem::At<float>(weapon, o.fallbackWear) = e.wear;
        mem::At<int>(weapon, o.fallbackStatTrak) = e.stattrak >= 0 ? e.stattrak : -1;
        if (skins::paintMode == 1)
            mem::At<uint32_t>(view, o.itemIdHigh) = 0xFFFFFFFF;
    }

    void Regenerate(void* weapon)
    {
        game::RegenerateSkin(weapon);
        game::RegenerateViewmodel(weapon);
        if (regenCount.size() > 256)
            regenCount.clear();
        ++regenCount[weapon];
    }

    void FillDebug(void* weapon, uint8_t* view, const skins::Entry* e, bool knife)
    {
        const auto& o = game::off;
        skins::Debug d;
        d.valid = true;
        d.def = mem::At<uint16_t>(view, o.defIndex);
        d.itemId = mem::At<uint64_t>(view, o.itemId);
        d.paint = e ? e->paint : 0;
        d.fallbackPaint = o.fallbackPaint ? mem::At<int>(weapon, o.fallbackPaint) : 0;
        const items::Paint* p = e ? items::FindPaint(e->paint) : nullptr;
        d.legacy = p && p->legacy;
        d.knife = knife;
        d.mask = game::MeshGroupMask(weapon);
        void* attach = game::ViewmodelAttachment(weapon);
        d.attach = attach != nullptr;
        d.attachMask = attach ? game::MeshGroupMask(attach) : 0;
        auto rc = regenCount.find(weapon);
        d.regenerations = rc != regenCount.end() ? rc->second : 0;
        if (const char* m = game::ModelName(attach ? attach : weapon))
            strncpy_s(d.model, m, _TRUNCATE);
        skins::debug = d;
    }

    void ApplyWeapon(void* weapon, uint8_t* view, const skins::Entry& e, bool knife, uint64_t steam, bool force)
    {
        const auto& o = game::off;
        const items::Item* item = items::Find(e.def);
        if (!item)
            return;
        bool changed = force || mem::At<uint64_t>(view, o.itemId) != e.id || mem::At<uint16_t>(view, o.defIndex) != e.def;
        if (o.fallbackPaint && mem::At<int>(weapon, o.fallbackPaint) != e.paint)
            changed = true;
        if (knife)
        {
            if (originalSubclass.size() > 256)
                originalSubclass.clear();
            uint32_t& current = mem::At<uint32_t>(weapon, o.subclassId);
            const uint32_t original = originalSubclass.try_emplace(weapon, current).first->second;
            const uint32_t token = game::Token(item->code.c_str());
            const bool native = skins::knifeAnimations && (current == token || game::KnifeVData(weapon, item->code.c_str()));
            const uint32_t wanted = native ? token : original;
            skins::knifeMode = native ? 2 : -1;
            if (current != wanted)
            {
                current = wanted;
                game::UpdateSubclass(weapon);
                changed = true;
                if (o.attachmentDirty)
                    mem::At<bool>(weapon, o.attachmentDirty) = true;
            }
            if (changed)
                game::SetModel(weapon, item->model.c_str());
            if (void* attach = game::ViewmodelAttachment(weapon))
            {
                if (attachModels.size() > 256)
                    attachModels.clear();
                int& applied = attachModels[attach];
                if (changed || applied != e.def)
                {
                    applied = e.def;
                    game::SetModel(attach, item->model.c_str());
                    regens[weapon].pending = true;
                }
            }
        }
        if (changed)
        {
            WriteView(view, e, *item, steam);
            WriteFallback(weapon, view, e);
            if (!knife)
            {
                const items::Paint* p = items::FindPaint(e.paint);
                if (regens.size() > 256)
                    regens.clear();
                Regen& r = regens[weapon];
                r.reload = true;
                r.mask = p && p->legacy ? 2 : game::MeshGroupMask(weapon) == 2 ? 1 : 0;
            }
            regens[weapon].pending = true;
        }
        auto it = regens.find(weapon);
        if (it == regens.end())
            return;
        if (!knife)
        {
            void* attach = game::ViewmodelAttachment(weapon);
            if (attach && attach != it->second.attach)
            {
                it->second.attach = attach;
                game::RefreshModel(attach);
                if (it->second.mask)
                    game::SetMeshGroupMask(attach, it->second.mask);
                it->second.pending = true;
                it->second.last = 0;
            }
        }
        if (!it->second.pending)
            return;
        const ULONGLONG now = GetTickCount64();
        if (now - it->second.last < 250)
            return;
        it->second.last = now;
        it->second.pending = false;
        if (!knife && it->second.reload)
        {
            it->second.reload = false;
            game::RefreshModel(weapon);
            if (void* attach = game::ViewmodelAttachment(weapon))
            {
                game::RefreshModel(attach);
                it->second.attach = attach;
                if (it->second.mask)
                    game::SetMeshGroupMask(attach, it->second.mask);
            }
        }
        if (!knife && it->second.mask)
            game::SetMeshGroupMask(weapon, it->second.mask);
        Regenerate(weapon);
    }

    const skins::Entry* Equipped(uint8_t bit, int def, items::Category category)
    {
        for (auto& e : entries)
        {
            if (e.removed || !e.econ || !(e.equip & bit))
                continue;
            if (def)
            {
                if (e.def == def)
                    return &e;
                continue;
            }
            auto it = items::Find(e.def);
            if (it && it->category == category)
                return &e;
        }
        return nullptr;
    }

    void Apply()
    {
        const auto& o = game::off;
        void* inv = game::Inventory();
        if (!inv)
            return;
        const uint64_t steam = game::Owner(inv);
        void* controller = game::LocalController();
        if (!controller)
            return;
        void* pawn = game::Handle(mem::At<uint32_t>(controller, o.playerPawn));
        if (!pawn)
            return;
        const int team = mem::At<uint8_t>(pawn, o.teamNum);
        if (team != 2 && team != 3)
            return;
        const uint8_t bit = team == 2 ? 1 : 2;

        const bool force = refresh.exchange(false);
        const skins::Entry* knife = Equipped(bit, 0, items::Knife);
        if (auto services = mem::At<uint8_t*>(pawn, o.weaponServices))
        {
            const uint32_t active = mem::At<uint32_t>(services, o.activeWeapon);
            if (active != activeHandle)
            {
                activeHandle = active;
                if (void* w = game::Handle(active))
                    if (regens.count(w))
                    {
                        regens[w].pending = true;
                        regens[w].last = GetTickCount64();
                    }
            }
            const int count = mem::At<int>(services, o.myWeapons);
            auto handles = mem::At<uint32_t*>(services, o.myWeapons + 8);
            for (int i = 0; handles && i < count && i < 64; ++i)
            {
                void* weapon = game::Handle(handles[i]);
                if (!weapon)
                    continue;
                const uint64_t xuid = static_cast<uint64_t>(mem::At<uint32_t>(weapon, o.xuidHigh)) << 32 | mem::At<uint32_t>(weapon, o.xuidLow);
                if (xuid && xuid != steam)
                    continue;
                uint8_t* view = static_cast<uint8_t*>(weapon) + o.attributeManager + o.item;
                const int def = mem::At<uint16_t>(view, o.defIndex);
                const bool isKnife = def == 42 || def == 59 || (def >= 500 && def < 600);
                const skins::Entry* e = isKnife ? knife : Equipped(bit, def, items::Pistol);
                if (e)
                    ApplyWeapon(weapon, view, *e, isKnife, steam, force);
                if (handles[i] == active)
                    FillDebug(weapon, view, e, isKnife);
            }
        }

        if (const skins::Entry* gloves = Equipped(bit, 0, items::Glove))
        {
            uint8_t* view = static_cast<uint8_t*>(pawn) + o.gloves;
            const items::Item* item = items::Find(gloves->def);
            if (item && (force || mem::At<uint64_t>(view, o.itemId) != gloves->id || mem::At<uint16_t>(view, o.defIndex) != gloves->def))
            {
                WriteView(view, *gloves, *item, steam);
                mem::At<bool>(pawn, o.reapplyGloves) = true;
            }
        }

        if (const skins::Entry* agent = Equipped(bit, 0, items::Agent))
        {
            const items::Item* item = items::Find(agent->def);
            const char* current = game::ModelName(pawn);
            const bool fresh = pawn != agentPawn || agent->def != agentDef;
            const bool reverted = current && !agentModel.empty() && _stricmp(current, agentModel.c_str());
            const ULONGLONG now = GetTickCount64();
            if (item && current && (fresh || reverted) && now - agentTime > 1000)
            {
                agentTime = now;
                agentPawn = pawn;
                agentDef = agent->def;
                if (_stricmp(current, item->model.c_str()))
                    game::SetModel(pawn, item->model.c_str());
                const char* applied = game::ModelName(pawn);
                agentModel = applied ? applied : "";
            }
        }
    }
}

void skins::Load()
{
    FILE* f = nullptr;
    if (fopen_s(&f, Path().c_str(), "rb") || !f)
        return;
    std::lock_guard l(lock);
    char line[512];
    while (fgets(line, sizeof(line), f))
    {
        Entry e;
        int equip = 0, n = 0;
        if (sscanf_s(line, "%d %d %f %d %d %d %d %d %d %d %d %n", &e.def, &e.paint, &e.wear, &e.seed, &e.stattrak, &equip,
                &e.stickers[0], &e.stickers[1], &e.stickers[2], &e.stickers[3], &e.stickers[4], &n) < 11)
            continue;
        std::string tag = line + n;
        while (!tag.empty() && (tag.back() == '\n' || tag.back() == '\r' || tag.back() == ' '))
            tag.pop_back();
        strncpy_s(e.tag, tag.c_str(), _TRUNCATE);
        e.equip = static_cast<uint8_t>(equip & 3);
        e.uid = nextUid++;
        entries.push_back(e);
    }
    fclose(f);
}

std::vector<skins::Entry> skins::Snapshot()
{
    std::lock_guard l(lock);
    std::vector<Entry> out;
    out.reserve(entries.size());
    for (auto& e : entries)
        if (!e.removed)
            out.push_back(e);
    return out;
}

void skins::Add(Entry e)
{
    std::lock_guard l(lock);
    e.uid = nextUid++;
    e.econ = nullptr;
    e.applied = 0;
    e.removed = false;
    if (auto it = items::Find(e.def))
        e.equip &= it->teams;
    Exclusive(e);
    entries.push_back(e);
    dirty = true;
    refresh = true;
}

void skins::Remove(uint32_t uid)
{
    std::lock_guard l(lock);
    for (auto& e : entries)
        if (e.uid == uid)
            e.removed = true;
    dirty = true;
    refresh = true;
}

void skins::RemoveAll()
{
    std::lock_guard l(lock);
    for (auto& e : entries)
        e.removed = true;
    dirty = true;
    refresh = true;
}

void skins::SetEquip(uint32_t uid, uint8_t mask)
{
    std::lock_guard l(lock);
    for (auto& e : entries)
    {
        if (e.uid != uid)
            continue;
        if (auto it = items::Find(e.def))
            mask &= it->teams;
        e.equip = mask;
        Exclusive(e);
    }
    dirty = true;
    refresh = true;
}

void skins::OnFrameStage(int stage)
{
    if (faulted || !game::Ready() || items::state != 1)
        return;
    std::lock_guard l(lock);
    Sync();
    if (stage == 7 && enabled)
        Apply();
}

void skins::Refresh()
{
    refresh = true;
}

void skins::Cleanup()
{
    if (!game::Ready())
        return;
    std::lock_guard l(lock);
    const auto& o = game::off;
    if (void* controller = game::LocalController())
        if (void* pawn = game::Handle(mem::At<uint32_t>(controller, o.playerPawn)))
            if (auto services = mem::At<uint8_t*>(pawn, o.weaponServices))
            {
                const int count = mem::At<int>(services, o.myWeapons);
                auto handles = mem::At<uint32_t*>(services, o.myWeapons + 8);
                for (int i = 0; handles && i < count && i < 64; ++i)
                {
                    void* weapon = game::Handle(handles[i]);
                    auto it = weapon ? originalSubclass.find(weapon) : originalSubclass.end();
                    if (it != originalSubclass.end() && it->second && mem::At<uint32_t>(weapon, o.subclassId) != it->second)
                    {
                        mem::At<uint32_t>(weapon, o.subclassId) = it->second;
                        game::UpdateSubclass(weapon);
                        if (o.attachmentDirty)
                            mem::At<bool>(weapon, o.attachmentDirty) = true;
                    }
                }
            }
    originalSubclass.clear();
    for (auto& [key, id] : originalLoadout)
        game::Equip(key / 64, key % 64, id);
    originalLoadout.clear();
    regens.clear();
    attachModels.clear();
    void* inv = game::Inventory();
    if (!inv)
        return;
    for (auto& e : entries)
    {
        if (e.econ)
            game::RemoveItem(inv, e.econ);
        e.econ = nullptr;
        e.applied = 0;
    }
}
