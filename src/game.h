#pragma once
#include <cstdint>
#include <string>

namespace game
{
    struct Offsets
    {
        uint32_t sceneNode, teamNum, subclassId;
        uint32_t isLocalController, playerPawn, tickBase;
        uint32_t flags, health, moveType, absVelocity, movementServices, maxSpeed, aimPunchServices, absOrigin, gravityScale;
        uint32_t weaponServices, myWeapons, activeWeapon, viewmodelAttachment;
        uint32_t gloves, reapplyGloves;
        uint32_t attributeManager, item, xuidLow, xuidHigh;
        uint32_t defIndex, quality, itemId, itemIdHigh, itemIdLow, accountId, initialized, disallowSoc;
        uint32_t modelState, modelName, meshGroupMask;
        uint32_t gearSlot, melee;
        uint32_t fallbackPaint, fallbackSeed, fallbackWear, fallbackStatTrak, attachmentDirty;
        uint32_t sanitizedName, pawnIsAlive, collision, vecMins, vecMaxs, dormant;
        uint32_t spottedState, spottedMask, viewOffset, viewOffsetZ;
        uint32_t glow, glowType, glowOverride, glowing, glowRange, glowRangeMin, glowFlashing;
        uint32_t viewModelServices, viewModel, clrRender, renderMode, ownerEntity;
        uint32_t armor, itemServices, hasHelmet, scoped, immunity, clip1, nextAttackTick, shotsFired, lifeState;
    };

    struct SOID
    {
        uint64_t id;
        uint32_t type;
        uint32_t pad;
    };

    inline Offsets off{};

    bool Init();
    bool Ready();
    const std::string& Error();

    void* Handle(uint32_t handle);
    void* LocalController();

    void* Inventory();
    uint64_t Owner(void* inventory);
    void* ItemCache(void* inventory);
    int CacheCount(void* cache);
    void* CacheObject(void* cache, int index);
    void* CreateItem();
    void SetAttribute(void* item, int index, uint32_t bits);
    bool SetAttributeString(void* item, int index, const char* value);
    bool AddItem(void* inventory, void* item);
    void RemoveItem(void* inventory, void* item);
    void Equip(int team, int slot, uint64_t itemId);

    void SetModel(void* entity, const char* model);
    void RefreshModel(void* entity);
    const char* ModelName(void* entity);
    uint64_t MeshGroupMask(void* entity);
    void SetMeshGroupMask(void* entity, uint64_t mask);
    void UpdateSubclass(void* entity);
    bool HasVData(uint32_t key);
    void* KnifeVData(void* entity, const char* name);
    void* CurrentVData(void* entity);
    void* VData(uint32_t key);
    void* LoadoutItem(int team, int slot);
    uint64_t ViewItemId(void* view);
    int ViewDef(void* view);
    uint8_t* FindConVar(const char* name);
    float ConVarFloat(uint8_t* object, float fallback);
    uint8_t* ConVarValue(uint8_t* object);
    void RegenerateSkin(void* weapon);
    void* ViewmodelAttachment(void* weapon);
    void RegenerateViewmodel(void* weapon);
    uint32_t Token(const char* s);

    struct Vec3
    {
        float x, y, z;
    };

    void* EntityAt(int index, const char** designer = nullptr);
    uint32_t HandleAt(int index);
    const float* ViewMatrix();
    bool Bone(void* pawn, int index, Vec3& out);
    bool Origin(void* entity, Vec3& out);
    Vec3 EyePosition(void* pawn);
    bool SetViewAngles(void* input, float pitch, float yaw);
    bool SpottedBy(void* pawn, int controllerIndex);
    int LocalIndex();
}
