#include "core/patterns.h"
#include "game.h"
#include "mem.h"
#include <Windows.h>
#include <cstring>

namespace
{
    mem::Module client;
    uint8_t* entityList = nullptr;
    void* inventoryManager = nullptr;
    uint8_t* stringAttrVtable = nullptr;
    uint32_t compositeOffset = 0;
    bool ready = false;
    std::string error;

    void* (*createItem)() = nullptr;
    void (*setAttrU32)(void*, void*, const void*) = nullptr;
    void (*setAttrF32)(void*, void*, const void*) = nullptr;
    void* (*createTypeCache)(void*, int) = nullptr;
    void (*setModel)(void*, const char*) = nullptr;
    void (*setMeshGroupMask)(void*, uint64_t) = nullptr;
    void (*subclassChanged)(void*) = nullptr;
    void** vdataRegistry = nullptr;
    void* (*findVData)(void*, int, uint32_t) = nullptr;
    void* (*loadVData)(void*, int, const char*, void*) = nullptr;
    int (*kvCount)(void*) = nullptr;
    void* (*kvMember)(void*, int) = nullptr;
    const char* (*kvName)(void*, int) = nullptr;
    uint32_t vdataOffset = 0;
    float* viewMatrix = nullptr;
    void (*setViewAngles)(void*, int, const float*) = nullptr;
    int localIndex = 0;
    uint32_t attachItemOffset = 0, attachCompositeOffset = 0;
    bool (*hasCustomMaterial)(void*) = nullptr;
    void (*buildComposite)(void*, void*, bool, bool, void*) = nullptr;
    void* (*itemSchema)() = nullptr;
    void (*clearComposite)(void*, bool) = nullptr;
    void (*updateComposite)(void*, bool) = nullptr;

    bool Fail(const char* what)
    {
        error = what;
        return false;
    }

    uint32_t Field(const char* name, const char* pattern, int pos, int size)
    {
        uint8_t* p = patterns::Find(client, name, pattern);
        if (!p)
            return 0;
        return size == 1 ? p[pos] : *reinterpret_cast<uint32_t*>(p + pos);
    }

    uint8_t* Identity(int index)
    {
        if (!entityList || index < 0 || index > 0x7fff)
            return nullptr;
        auto chunks = *reinterpret_cast<uint8_t***>(entityList);
        if (!chunks)
            return nullptr;
        uint8_t* chunk = chunks[index >> 9];
        return chunk ? chunk + (index & 0x1ff) * 0x70 : nullptr;
    }

    void* AttributeDefinition(int index)
    {
        void* schema = itemSchema ? itemSchema() : nullptr;
        return schema ? mem::Call<void*, 27>(schema, index) : nullptr;
    }

    void* Skeleton(void* entity)
    {
        void* node = mem::At<void*>(entity, game::off.sceneNode);
        return node ? mem::Call<void*, 14>(node) : nullptr;
    }
}

bool game::Init()
{
    client = mem::Load("client.dll");
    if (!client)
        return Fail("client.dll");

    struct FieldSig
    {
        uint32_t* out;
        const char* name;
        const char* pattern;
        int pos;
        int size;
    };
    const FieldSig fields[] = {
        { &off.sceneNode, "C_BaseEntity::m_pGameSceneNode", "48 8B 47 10 48 89 8F ? ? ? ? 4C 8B 00 4D 85 C0", 7, 4 },
        { &off.teamNum, "C_BaseEntity::m_iTeamNum", "44 38 B7 E7 03 00 00 74 ? 44 88 B7 ? ? ? ? F3 0F 10 0D", 12, 4 },
        { &off.subclassId, "C_BaseEntity::m_nSubclassID", "88 87 79 03 00 00 39 87 80 03 00 00 74 ? 44 89 B7", 17, 4 },
        { &off.isLocalController, "CBasePlayerController::m_bIsLocalPlayerController", "8B 87 8C 07 00 00 89 87 E4 06 00 00 0F B6 87", 15, 4 },
        { &off.playerPawn, "CCSPlayerController::m_hPlayerPawn", "48 C7 83 ? ? ? ? FF FF FF FF 40 88 B3 34 09 00 00", 3, 4 },
        { &off.tickBase, "CBasePlayerController::m_nTickBase", "89 B7 ? ? ? ? C7 87 BC 06 00 00 FF FF FF FF", 2, 4 },
        { &off.flags, "C_BaseEntity::m_fFlags", "44 39 B7 F4 03 00 00 74 ? 44 89 B7 ? ? ? ? 41 B8 01 00 00 00", 12, 4 },
        { &off.health, "C_BaseEntity::m_iHealth", "41 F6 C4 02 74 ? 44 8B 8E ? ? ? ? 45 85 C9 7E", 9, 4 },
        { &off.moveType, "C_BaseEntity::m_MoveType", "48 85 C0 74 ? 80 BB ? ? ? ? 08 74 ? 40 32 F6 E9", 7, 4 },
        { &off.absVelocity, "C_BaseEntity::m_vecAbsVelocity", "48 8D 05 ? ? ? ? C6 85 F0 03 00 00 05 48 89 85", 17, 4 },
        { &off.movementServices, "C_BasePlayerPawn::m_pMovementServices", "48 89 B3 20 13 00 00 89 B3 28 13 00 00 48 89 BB", 16, 4 },
        { &off.maxSpeed, "CPlayer_MovementServices::m_flMaxspeed", "89 47 70 E9 ? ? ? ? 48 8D 95 ? ? ? ? FF 15 ? ? ? ? 8B 0D ? ? ? ? 39 08", 11, 4 },
        { &off.aimPunchServices, "C_CSPlayerPawn::m_pAimPunchServices", "4C 89 A3 88 15 00 00 4C 89 A3 90 15 00 00 4C 89 A3", 17, 4 },
        { &off.absOrigin, "CGameSceneNode::m_vecAbsOrigin", "66 89 83 07 01 00 00 F2 0F 10 05 ? ? ? ? F2 0F 11 83", 19, 4 },
        { &off.gravityScale, "C_BaseEntity::m_flGravityScale", "C7 87 ? ? ? ? 00 00 80 3F F3 0F 10 87 44 05 00 00", 2, 4 },
        { &off.weaponServices, "C_BasePlayerPawn::m_pWeaponServices", "48 89 BB E0 12 00 00 48 89 B3 E8 12 00 00 48 89 B3", 17, 4 },
        { &off.myWeapons, "CPlayer_WeaponServices::m_hMyWeapons", "44 89 64 24 30 4C 89 64 24 ? 44 89 64 24 50", 9, 1 },
        { &off.activeWeapon, "CPlayer_WeaponServices::m_hActiveWeapon", "44 88 64 24 28 48 85 C0 44 89 64 24", 12, 1 },
        { &off.gloves, "C_CSPlayerPawn::m_EconGloves", "48 8D 05 ? ? ? ? 48 89 87 ? ? ? ? 48 8D 8F 28 1D 00 00", 10, 4 },
        { &off.reapplyGloves, "C_CSPlayerPawn::m_bNeedToReApplyGloves", "49 8D 8E 70 17 00 00 E8 ? ? ? ? 41 C6 86 ? ? ? ? 01", 15, 4 },
        { &off.attributeManager, "C_EconEntity::m_AttributeManager", "C7 87 90 18 00 00 FF FF FF FF 48 8D 05 ? ? ? ? 48 89 87", 20, 4 },
        { &off.item, "C_AttributeContainer::m_Item", "48 89 47 ? 48 8B C7 48 89 B7 08 06 00 00", 3, 1 },
        { &off.xuidLow, "C_EconEntity::m_OriginalOwnerXuidLow", "40 88 B7 88 12 00 00 39 B7 A0 18 00 00 74 ? 89 B7", 17, 4 },
        { &off.viewmodelAttachment, "C_EconEntity::m_hViewmodelAttachment", "89 B7 C0 18 00 00 C7 87 ? ? ? ? FF FF FF FF", 8, 4 },
        { &off.xuidHigh, "C_EconEntity::m_OriginalOwnerXuidHigh", "89 B7 A0 18 00 00 39 B7 A4 18 00 00 74 ? 89 B7", 16, 4 },
        { &off.defIndex, "C_EconItemView::m_iItemDefinitionIndex", "66 89 B7 ? ? ? ? 83 BF BC 01 00 00 FF", 3, 4 },
        { &off.quality, "C_EconItemView::m_iEntityQuality", "C7 87 ? ? ? ? FF FF FF FF 39 B7 C0 01 00 00", 2, 4 },
        { &off.itemId, "C_EconItemView::m_iItemID", "39 B7 C0 01 00 00 74 ? 89 B7 C0 01 00 00 48 89 B7", 17, 4 },
        { &off.itemIdHigh, "C_EconItemView::m_iItemIDHigh", "48 89 B7 C8 01 00 00 39 B7 D0 01 00 00 74 ? 89 B7", 17, 4 },
        { &off.itemIdLow, "C_EconItemView::m_iItemIDLow", "89 B7 D0 01 00 00 39 B7 D4 01 00 00 74 ? 89 B7", 16, 4 },
        { &off.accountId, "C_EconItemView::m_iAccountID", "40 88 B7 E8 01 00 00 39 B7 D8 01 00 00 74 ? 89 B7", 17, 4 },
        { &off.initialized, "C_EconItemView::m_bInitialized", "89 B7 DC 01 00 00 40 38 B7 E8 01 00 00 74 ? 40 88 B7", 18, 4 },
        { &off.disallowSoc, "C_EconItemView::m_bDisallowSOC", "66 89 B7 EA 01 00 00 89 B7 F4 01 00 00 40 88 B7", 16, 4 },
        { &off.modelState, "CSkeletonInstance::m_modelState", "48 8B D8 49 8D B6 ? ? ? ? 48 85 F6 75 ? 44 88 7C 24 30", 6, 4 },
        { &off.modelName, "CModelState::m_ModelName", "4C 89 7C 24 58 E8 ? ? ? ? 48 8B D8 49 8B 8E", 16, 4 },
        { &off.meshGroupMask, "CModelState::m_MeshGroupMask", "48 89 5C 24 58 E8 ? ? ? ? 48 8B F8 49 8B 9E", 16, 4 },
    };
    for (auto& f : fields)
        if (!(*f.out = Field(f.name, f.pattern, f.pos, f.size)))
            return Fail(f.name);
    const FieldSig optional[] = {
        { &off.sanitizedName, "CCSPlayerController::m_sSanitizedPlayerName", "48 89 B3 68 08 00 00 89 B3 70 08 00 00 48 89 B3", 16, 4 },
        { &off.pawnIsAlive, "CCSPlayerController::m_bPawnIsAlive", "48 C7 83 2C 09 00 00 FF FF FF FF 40 88 B3", 14, 4 },
        { &off.collision, "C_BaseEntity::m_pCollision", "4C 89 B7 ? ? ? ? C7 87 D0 05 00 00 FF FF FF FF", 3, 4 },
        { &off.lifeState, "C_BaseEntity::m_lifeState", "66 44 89 B7 ? ? ? ? 4C 89 B7 58 03 00 00", 4, 4 },
        { &off.vecMins, "CCollisionProperty::m_vecMins", "48 8D 4E ? 4C 89 7E 18 0F 57 DB 4C 89 7E 20", 3, 1 },
        { &off.vecMaxs, "CCollisionProperty::m_vecMaxs", "48 8D 4E ? 0F 57 D2 0F 57 C9 E8 ? ? ? ? 44 89 7E 60", 3, 1 },
        { &off.dormant, "CGameSceneNode::m_bDormant", "66 89 B3 ? ? ? ? 40 88 B3 09 01 00 00", 3, 4 },
        { &off.spottedState, "C_CSPlayerPawn::m_entitySpottedState", "48 89 87 ? ? ? ? 48 8D 8F BC 1E 00 00 4C 89 BF A0 1E 00 00", 3, 4 },
        { &off.spottedMask, "EntitySpottedState_t::m_bSpottedByMask", "C6 46 08 01 8B 7E ? 48 8B 74 24 58 85 FF", 6, 1 },
        { &off.viewOffset, "C_BaseModelEntity::m_vecViewOffset", "48 8D BE ? ? ? ? 89 AE 60 0E 00 00 48 8B CF", 3, 4 },
        { &off.viewOffsetZ, "CNetworkViewOffsetVector::m_vecZ", "49 89 5B 10 49 8B D7 49 89 73 18 4D 89 63", 14, 1 },
        { &off.glow, "C_BaseModelEntity::m_Glow", "48 8D 8E ? ? ? ? E8 ? ? ? ? F3 0F 10 86 40 0E 00 00", 3, 4 },
        { &off.glowType, "CGlowProperty::m_iGlowType", "8B 42 ? 89 41 08 8B 42 34 89 41 0C 8B 42 38", 2, 1 },
        { &off.glowOverride, "CGlowProperty::m_glowColorOverride", "8B 4E ? C6 02 65 89 4A 01 48 83 C2 05 0F BA E7 0B", 2, 1 },
        { &off.glowing, "CGlowProperty::m_bGlowing", "41 88 46 ? 48 85 DB 0F 85 ? ? ? ? E9 ? ? ? ? 40 80 FF 90 75", 3, 1 },
        { &off.glowRange, "CGlowProperty::m_nGlowRange", "8B 42 ? 89 41 10 8B 42 3C 89 41 14 8B 42 40", 2, 1 },
        { &off.glowRangeMin, "CGlowProperty::m_nGlowRangeMin", "89 47 ? F6 C1 10 74 ? 8B 46 40 89 47 40", 2, 1 },
        { &off.glowFlashing, "CGlowProperty::m_bFlashing", "89 47 ? F6 C1 40 74 ? 8B 46 48 89 47 48", 2, 1 },
        { &off.ownerEntity, "C_BaseEntity::m_hOwnerEntity", "C7 85 18 05 00 00 D4 11 00 00 66 89 8D 1C 05 00 00 C7 85 ? ? ? ? 04 00 00 00", 19, 4 },
        { &off.clrRender, "C_BaseModelEntity::m_clrRender", "74 ? C6 86 ? ? ? ? FF 80 BE A1 0C 00 00 FF 74", 4, 4 },
        { &off.renderMode, "C_BaseModelEntity::m_nRenderMode", "48 8B 8E 40 10 00 00 33 D2 44 0F B6 86", 13, 4 },
        { &off.scoped, "C_CSPlayerPawn::m_bIsScoped", "48 89 87 88 1E 00 00 48 8D 8F BC 1E 00 00 4C 89 BF", 17, 4 },
        { &off.immunity, "C_CSPlayerPawn::m_bGunGameImmunity", "F3 0F 11 87 04 35 00 00 44 88 BF ? ? ? ? F3 0F 10 0D", 11, 4 },
        { &off.shotsFired, "C_CSPlayerPawn::m_iShotsFired", "48 8B 88 28 13 00 00 39 B0 B4 1E 00 00 74 ? 89 B0", 17, 4 },
        { &off.clip1, "C_BasePlayerWeapon::m_iClip1", "C7 83 ? ? ? ? FF FF FF FF 41 B8 01 00 00 00", 2, 4 },
        { &off.nextAttackTick, "C_BasePlayerWeapon::m_nNextPrimaryAttackTick", "48 8D 8B 1C 19 00 00 48 8D 05 ? ? ? ? 89 BB", 16, 4 },
        { &off.fallbackPaint, "C_EconEntity::m_nFallbackPaintKit", "89 B7 A4 18 00 00 39 B7 A8 18 00 00 74 ? 89 B7", 16, 4 },
        { &off.fallbackSeed, "C_EconEntity::m_nFallbackSeed", "89 B7 A8 18 00 00 39 B7 AC 18 00 00 74 ? 89 B7", 16, 4 },
        { &off.fallbackWear, "C_EconEntity::m_flFallbackWear", "7A ? 74 ? 89 B7 ? ? ? ? 48 8B 5C 24 30 48 8B C7", 6, 4 },
        { &off.fallbackStatTrak, "C_EconEntity::m_nFallbackStatTrak", "48 8B C7 48 8B 74 24 38 C7 87 ? ? ? ? FF FF FF FF", 10, 4 },
        { &off.attachmentDirty, "C_EconEntity::m_bAttachmentDirty", "C6 87 ? ? ? ? 01 48 8D 8F 90 12 00 00 48 8B 5C 24 30", 2, 4 },
        { &off.gearSlot, "CCSWeaponBaseVData::m_GearSlot", "48 89 8B 20 07 00 00 48 C7 83 ? ? ? ? FF FF FF FF", 10, 4 },
        { &off.melee, "CCSWeaponBaseVData::m_bMeleeWeapon", "48 89 8B 0C 07 00 00 48 89 8B 14 07 00 00 89 8B", 16, 4 },
    };
    for (auto& f : optional)
        *f.out = Field(f.name, f.pattern, f.pos, f.size);
    if (uint8_t* p = patterns::Find(client, "ViewMatrix", "48 63 C1 48 8D 0D ? ? ? ? 48 C1 E0 06 48 03 C1 C3"))
        viewMatrix = reinterpret_cast<float*>(mem::Rel(p + 3, 3, 7));
    if (uint8_t* p = patterns::Find(client, "Pawn::m_ArmorValue/m_bHasHelmet", "48 8D 15 ? ? ? ? 48 8B CF E8 ? ? ? ? 8B 93 ? ? ? ? 48 8B C8 E8 ? ? ? ? 48 8D 15 ? ? ? ? 48 8B CF E8 ? ? ? ? 48 8B 93 ? ? ? ? 48 8B C8 0F B6 52 ? E8"))
    {
        off.armor = *reinterpret_cast<uint32_t*>(p + 17);
        off.itemServices = *reinterpret_cast<uint32_t*>(p + 47);
        off.hasHelmet = p[57];
    }
    setViewAngles = reinterpret_cast<decltype(setViewAngles)>(patterns::Find(client, "SetViewAngles", "85 D2 75 3D 48 63 81 50 0B 00 00 F2 41 0F 10 00"));

    entityList = patterns::FindRef(client, "EntityList", "48 8B 0D ? ? ? ? 48 85 C9 74 ? 41 83 F8 FE", 3, 7);
    if (!entityList)
        return Fail("entity list");

    createItem = reinterpret_cast<decltype(createItem)>(patterns::Find(client, "CEconItem::CreateInstance", "48 83 EC 28 B9 48 00 00 00 E8 ? ? ? ? 48 85"));
    if (!createItem)
        return Fail("CEconItem::CreateInstance");

    for (uint8_t* p : patterns::FindAll(client, "SetDynamicAttributeValue", "48 89 6C 24 ? 57 41 56 41 57 48 81 EC ? ? ? ? 48 8B FA C7 44 24 ? ? ? ? ? 4D 8B F8 4C 8D 0D", 8, 3))
    {
        uint8_t* td = mem::Rel(p + 31, 3, 7);
        if (!client.Contains(td))
            continue;
        auto name = reinterpret_cast<const char*>(td + 0x10);
        if (!strcmp(name, ".?AV?$ISchemaAttributeTypeBase@I@@"))
            setAttrU32 = reinterpret_cast<decltype(setAttrU32)>(p);
        else if (!strcmp(name, ".?AV?$ISchemaAttributeTypeBase@M@@"))
            setAttrF32 = reinterpret_cast<decltype(setAttrF32)>(p);
    }
    if (!setAttrU32)
        return Fail("SetDynamicAttributeValue");

    createTypeCache = reinterpret_cast<decltype(createTypeCache)>(patterns::Find(client, "CreateBaseTypeCache", "40 57 48 83 EC 20 4C 8B 49 18 44 8B D2 4C 63 41 10 48 8B F9"));
    if (!createTypeCache)
        return Fail("CreateBaseTypeCache");

    itemSchema = reinterpret_cast<decltype(itemSchema)>(patterns::Find(client, "GetItemSchema", "48 83 EC 28 E8 ? ? ? ? 48 8B 40 08 48 83 C4 28 C3"));
    if (!itemSchema)
        return Fail("GetItemSchema");

    setModel = reinterpret_cast<decltype(setModel)>(patterns::Find(client, "SetModel", "40 53 48 83 EC 20 48 8B D9 4C 8B C2 48 8B 0D ? ? ? ? 48 8D 54 24"));
    if (!setModel)
        return Fail("SetModel");

    setMeshGroupMask = reinterpret_cast<decltype(setMeshGroupMask)>(patterns::Find(client, "SetMeshGroupMask", "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 48 8D 99 ? ? ? ? 48 8B 71"));
    if (!setMeshGroupMask)
        return Fail("SetMeshGroupMask");

    subclassChanged = reinterpret_cast<decltype(subclassChanged)>(patterns::Find(client, "OnSubclassIDChanged", "40 53 48 83 EC 20 48 8B D9 E8 ? ? ? ? 48 83 BB ? ? ? ? 00 74 ? 48 8B 03 BA 01 00 00 00"));
    if (!subclassChanged)
        return Fail("OnSubclassIDChanged");

    if (uint8_t* p = patterns::Find(client, "AttachmentComposite", "48 8D 8F ? ? ? ? 48 8B 01 FF 50 18 48 8D 8F ? ? ? ? B2 01 48 8B D8 E8 ? ? ? ? 48 8D 4B 50 E8 ? ? ? ? 44 0F B6 C8 48 89 6C 24 20 44 0F B6 C6"))
    {
        attachItemOffset = *reinterpret_cast<uint32_t*>(p + 3);
        attachCompositeOffset = *reinterpret_cast<uint32_t*>(p + 16);
        hasCustomMaterial = reinterpret_cast<decltype(hasCustomMaterial)>(mem::Rel(p + 34, 1, 5));
        for (uint8_t* q = p + 39; q < p + 72; ++q)
            if (*q == 0xE8)
            {
                buildComposite = reinterpret_cast<decltype(buildComposite)>(mem::Rel(q, 1, 5));
                break;
            }
    }

    if (uint8_t* p = patterns::Find(client, "VDataLoader", "89 54 24 10 53 57 41 56 41 57 48 81 EC A8 00 00 00 4C 63 FA 4C 8B F1 49 69 DF 30 01 00 00"))
    {
        uint8_t* targets[8]{};
        int n = 0;
        for (uint8_t* q = p; q < p + 0x340 && n < 8; ++q)
        {
            if (*q != 0xE8)
                continue;
            uint8_t* t = mem::Rel(q, 1, 5);
            if (!client.Contains(t) || (n && targets[n - 1] == t))
                continue;
            targets[n++] = t;
            q += 4;
        }
        if (n >= 5)
        {
            kvCount = reinterpret_cast<decltype(kvCount)>(targets[1]);
            kvMember = reinterpret_cast<decltype(kvMember)>(targets[2]);
            kvName = reinterpret_cast<decltype(kvName)>(targets[3]);
            loadVData = reinterpret_cast<decltype(loadVData)>(targets[4]);
        }
    }

    if (uint8_t* p = patterns::Find(client, "VDataRegistry", "48 8B 0D ? ? ? ? 8B D6 E8 ? ? ? ? 48 89 83 ? ? ? ? 48 85 C0 75"))
    {
        vdataRegistry = reinterpret_cast<void**>(mem::Rel(p, 3, 7));
        findVData = reinterpret_cast<decltype(findVData)>(mem::Rel(p + 9, 1, 5));
        vdataOffset = *reinterpret_cast<uint32_t*>(p + 17);
    }

    if (uint8_t* p = patterns::Find(client, "RegenerateWeaponSkins", "48 8D 8B ? ? ? ? B2 01 E8 ? ? ? ? 33 D2 48 8B CB E8"))
    {
        compositeOffset = *reinterpret_cast<uint32_t*>(p + 3);
        clearComposite = reinterpret_cast<decltype(clearComposite)>(mem::Rel(p + 9, 1, 5));
        updateComposite = reinterpret_cast<decltype(updateComposite)>(mem::Rel(p + 19, 1, 5));
    }
    if (!updateComposite)
        return Fail("regenerate_weapon_skins");

    uint8_t* inventoryVtable = mem::VTable(client, ".?AVCCSInventoryManager@@");
    patterns::Note(client, "CCSInventoryManager::vftable", "vtable", inventoryVtable);
    inventoryManager = mem::StaticInstance(client, inventoryVtable);
    patterns::Note(client, "CCSInventoryManager", "global", inventoryManager);
    if (!inventoryManager)
        return Fail("CCSInventoryManager");

    stringAttrVtable = mem::VTable(client, ".?AVCSchemaAttributeType_String@@");
    patterns::Note(client, "CSchemaAttributeType_String::vftable", "vtable", stringAttrVtable);
    patterns::Note(client, "ViewMatrix::data", "global", viewMatrix);
    patterns::Note(client, "VDataRegistry::data", "global", vdataRegistry);

    error.clear();
    ready = true;
    return true;
}

bool game::Ready()
{
    return ready;
}

const std::string& game::Error()
{
    return error;
}

void* game::Handle(uint32_t handle)
{
    if (handle == 0xffffffff)
        return nullptr;
    uint8_t* id = Identity(handle & 0x7fff);
    if (!id || *reinterpret_cast<uint32_t*>(id + 0x10) != handle)
        return nullptr;
    return *reinterpret_cast<void**>(id);
}

void* game::LocalController()
{
    for (int i = 1; i <= 64; ++i)
    {
        uint8_t* id = Identity(i);
        if (!id)
            continue;
        void* e = *reinterpret_cast<void**>(id);
        auto name = *reinterpret_cast<const char**>(id + 0x20);
        if (e && name && !strcmp(name, "cs_player_controller") && mem::At<bool>(e, off.isLocalController))
        {
            localIndex = i;
            return e;
        }
    }
    return nullptr;
}

void* game::Inventory()
{
    return inventoryManager ? mem::Call<void*, 72>(inventoryManager) : nullptr;
}

uint64_t game::Owner(void* inventory)
{
    return inventory ? mem::At<uint64_t>(inventory, 0x10) : 0;
}

void* game::ItemCache(void* inventory)
{
    void* cache = inventory ? mem::At<void*>(inventory, 0x68) : nullptr;
    return cache ? createTypeCache(cache, 1) : nullptr;
}

int game::CacheCount(void* cache)
{
    return cache ? mem::At<int>(cache, 0x8) : 0;
}

void* game::CacheObject(void* cache, int index)
{
    auto objects = mem::At<void**>(cache, 0x10);
    return objects ? objects[index] : nullptr;
}

void* game::CreateItem()
{
    return createItem();
}

void game::SetAttribute(void* item, int index, uint32_t bits)
{
    void* def = AttributeDefinition(index);
    if (!def)
        return;
    setAttrU32(item, def, &bits);
    if (setAttrF32)
        setAttrF32(item, def, &bits);
}

bool game::SetAttributeString(void* item, int index, const char* value)
{
    void* def = AttributeDefinition(index);
    if (!def || !stringAttrVtable)
        return false;
    void* type = mem::At<void*>(def, 0x18);
    if (!type || *reinterpret_cast<uint8_t**>(type) != stringAttrVtable)
        return false;
    mem::Call<void, 6>(type, item, def, value);
    return true;
}

bool game::AddItem(void* inventory, void* item)
{
    void* cache = ItemCache(inventory);
    if (!cache || !mem::Call<bool, 1>(cache, item))
        return false;
    SOID owner = mem::At<SOID>(inventory, 0x10);
    mem::Call<void, 0>(inventory, &owner, item, 4);
    return true;
}

void game::RemoveItem(void* inventory, void* item)
{
    void* cache = ItemCache(inventory);
    if (!cache)
        return;
    bool found = false;
    for (int i = 0, n = CacheCount(cache); i < n && !found; ++i)
        found = CacheObject(cache, i) == item;
    if (!found)
        return;
    SOID owner = mem::At<SOID>(inventory, 0x10);
    mem::Call<void, 2>(inventory, &owner, item, 4);
    mem::Call<bool, 3>(cache, item);
}

void game::Equip(int team, int slot, uint64_t itemId)
{
    if (inventoryManager)
        mem::Call<bool, 69>(inventoryManager, team, slot, itemId);
}

void game::SetModel(void* entity, const char* model)
{
    setModel(entity, model);
}

void game::RefreshModel(void* entity)
{
    const char* name = entity ? ModelName(entity) : nullptr;
    if (!name || !*name)
        return;
    const std::string copy = name;
    setModel(entity, copy.c_str());
}

const char* game::ModelName(void* entity)
{
    void* skel = Skeleton(entity);
    return skel ? mem::At<const char*>(skel, off.modelState + off.modelName) : nullptr;
}

uint64_t game::MeshGroupMask(void* entity)
{
    void* skel = Skeleton(entity);
    return skel ? mem::At<uint64_t>(skel, off.modelState + off.meshGroupMask) : 0;
}

void game::SetMeshGroupMask(void* entity, uint64_t mask)
{
    void* skel = Skeleton(entity);
    if (skel && mem::At<uint64_t>(skel, off.modelState + off.meshGroupMask) != mask)
        setMeshGroupMask(skel, mask);
}

void game::UpdateSubclass(void* entity)
{
    subclassChanged(entity);
}

void game::RegenerateSkin(void* weapon)
{
    if (clearComposite)
        clearComposite(static_cast<uint8_t*>(weapon) + compositeOffset, true);
    updateComposite(weapon, false);
}

uint32_t game::Token(const char* s)
{
    const uint32_t m = 0x5bd1e995;
    uint32_t len = static_cast<uint32_t>(strlen(s));
    uint32_t h = 0x31415926 ^ len;
    auto lower = [](char c) { return static_cast<uint8_t>(c >= 'A' && c <= 'Z' ? c + 32 : c); };
    while (len >= 4)
    {
        uint32_t k = lower(s[0]) | lower(s[1]) << 8 | lower(s[2]) << 16 | static_cast<uint32_t>(lower(s[3])) << 24;
        k *= m;
        k ^= k >> 24;
        k *= m;
        h *= m;
        h ^= k;
        s += 4;
        len -= 4;
    }
    switch (len)
    {
    case 3: h ^= lower(s[2]) << 16; [[fallthrough]];
    case 2: h ^= lower(s[1]) << 8; [[fallthrough]];
    case 1: h ^= lower(s[0]); h *= m;
    }
    h ^= h >> 13;
    h *= m;
    h ^= h >> 15;
    return h;
}

bool game::HasVData(uint32_t key)
{
    void* registry = vdataRegistry ? *vdataRegistry : nullptr;
    return key && registry && findVData && findVData(registry, -1, key);
}

void* game::ViewmodelAttachment(void* weapon)
{
    return off.viewmodelAttachment ? Handle(mem::At<uint32_t>(weapon, off.viewmodelAttachment)) : nullptr;
}

void game::RegenerateViewmodel(void* weapon)
{
    uint8_t* attach = static_cast<uint8_t*>(ViewmodelAttachment(weapon));
    if (!attach || !attachItemOffset || !buildComposite || !hasCustomMaterial || !clearComposite)
        return;
    void* holder = attach + attachItemOffset;
    auto item = mem::Call<uint8_t*, 3>(holder);
    if (!item)
        return;
    clearComposite(attach + attachCompositeOffset, true);
    buildComposite(attach, item + 0x50, false, hasCustomMaterial(item + 0x50), nullptr);
}

uint8_t* game::FindConVar(const char* name)
{
    const size_t len = strlen(name) + 1;
    const uint8_t* str = nullptr;
    for (size_t i = 1; i + len < client.rdata.size && !str; ++i)
        if (!client.rdata.start[i - 1] && !memcmp(client.rdata.start + i, name, len))
            str = client.rdata.start + i;
    if (!str)
    {
        patterns::Note(client, name, "convar", nullptr);
        return nullptr;
    }
    uint8_t* text = client.text.start;
    for (size_t i = 0; i + 7 < client.text.size; ++i)
    {
        if (text[i] != 0x48 || text[i + 1] != 0x8D || text[i + 2] != 0x15)
            continue;
        if (text + i + 7 + *reinterpret_cast<int32_t*>(text + i + 3) != str)
            continue;
        for (size_t j = i + 7; j < i + 0x90 && j + 7 < client.text.size; ++j)
            if (text[j] == 0x48 && text[j + 1] == 0x8D && text[j + 2] == 0x0D)
            {
                uint8_t* obj = text + j + 7 + *reinterpret_cast<int32_t*>(text + j + 3);
                obj = client.Contains(obj) ? obj : nullptr;
                patterns::Note(client, name, "convar", obj);
                return obj;
            }
    }
    patterns::Note(client, name, "convar", nullptr);
    return nullptr;
}

uint8_t* game::ConVarValue(uint8_t* object)
{
    uint8_t* data = object ? *reinterpret_cast<uint8_t**>(object + 8) : nullptr;
    return data ? data + 0x58 : nullptr;
}

float game::ConVarFloat(uint8_t* object, float fallback)
{
    uint8_t* v = ConVarValue(object);
    return v ? *reinterpret_cast<float*>(v) : fallback;
}

namespace
{
    bool RealKnife(void* vdata, void* reference)
    {
        if (!vdata || !game::off.gearSlot || !game::off.melee)
            return false;
        if (!mem::At<bool>(vdata, game::off.melee))
            return false;
        return !reference || mem::At<int>(vdata, game::off.gearSlot) == mem::At<int>(reference, game::off.gearSlot);
    }

    void* KvRoot(uint8_t* registry, int type)
    {
        if (type < 0 || type > 1)
            return nullptr;
        auto slots = mem::At<uint8_t*>(registry, 0x20);
        if (!slots)
            return nullptr;
        auto resource = mem::At<uint8_t**>(slots + type * 0x130, 0xe8);
        if (!resource || !*resource)
            return nullptr;
        uint8_t* data = *resource + 8;
        if (!(mem::At<uint8_t>(data, 0x1130) & 4))
            return nullptr;
        return data + 0x78;
    }

    bool LoadEntry(uint8_t* registry, int type, const char* name)
    {
        void* root = KvRoot(registry, type);
        if (!root || !kvCount || !kvMember || !kvName || !loadVData)
            return false;
        const int count = kvCount(root);
        for (int i = 0; i < count && i < 4096; ++i)
        {
            const char* key = kvName(root, i);
            if (!key || _stricmp(key, name))
                continue;
            void* member = kvMember(root, i);
            if (!member || ((*static_cast<uint8_t*>(member) >> 2) & 0xf) != 9)
                return false;
            int& loading = mem::At<int>(registry, 0x100);
            const int saved = loading;
            loading = type;
            loadVData(registry, type, key, member);
            loading = saved;
            return true;
        }
        return false;
    }

    bool SafeLoadEntry(uint8_t* registry, int type, const char* name)
    {
        __try
        {
            return LoadEntry(registry, type, name);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }
}

void* game::CurrentVData(void* entity)
{
    return entity && vdataOffset ? mem::At<void*>(entity, vdataOffset) : nullptr;
}

void* game::KnifeVData(void* entity, const char* name)
{
    const uint32_t token = Token(name);
    auto registry = static_cast<uint8_t*>(vdataRegistry ? *vdataRegistry : nullptr);
    if (!registry || !findVData || !entity || !vdataOffset)
        return nullptr;
    void* reference = CurrentVData(entity);
    if (void* v = findVData(registry, -1, token))
        return RealKnife(v, reference) ? v : nullptr;
    static uint32_t tried[64]{};
    static int triedCount = 0;
    for (int i = 0; i < triedCount; ++i)
        if (tried[i] == token)
            return nullptr;
    if (triedCount < 64)
        tried[triedCount++] = token;
    const int type = mem::Call<int, 0x638 / 8>(entity);
    if (!SafeLoadEntry(registry, type, name))
        return nullptr;
    void* v = findVData(registry, -1, token);
    return RealKnife(v, reference) ? v : nullptr;
}

void* game::VData(uint32_t key)
{
    void* registry = vdataRegistry ? *vdataRegistry : nullptr;
    return key && registry && findVData ? findVData(registry, -1, key) : nullptr;
}

void* game::EntityAt(int index, const char** designer)
{
    uint8_t* id = Identity(index);
    if (!id)
        return nullptr;
    if (designer)
        *designer = *reinterpret_cast<const char**>(id + 0x20);
    return *reinterpret_cast<void**>(id);
}

uint32_t game::HandleAt(int index)
{
    uint8_t* id = Identity(index);
    return id ? *reinterpret_cast<uint32_t*>(id + 0x10) : 0xffffffff;
}

const float* game::ViewMatrix()
{
    return viewMatrix;
}

bool game::Origin(void* entity, Vec3& out)
{
    void* node = entity ? mem::At<void*>(entity, off.sceneNode) : nullptr;
    if (!node)
        return false;
    out = mem::At<Vec3>(node, off.absOrigin);
    return true;
}

bool game::Bone(void* pawn, int index, Vec3& out)
{
    void* node = pawn ? mem::At<void*>(pawn, off.sceneNode) : nullptr;
    if (!node || index < 0 || index > 127)
        return false;
    auto bones = mem::At<uint8_t*>(node, off.modelState + 0x80);
    if (!bones)
        return false;
    out = *reinterpret_cast<Vec3*>(bones + index * 0x20);
    const Vec3 o = mem::At<Vec3>(node, off.absOrigin);
    const float dx = out.x - o.x, dy = out.y - o.y, dz = out.z - o.z;
    return dx * dx + dy * dy + dz * dz < 200.f * 200.f;
}

game::Vec3 game::EyePosition(void* pawn)
{
    Vec3 o{};
    Origin(pawn, o);
    float z = 64.f;
    if (off.viewOffset && off.viewOffsetZ)
    {
        const float v = mem::At<float>(pawn, off.viewOffset + off.viewOffsetZ);
        if (v > 10.f && v < 80.f)
            z = v;
    }
    o.z += z;
    return o;
}

bool game::SetViewAngles(void* input, float pitch, float yaw)
{
    if (!setViewAngles || !input)
        return false;
    const float a[3]{ pitch, yaw, 0.f };
    setViewAngles(input, 0, a);
    return true;
}

bool game::SpottedBy(void* pawn, int controllerIndex)
{
    if (!off.spottedState || !off.spottedMask || controllerIndex <= 0)
        return true;
    const uint32_t* mask = &mem::At<uint32_t>(pawn, off.spottedState + off.spottedMask);
    const int bit = controllerIndex - 1;
    return (mask[bit >> 5] >> (bit & 31)) & 1;
}

int game::LocalIndex()
{
    return localIndex;
}

void* game::LoadoutItem(int team, int slot)
{
    void* inventory = Inventory();
    return inventory ? mem::Call<void*, 8>(inventory, team, slot) : nullptr;
}

uint64_t game::ViewItemId(void* view)
{
    if (!view)
        return 0;
    return static_cast<uint64_t>(mem::At<uint32_t>(view, off.itemIdHigh)) << 32 | mem::At<uint32_t>(view, off.itemIdLow);
}

int game::ViewDef(void* view)
{
    return view && mem::At<bool>(view, off.initialized) ? mem::At<uint16_t>(view, off.defIndex) : 0;
}
