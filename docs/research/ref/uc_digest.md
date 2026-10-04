# UC CS2 digest (2025–2026), собрано 2026-10-04

База ссылок: `https://www.unknowncheats.me/forum/counter-strike-2-a/<id>-x.html` (или `showthread.php?t=<id>`).
Пометка **[stale?]** = оффсет/индекс, который менялся по тредам или датирован до апдейтов 2026-07 (S5) / 2026-09-23.

---

## 1. Silent aim + subtick

### Треды
| id | дата | суть |
|---|---|---|
| 739930 | 2026-02 | ogpolak: хук SubTickAngle-функции, a1[4..6] = углы |
| 727475 | 2025-11/12 | MegaBlax set_sub_tick_angles; Requi: лерп по истории |
| 753710 | 2026-05 | SetSubTickAngle по всем inputHistory; «silent детектится в MM» |
| 774174 | 2026-09 | снайперы мажут; CRC save/recalc/restore |
| 758320 / 755398 | 2026-06 | 50% выстрелов не регаются онлайн; frame-history silent не работает на реальных серверах |
| 727859 / 767960 | — | TrueView: в демке виден silent-угол |

### Шаги
1. Хук `CCSGOInput::CreateMove` (vtable idx 5, `(CCSGOInput*, int slot, bool active)`), вызвать оригинал.
2. Получить `CUserCmd` текущего тика (см. 3, get_user_command_from_controller).
3. Для **каждого** `input_history` entry записать `pViewAngles->angValue` = aim-угол, выставить hasbits `INPUT_HISTORY_BITS_VIEWANGLES (0x1)`.
4. Base cmd viewangles: либо оставить (настоящий silent), либо тоже писать — но тогда пересчитать CRC (раздел 2).
5. Выставить корректные `nRenderTickCount/nPlayerTickCount` (tickbase) и фракции, иначе сервер интерполирует не туда.
6. Shoot pos брать из `CNetworkedClientInfo` (IVEngineClient2 idx 178 [stale?]).

### Код (753710 / 727475, сведённый)
```cpp
void SetSubTickAngle(CCSGOUserCmdPB* cs, const QAngle& ang)
{
    auto& f = cs->inputHistoryField;
    if (!f.pRep) return;
    for (int i = 0; i < f.nCurrentSize; ++i)
    {
        auto* e = f.pRep->tElements[i];
        if (!e || !e->pViewAngles) continue;
        e->pViewAngles->angValue = ang;
        e->nHasBits |= INPUT_HISTORY_BITS_VIEWANGLES;
        e->pViewAngles->nHasBits |= 0x7;
    }
}
```
Вариант с лерпом (Requi, против «снапа» в VAC Live):
```cpp
void SetSubTickAngleSmooth(CCSGOUserCmdPB* cs, const QAngle& from, const QAngle& to)
{
    auto& f = cs->inputHistoryField;
    int n = f.nCurrentSize;
    for (int i = 0; i < n; ++i)
    {
        auto* e = f.pRep->tElements[i];
        if (!e || !e->pViewAngles) continue;
        float t = n > 1 ? float(i + 1) / float(n) : 1.f;
        QAngle a = from + (to - from).Normalized() * t;
        a.Normalize(); a.Clamp();
        e->pViewAngles->angValue = a;
        e->nHasBits |= INPUT_HISTORY_BITS_VIEWANGLES;
    }
}
```
Хук subtick-angle (739930):
```cpp
using SubTickAngle_t = __int64(__fastcall*)(DWORD* a1, CSGOInputHistoryEntryPB* a2, char a3, double a4, int a5, CCSPlayerPawn* a6);
__int64 __fastcall hkSubTickAngle(DWORD* a1, CSGOInputHistoryEntryPB* a2, char a3, double a4, int a5, CCSPlayerPawn* a6)
{
    if (g_aim.active)
    {
        *(float*)&a1[4] = g_aim.angle.x;
        *(float*)&a1[5] = g_aim.angle.y;
        *(float*)&a1[6] = 0.f;
    }
    return oSubTickAngle(a1, a2, a3, a4, a5, a6);
}
```
Subtick manager RolekTV — bits шага: `has_button=1, has_pressed=2, has_when=4, has_forward=8, has_side=0x10, has_yaw=0x20, has_pitch=0x40`; поля `analog_yaw_delta / analog_pitch_delta`.

### Подводные камни
- Одинаковые углы во всех entries = мгновенный флик, палится (VAC Live). Лерп.
- Снайперы: нужна проверка scope/inaccuracy; тип оружия брать из `CCSWeaponBaseVData::m_WeaponType` (774174).
- 774174: CRC пересчитан с мутированным base viewangle → флик скрыт, но выстрел ушёл по оригинальному углу ⇒ сервер берёт угол выстрела из input_history, не из base.
- Без правильного tickcount выстрелы не регаются (~50% онлайн, на ботах ок).
- TrueView в демках показывает silent.

### Противоречия
- 755398 «frame-history silent не работает на серверах» vs 753710 «работает, но детект в MM».
- Писать base viewangles + CRC (641268) vs только history (774174 показывает, что base не влияет на выстрел).

---

## 2. CRC usercmd

### Треды
641268 (Exlodium, 2024-06, базовая реализация, до сих пор цитируется), 744444 (2026-03, краши онлайн), 743153 (что хешируется), 738333 (2026-02, полный SDK).

### Шаги
1. После оригинала CreateMove сохранить (Save) кнопки/углы, внести изменения.
2. `CalculateCmdCRCSize` (vfunc 7 у CBaseUserCmdPB) → размер.
3. `CUtlBuffer(0,0,0)`, `EnsureCapacity(size+1)`.
4. `SerializePartialToArray(baseCmd, buf.Base(), size)`.
5. Аллоц строки protobuf (MemAlloc 0x18), `WriteMessage`, `m_pMoveCrc = SetMessageData(&m_pMoveCrc, pMsg, &nHasBits)`, Free.
6. Restore исходных кнопок/углов в cmd (если нужен «настоящий» визуал), иначе — оставить.

### Код (641268, полный)
```cpp
class CUtlBuffer
{
public:
    MEM_PAD(0x80);
    CUtlBuffer(int a1, int nSize, int a3)
    {
        static auto fn = (void(__fastcall*)(CUtlBuffer*, int, int, int))GetProcAddress(GetModuleHandleA("tier0.dll"), "??0CUtlBuffer@@QEAA@HHH@Z");
        fn(this, a1, nSize, a3);
    }
    void EnsureCapacity(int nSize)
    {
        static auto fn = (void(__fastcall*)(CUtlBuffer*, int))GetProcAddress(GetModuleHandleA("tier0.dll"), "?EnsureCapacity@CUtlBuffer@@QEAAXH@Z");
        fn(this, nSize);
    }
};

struct CRCInformation
{
    uint64_t nButtons, nButtonsChanged, nButtonsScroll;
    QAngle angView;
    void Save(CUserCmd* cmd)
    {
        nButtons = cmd->nButtons.nValue;
        nButtonsChanged = cmd->nButtons.nValueChanged;
        nButtonsScroll = cmd->nButtons.nValueScroll;
        angView = cmd->csgoUserCmd.pBaseCmd->pViewAngles->angValue;
    }
    void Apply(CUserCmd* cmd)
    {
        cmd->nButtons.nValue = nButtons;
        cmd->nButtons.nValueChanged = nButtonsChanged;
        cmd->nButtons.nValueScroll = nButtonsScroll;
        cmd->csgoUserCmd.pBaseCmd->pViewAngles->angValue = angView;
    }
};

bool CBaseUserCmdPB::CalculateCRC()
{
    int nSize = CallVFunc<int, 7>(this);
    CUtlBuffer buf(0, 0, 0);
    buf.EnsureCapacity(nSize + 1);
    void* pData = *(void**)&buf;
    if (!SerializePartialToArray(this, pData, nSize))
        return false;
    void* pMsg = MemAlloc()->Alloc(0x18);
    WriteMessage(pMsg, pData, nSize);
    m_pMoveCrc = SetMessageData(&m_pMoveCrc, pMsg, &nHasBits);
    MemAlloc()->Free(pMsg);
    return true;
}

void __fastcall hkCreateMove(CCSGOInput* input, int slot, bool active)
{
    oCreateMove(input, slot, active);
    CUserCmd* cmd = GetUserCmd();
    if (!cmd) return;
    CRCInformation crc; crc.Save(cmd);
    RunFeatures(cmd);
    cmd->csgoUserCmd.pBaseCmd->CalculateCRC();
    crc.Apply(cmd);
}
```
Альтернатива (Defiet): собрать proto-lib 3.21.8 и `baseCmd.SerializeToString(mutable_move_crc())`.

### Подводные камни
- ji666: CUtlBuffer по значению → копия, утечка памяти; держать как локальный объект и не копировать.
- Краш «read 0x4» у части юзеров (641268) и краш онлайн через пару минут (744444) — вероятно утечка/арена. proto-lib крашит сильнее.
- 743153: CRC — хеш входов; сервер сравнивает: `IN_FORWARD` должен соответствовать `forward_move`, иначе рассинхрон.

### Противоречия
- Restore после CRC (641268) vs «не трогать base вообще» (774174).

---

## 3. Добавление CSubtickMoveStep

### Треды
773324 (2026-09), 751125 (2026-05), 722845 (арена/protobuf 3.21.8), 754168 (layout), 740766 (when), 751528 (deep dive), 746996 (Patoke: реализовать protobuf руками).

### Структуры [stale?]
```cpp
struct CBasePB { void* vt; uint32_t nHasBits; uint32_t nCachedBits; void* pArena; };
struct CSubtickMoveStep : CBasePB
{
    uint64_t nButton;
    bool bPressed;
    float flWhen;
    float flAnalogForwardDelta;
    float flAnalogLeftDelta;
    float flPitchDelta;
    float flYawDelta;
};
enum ESubtickMoveStepBits : uint32_t
{
    MOVESTEP_BITS_BUTTON = 0x1,
    MOVESTEP_BITS_PRESSED = 0x2,
    MOVESTEP_BITS_WHEN = 0x4,
    MOVESTEP_BITS_ANALOG_FORWARD_DELTA = 0x8,
    MOVESTEP_BITS_ANALOG_LEFT_DELTA = 0x10,
    MOVESTEP_BITS_PITCH_DELTA = 0x20,
    MOVESTEP_BITS_YAW_DELTA = 0x40,
};
template <typename T> struct RepeatedPtrField_t
{
    struct Rep_t { int nAllocatedSize; T* tElements[(INT_MAX - 2 * sizeof(int)) / sizeof(void*)]; };
    void* pArena;
    int nCurrentSize;
    int nTotalSize;
    Rep_t* pRep;
};
```
754168: после CBasePB — buttons u64, pressed, when@0xC(rel), fwd 0x10, left 0x14, pitch 0x18, yaw 0x1C. Биты PITCH/YAW у RolekTV (0x40/0x20) и в 754168 (0x20/0x40) — **перепутаны между тредами**, проверять в IDA.

### Код (773324 / 751125)
```cpp
CSubtickMoveStep* AddSubtickStep(CBaseUserCmdPB* base)
{
    auto& f = base->subtickMovesField;
    if (f.pRep && f.nCurrentSize < f.pRep->nAllocatedSize && f.pRep->tElements[f.nCurrentSize])
        return f.pRep->tElements[f.nCurrentSize++];
    auto* step = CreateSubtickMoveStep(f.pArena);
    AddToRepeated(&f, step);
    return step;
}

void PushStep(CBaseUserCmdPB* base, uint64_t button, bool pressed, float when)
{
    auto* s = AddSubtickStep(base);
    if (!s) return;
    s->nButton = button;
    s->bPressed = pressed;
    s->flWhen = std::clamp(when, 0.f, 0.999f);
    s->nHasBits |= MOVESTEP_BITS_BUTTON | MOVESTEP_BITS_PRESSED | MOVESTEP_BITS_WHEN;
    base->nHasBits |= BASE_BITS_SUBTICKMOVES;
}
```
get_user_command_from_controller (722845):
```cpp
CUserCmd* GetUserCmd(CCSPlayerController* ctrl)
{
    int seq = GetCommandSequence(ctrl);
    auto mgr = GetCmdManager(ctrl);
    auto cmd = (CUserCmd*)(mgr + 0x98 * (seq % 150));
    return *(int*)((uintptr_t)cmd + 8) == seq ? cmd : nullptr;
}
```
when (740766/751528): `fraction = (now - tick_start) / interval_per_tick`; клиент берёт его из `csgoinput+0x228` queue, `+0x70-4`, `enabled_fields |= 4`.

### Подводные камни
- Add каждый кадр → исчерпание/краш (751125). Только в CreateMove, один раз на тик.
- Арена шага должна совпадать с ареной cmd; `GetArena()==nullptr` → краш AddAllocated (722845), фикс — хук memalloc/operator new.
- Залипание pressed: каждый press должен иметь парный release.
- Сервер интегрирует физику кусочно по шагам; точность when зависит от FPS.
- shakro: проще `Clear()` + `add_subtick_moves()` через proto-lib.

### Противоречия
- Reuse-then-create (773324) vs сначала Moves->Add() (Fgt1HI0).
- Биты pitch/yaw.

---

## 4. Autofire / input_history / attack1_start_history_index

### Треды
743750 (semi-auto toggle), 702394 (DT history loop), 738333 (биты).

### Биты
```cpp
enum ECSGOUserCmdBits : uint32_t
{
    CSGOUSERCMD_BITS_BASECMD = 0x1,
    CSGOUSERCMD_BITS_LEFTHAND = 0x2,
    CSGOUSERCMD_BITS_ATTACK3START = 0x20,
    CSGOUSERCMD_BITS_ATTACK1START = 0x40,
    CSGOUSERCMD_BITS_ATTACK2START = 0x80,
};
```

### Код
```cpp
void AutoFire(CUserCmd* cmd, C_CSWeaponBase* wep, bool want)
{
    auto& b = cmd->nButtons;
    auto* cs = &cmd->csgoUserCmd;
    if (!want)
    {
        b.nValue &= ~IN_ATTACK;
        b.nValueChanged &= ~IN_ATTACK;
        return;
    }
    bool semi = !wep->GetVData()->m_bIsFullAuto();
    static bool last = false;
    if (semi && last) { b.nValue &= ~IN_ATTACK; last = false; return; }
    b.nValue |= IN_ATTACK;
    b.nValueChanged |= IN_ATTACK;
    last = true;
    int idx = cs->inputHistoryField.nCurrentSize - 1;
    cs->nAttack1StartHistoryIndex = idx < 0 ? 0 : idx;
    cs->nHasBits |= CSGOUSERCMD_BITS_ATTACK1START;
}
```
702394 (DT/timing):
```cpp
for (int i = 0; i < cs->inputHistoryField.nCurrentSize; ++i)
{
    auto* e = cs->inputHistoryField.pRep->tElements[i];
    e->nPlayerTickCount = wep->m_nNextPrimaryAttackTick() + offset + latencyTicks - 1;
    e->flPlayerTickFraction = 0.f;
}
cs->nAttack1StartHistoryIndex = -1;
cs->nHasBits |= CSGOUSERCMD_BITS_ATTACK1START;
```
### Камни
- Semi-auto: бит обязан чередоваться, иначе стреляет 1 раз.
- Когда не стреляешь — снимать IN_ATTACK и в value, и в value_changed.
- Проверять тип активного оружия (нож/граната).
- CCSGOInput attack_history 0x610–0x618 [stale? — 25.09 LoverofHate: «что-то поменялось на 0x610»].

---

## 5. Autowall

### Треды
764290 (airduck, 2026-07, полный релиз), 755418 (FOrna, 2026-06), 761819 (scaleDamage), 747821, 730349, 775074 (структуры устарели).

### Структуры (764290) [stale?]
```cpp
struct SurfaceData_t { MEM_PAD(0x8); float flPenetrationModifier; float flDamageModifier; };
struct TraceHitboxData_t { MEM_PAD(0x38); int nHitGroup; MEM_PAD(0x4); int nHitboxId; };
struct CGameTrace
{
    SurfaceData_t* pSurface;       // 0x0
    C_BaseEntity* pHitEntity;      // 0x8
    TraceHitboxData_t* pHitbox;    // 0x10
    MEM_PAD(0x10);
    uint32_t nContents;            // 0x28
    MEM_PAD(0x4C);
    Vector vecStart;               // 0x78
    Vector vecEnd;                 // 0x84
    Vector vecNormal;              // 0x90
    Vector vecPosition;            // 0x9C
    MEM_PAD(0x4);
    float flFraction;              // 0xAC
    MEM_PAD(0xA);
    bool bStartSolid;              // 0xBA
};
struct UpdateValue_t { float prevLenMod; float curLenMod; MEM_PAD(0x8); short handleIdx; MEM_PAD(0x6); };
struct TraceArrElement_t { MEM_PAD(0x38); };
struct TraceData_t
{
    int32_t nUnk;
    void* pArr;                         // 0x8
    MEM_PAD(0x8);
    TraceArrElement_t arr[128];         // 0x18
    MEM_PAD(0x8);
    int nModulateCount;                 // 0x1C20
    UpdateValue_t* pEntries;            // 0x1C28
    MEM_PAD(0xC8);
    Vector vecStart;                    // 0x1CF8
    Vector vecEnd;                      // 0x1D04
};
struct CTraceFilter { MEM_PAD(0xB8); };
struct BulletPenetrationData_t { float flDamage; float flPenetration; float flRangeModifier; float flRange; int nPenCount; bool bStopped; };
```
755418 даёт **другое**: CGameTrace 0x140, contents 0x50, arr element 0x30, filter 164 байта.

### Прототипы
```cpp
using InitTraceFilter_t = CTraceFilter*(__fastcall*)(CTraceFilter*, C_BaseEntity* skip, uint64_t mask, uint8_t layer, uint16_t unk);
using CreateTrace_t = void(__fastcall*)(TraceData_t*, Vector start, Vector delta, CTraceFilter*, int penCount, bool);
using GetTraceInfo_t = void(__fastcall*)(TraceData_t*, CGameTrace*, float startFrac, void* arrEntry);
using HandleBulletPen_t = bool(__fastcall*)(TraceData_t*, BulletPenetrationData_t*, UpdateValue_t*, int, void*);
```

### Код (764290, сведённый)
```cpp
constexpr uint64_t MASK_WALLBANG = 0x1C100B;

float ScaleDamage(C_CSPlayerPawn* p, float dmg, float armorRatio, float hsMult, int hg)
{
    switch (hg)
    {
    case HITGROUP_HEAD: dmg *= hsMult; break;
    case HITGROUP_STOMACH: dmg *= 1.25f; break;
    case HITGROUP_LEFTLEG: case HITGROUP_RIGHTLEG: dmg *= 0.75f; break;
    }
    int armor = p->m_ArmorValue();
    bool helmet = p->m_pItemServices()->m_bHasHelmet();
    bool armored = armor > 0 && (hg != HITGROUP_HEAD || helmet) && hg != HITGROUP_LEFTLEG && hg != HITGROUP_RIGHTLEG;
    if (armored)
    {
        float ratio = armorRatio * 0.5f;
        float newDmg = dmg * ratio;
        if ((dmg - newDmg) * 0.5f > armor) newDmg = dmg - armor / 0.5f;
        dmg = newDmg;
    }
    return dmg;
}

bool FireBullet(C_CSPlayerPawn* local, C_CSPlayerPawn* target, const Vector& start, const Vector& end, float& outDmg)
{
    auto* vd = local->GetActiveWeapon()->GetVData();
    float dmg = vd->m_nDamage(), pen = vd->m_flPenetration(), range = vd->m_flRange(), rmod = vd->m_flRangeModifier();
    float armorRatio = vd->m_flArmorRatio(), hsMult = vd->m_flHeadshotMultiplier();
    TraceData_t td{}; td.pArr = &td.arr;
    Vector dir = (end - start).Normalized();
    CTraceFilter filter; InitTraceFilter(&filter, local, MASK_WALLBANG, 3, 15);
    CreateTrace(&td, start, dir * range, &filter, 4, true);
    BulletPenetrationData_t bd{ dmg, pen, rmod, range, 4, false };
    for (int i = 0; i < td.nModulateCount; ++i)
    {
        auto* e = &td.pEntries[i];
        CGameTrace tr{};
        GetTraceInfo(&td, &tr, 0.f, (char*)td.pArr + sizeof(TraceArrElement_t) * (e->handleIdx & 0x7FFF));
        if (HandleBulletPen(&td, &bd, e, 4, nullptr)) return false;
        if (tr.pHitEntity == target)
        {
            float dist = (tr.vecPosition - start).Length();
            float d = bd.flDamage * std::pow(rmod, dist * 0.002f);
            outDmg = ScaleDamage(target, d, armorRatio, hsMult, tr.pHitbox->nHitGroup);
            return outDmg > 0.f;
        }
    }
    return false;
}
```
VData оффсеты (764290) [stale?]: vdata `+0x388`, m_nDamage 0x828 … RangeModifier 0x83C — брать из schema.

### Камни
- HandleBulletPenetration вызывать **до** проверки попадания (иначе урон без учёта стены).
- 747821: hitEntity всегда world / dmg=1 — неверный filter/mask или layout трейса.
- 761819: учитывать `mp_damage_scale_ct/t_head/body`.
- 766191: p2c выигрывают за счёт экстраполяции и точного shoot pos, а не AW.

### Противоречия
Layout CGameTrace/TraceData/filter 764290 vs 755418 — разные билды; сверять с IDA.

---

## 6. Hitchance / nospread

### Треды
773657 (CalcSpread decompile), poluxen/PDXiZT (seed, ran1), RolekTV (roll), 758433, 732117/753109 (hitchance, inaccuracy fns).

### Алгоритм seed
SHA1 от 12 байт: quantized pitch, quantized yaw (шаг 0.5°), tick → `seed = digest[0] (dword)`; в CalcSpread уходит `seed + 1`.
```cpp
uint32_t ComputeSeed(const QAngle& a, int tick)
{
    struct { float p, y; int t; } in{ std::round(a.x * 2.f) / 2.f, std::round(a.y * 2.f) / 2.f, tick };
    uint8_t dig[20]; SHA1((uint8_t*)&in, 12, dig);
    return *(uint32_t*)dig;
}
```
(квантование уточнять: в треде «half-degree», реализация функции в IDA.)

### ran1 (poluxen, полный)
```cpp
struct Ran1
{
    int idum = 0, iy = 0, iv[32]{};
    static constexpr int IA = 16807, IM = 2147483647, IQ = 127773, IR = 2836, NTAB = 32, NDIV = 1 + (IM - 1) / NTAB;
    static constexpr float AM = 1.f / IM, RNMX = 1.f - 1.2e-7f;
    void Seed(int s) { idum = s < 0 ? s : -s; iy = 0; }
    int Gen()
    {
        int j, k;
        if (idum <= 0 || !iy)
        {
            idum = -idum > 1 ? -idum : 1;
            for (j = NTAB + 7; j >= 0; --j)
            {
                k = idum / IQ; idum = IA * (idum - k * IQ) - IR * k;
                if (idum < 0) idum += IM;
                if (j < NTAB) iv[j] = idum;
            }
            iy = iv[0];
        }
        k = idum / IQ; idum = IA * (idum - k * IQ) - IR * k;
        if (idum < 0) idum += IM;
        j = iy / NDIV; iy = iv[j]; iv[j] = idum;
        return iy;
    }
    float Float(float lo, float hi)
    {
        float f = AM * Gen(); if (f > RNMX) f = RNMX;
        return f * (hi - lo) + lo;
    }
};
```

### CalcSpread (773657)
```cpp
using CalcSpread_t = void(__fastcall*)(uint16_t weaponId, int bullets, int mode, int seed, float inacc, float spread, float recoilIdx, float* x, float* y);

Vector2D CalcSpread(uint16_t def, int bullets, int mode, int seed, float inacc, float spread, float recoil)
{
    Ran1 r; r.Seed(seed);
    float r1 = r.Float(0.f, 1.f), a1 = r.Float(0.f, 2.f * PI);
    if (def == WEAPON_REVOLVER && mode == 1) r1 = 1.f - r1 * r1;
    if (def == WEAPON_NEGEV && recoil < 3.f)
        for (int i = 3; i > (int)recoil; --i) r1 *= r1;
    float x = 0, y = 0;
    for (int b = 0; b < bullets; ++b)
    {
        float r2 = r.Float(0.f, 1.f), a2 = r.Float(0.f, 2.f * PI);
        if (def == WEAPON_REVOLVER && mode == 1) r2 = 1.f - r2 * r2;
        if (def == WEAPON_NEGEV && recoil < 3.f)
            for (int i = 3; i > (int)recoil; --i) r2 *= r2;
        x = std::cos(a1) * r1 * inacc + std::cos(a2) * r2 * spread;
        y = std::sin(a1) * r1 * inacc + std::sin(a2) * r2 * spread;
    }
    return { x, y };
}
```
Дробовики: при `weapon_accuracy_shotgun_spread_patterns 1` вместо random — таблица `LookupShotgunSpreadPattern(def, mode, recoil * bullets + pellet)`. Учитывать `weapon_debug_max_inaccuracy`, `weapon_accuracy_nospread`, `weapon_inaccuracy_only_up`. Negev в декомпиле: возведение в квадрат, затем `1 - r` — сверять.

### Компенсация (RolekTV roll)
```cpp
QAngle NoSpread(QAngle a, Vector2D s)
{
    a.x += RAD2DEG(std::atan(std::sqrt(s.x * s.x + s.y * s.y)));
    a.z = -RAD2DEG(std::atan2(s.x, s.y));
    return a;
}
```
Перебор pitch шагом 0.5° до совпадения seed с нужным разбросом.

### Hitchance
Сэмплинг: N=256 seed, для каждого CalcSpread → направление → трейс по хитбоксу (капсула) → доля попаданий. Быстрый вариант (732117): площадь хитбокса / площадь круга разброса на дистанции.

### Камни
- PDXiZT/poluxen: углы, записанные в cmd, на seed не влияют — только `set_view_angles` (CCSGOInput view_angles 0x3D0 [stale?]).
- Tick в seed = tickbase или -1, ~50/50 — неоднозначность.
- 758433: при движении ломается (blend истории).

---

## 7. Engine prediction

### Треды
772855 / 763618 (полный Start/End), 764744 (double-shot и краш при дропе), 762175 (Bop32), 745090, 727791.

### Код (772855/763618 + фиксы 764744)
```cpp
struct PredBackup
{
    float curtime, frametime; int tickcount; float tickfraction;
    int tickbase; bool inPred, firstPred, hasBeenPred, shouldPred;
    Vector velocity, absVelocity;
    uint8_t movement[0x400];
};
PredBackup g_pb;

void PredStart(CCSPlayerController* ctrl, C_CSPlayerPawn* pawn, CUserCmd* cmd)
{
    auto* gv = GlobalVars();
    g_pb.curtime = gv->flCurTime; g_pb.frametime = gv->flFrameTime; g_pb.tickcount = gv->nTickCount;
    g_pb.tickbase = ctrl->m_nTickBase();
    auto* pred = Prediction();
    g_pb.inPred = pred->bInPrediction; g_pb.firstPred = pred->bFirstPrediction;
    g_pb.hasBeenPred = cmd->bHasBeenPredicted; g_pb.shouldPred = pred->bShouldPredict;
    g_pb.velocity = pawn->m_vecVelocity(); g_pb.absVelocity = pawn->m_vecAbsVelocity();
    auto* ms = pawn->m_pMovementServices();
    memcpy(g_pb.movement, ms, sizeof(g_pb.movement));
    *(CUserCmd**)((uintptr_t)ctrl + 0x778) = cmd;
    pred->bInPrediction = true; pred->bFirstPrediction = false;
    cmd->bHasBeenPredicted = false;
    SetPredictionCommand(pawn, cmd);
    CallVFunc<void, 32>(ms, cmd);
    ResetPredictionCommand(pawn);
    g_shootPos = CalculateShootPosition(pawn, TimeStamp{ ctrl->m_nTickBase(), 0.f });
}

void PredEnd(CCSPlayerController* ctrl, C_CSPlayerPawn* pawn, CUserCmd* cmd)
{
    auto* gv = GlobalVars();
    gv->flCurTime = g_pb.curtime; gv->flFrameTime = g_pb.frametime; gv->nTickCount = g_pb.tickcount;
    ctrl->m_nTickBase() = g_pb.tickbase;
    auto* pred = Prediction();
    pred->bInPrediction = g_pb.inPred; pred->bFirstPrediction = g_pb.firstPred; pred->bShouldPredict = g_pb.shouldPred;
    cmd->bHasBeenPredicted = g_pb.hasBeenPred;
    memcpy(pawn->m_pMovementServices(), g_pb.movement, sizeof(g_pb.movement));
    pawn->m_vecVelocity() = g_pb.velocity; pawn->m_vecAbsVelocity() = g_pb.absVelocity;
}
```
Индексы [stale?]: RunCommand vfunc ~32/33 movementservices (762175: RunCommand→ProcessMovement→vfunc 33), controller+0x778 = текущий cmd, размер movementservices — из schema.

### Камни
- Двойной выстрел — без полного memcpy backup/restore movementservices (764744).
- Краш при дропе оружия — не записан cmd в controller+0x778.
- client_side_predict даёт варпинг (745090); rebuild RunCommand вместо оригинала убирает джиттер (727791).
- Restore-список брать из глобалок в IDA; кнопки сохраняются в ProcessMovement.

---

## 8. Enemy prediction / lagcomp / interp / backtrack

### Треды
767930 (2026-08), 754454 (тролль «15s bt», дебанк AnnieGrow), 716979, 762583, 760769 (interp — не дочитан).

### Код (767930)
```cpp
struct LagRecord { float simTime; Vector origin; Matrix3x4 bones[128]; };

bool IsValid(const LagRecord& r, float curtime, float latency, float lerp)
{
    float maxUnlag = std::min(Cvar("sv_maxunlag")->GetFloat(), 0.2f);
    float delta = std::clamp(latency + lerp, 0.f, maxUnlag) - (curtime - r.simTime);
    return std::fabs(delta) < 0.2f && (curtime - r.simTime) < maxUnlag - latency - lerp;
}

CUserCmd* __fastcall hkGetUserCmdByNumber(void* a1, void* a2, int num)
{
    auto* cmd = oGetUserCmdByNumber(a1, a2, num);
    if (cmd && g_bt.active)
    {
        auto* e = cmd->csgoUserCmd.inputHistoryField.pRep->tElements[0];
        float t = g_bt.record->simTime / TICK_INTERVAL;
        int tick = (int)t; float frac = t - tick;
        e->nRenderTickCount = tick; e->flRenderTickFraction = frac;
        e->nPlayerTickCount = tick; e->flPlayerTickFraction = frac;
        e->nHasBits |= INPUT_HISTORY_BITS_RENDERTICKCOUNT | INPUT_HISTORY_BITS_RENDERTICKFRACTION | INPUT_HISTORY_BITS_PLAYERTICKCOUNT | INPUT_HISTORY_BITS_PLAYERTICKFRACTION;
    }
    return cmd;
}
```
- `m_nSimulationTick` возвращал -1 → тик считать из `m_flSimulationTime`.
- Lagcomp (762583): кости через `get_skeleton_instance` idx 11 [stale?]; скорость = Δorigin/Δsimtime, если совпал NoInterpolationTick.
- Экстраполяция (766191): origin += velocity * ticks * interval.

### Камни / противоречия
- «15s backtrack» — фейк: сервер клампит своим sv_maxunlag (0.2 дефолт, max 1) и sv_maxunlag_player, в логе сервера есть строка об этом (754454).
- Модельный лаг ~0.2 тика на 20 ms, ~1 тик на 80 ms (716979).

---

## 9. Skinchanger

### Треды
| id | дата | суть |
|---|---|---|
| 769568 | 2026-08-29, upd 2026-09-23 | ChadWare2 полный changer (оружие/нож/перчатки/агенты/стикеры/charms), исходник в аттаче file id 57957 |
| 762409 | 2026-07-15 | перчатки не применяются после S5; RebuildEconMaterials; перчатки ≠ bodygroup |
| 767844 | 2026-08-18 | glove changer через хук ConstructPaintKitInstance |
| 772679 | 2026-09-18 | перчатки слетают после покупки дефуза (не решено) |
| 775042 | 2026-10-03 | анимации ножа: AnimGraphRebuild на HUD-оружии |
| 754710 | 2026-05-27 | очистка HUD-иконки |
| 749081 | 2026-04-21 | что сломал animgraph_2_beta |
| 744355 | 2026-03-22 | джиттер вьюмодели, текстура ножа на других пушках |

### Шаги (оружие)
1. FSN stage 6 (dankor) или 7 (767844/762409) — **противоречие**, работают оба; FSN idx в Source2Client сдвигался (749081: IsInGame 39, IsConnected 40) [stale?].
2. Для каждого C_EconEntity локального игрока: `m_AttributeManager.m_Item` (C_EconItemView).
3. `m_iItemIDHigh = -1`, `m_iItemIDLow`, `m_iAccountID = local steamid32`, `m_iEntityQuality`, `m_bInitialized = 1`, `m_bDisallowSOC = 0`.
4. Атрибуты: Remove + Create (paintkit 6, seed 7, wear 8, stattrak 80/81) или fallback-поля `m_nFallbackPaintKit/Seed/Wear/StatTrak` на C_EconEntity.
5. Mesh mask: 1 = legacy-модель, 2 = новая (`SetMeshGroupMask`), на мировом и HUD оружии.
6. Регенерация материалов: `ClearGeneratedMaterials(ent+0x608)` + `RebuildEconMaterials(ent, true)` — **только парой** (без rebuild — чёрная модель).
7. HUD иконка — 754710.

### Почему не применяется на spawn-оружии (NotWoofless, 762409)
Заспавненное оружие копирует уже сгенерированные материалы с исходного:
```cpp
FUN_181420680(sourceEntity + 0x608, spawnedEntity + 0x608, 0);
FUN_1807C5CA0(weapon, 0);
```
Rebuild читает paintkit/seed/wear из `weapon + 0x11F8` [stale?]. RebuildEconMaterials sig: `40 55 53 41 57 48 8D AC 24 ? ? ? ? 48 81 EC 00 03 00 00`.
```cpp
void RegenerateSkins()
{
    for (auto* e = FirstEntity(it); e; e = NextEntity(it))
        if (e->customMaterialCount > 0 || e->visualDataCount > 0)
        {
            ClearGeneratedMaterials((uintptr_t)e + 0x608, true);
            RebuildEconMaterials(e, true);
        }
}
```

### Нож
```cpp
void ApplyKnife(C_CSWeaponBase* w, C_BaseModelEntity* hud, uint16_t def, const char* model, uint64_t mask)
{
    auto* item = &w->m_AttributeManager().m_Item();
    item->m_iItemDefinitionIndex() = def;
    item->m_iEntityQuality() = 3;
    w->m_nSubclassID() = SubclassHash(def);
    if (hud) hud->m_nSubclassID() = SubclassHash(def);
    SetModel(w, model);
    if (hud) SetModel(hud, model);
    UpdateSubclass(w);
    if (hud) UpdateSubclass(hud);
    SetMeshGroupMask(w, mask);
    if (hud) SetMeshGroupMask(hud, mask);
}
void OnKnifeDeploy(C_BaseModelEntity* hud)
{
    auto* ctrl = (uintptr_t)hud->m_CBodyComponent() + SCHEMA(CBodyComponentBaseAnimGraph, m_animationController);
    AnimGraphRebuild((void*)ctrl, 2);
}
```
- SubclassID — хеш имени (`MurmurHash2`/`CUtlStringToken` от def name); писать **до** UpdateSubclass на обоих.
- 775042: анимации берутся с HUD-оружия (`cs2_hudmodel_weapon`, искать по designer name); `AnimGraphRebuild(controller, 2)` один раз на holster→draw, не пока нож в руках.
- 775042: UpdateSubclass уничтожает указатель item → SetModel до него.
- 744355: джиттер/текстура на других пушках — меняли модель через childOwner рук (`m_hHudModelArms`) вместо HUD-оружия текущего ножа.

### Перчатки
Правильная модель (NotWoofless): **не bodygroup**; игра создаёт отдельную модель перчаток, прикрепляет к игроку, материал вешается на неё.
767844 (рабочий, FSN 7):
```cpp
void UpdateGloves(uintptr_t pawn)
{
    uintptr_t econ = pawn + C_CSPlayerPawn::m_EconGloves;
    const GloveInfo* g = GetSelectedGlove();
    if (*(uint16_t*)(econ + C_EconItemView::m_iItemDefinitionIndex) != g->itemDef || ChangedGloves)
    {
        *(bool*)(econ + C_EconItemView::m_bInitialized) = true;
        *(bool*)(econ + C_EconItemView::m_bDisallowSOC) = false;
        *(uint32_t*)(econ + C_EconItemView::m_iItemIDHigh) = -1;
        *(uint32_t*)(econ + C_EconItemView::m_iItemIDLow) = -1;
        *(uint32_t*)(econ + C_EconItemView::m_iAccountID) = -1;
        *(uint64_t*)(econ + C_EconItemView::m_iItemID) = -1;
        *(uint16_t*)(econ + C_EconItemView::m_iItemDefinitionIndex) = g->itemDef;
        SetBodyGroup(pawn, 0, 1);
        ChangedGloves = false;
        *(bool*)(pawn + C_CSPlayerPawn::m_bNeedToReApplyGloves) = true;
    }
}

inline uint16_t g_lastDef = 0xFFFF;
CPaintKit* __fastcall hkConstructPaintKitInstance2(__int64 item)
{
    g_lastDef = *(uint16_t*)(item + C_EconItemView::m_iItemDefinitionIndex);
    return oConstructPaintKitInstance2(item);
}
CPaintKit* __fastcall hkConstructPaintKitInstance(__int64 a1, int paintKit)
{
    if (IsGlove(g_lastDef))
        if (auto* g = GetSelectedGlove(); g && GetSelectedGloveSkin(*g).paintKit)
            paintKit = GetSelectedGloveSkin(*g).paintKit;
    g_lastDef = 0xFFFF;
    return oConstructPaintKitInstance(a1, paintKit);
}
```
Альтернатива (762409): атрибуты 6/7/8 на m_EconGloves + `m_bNeedToReApplyGloves = 1` — после S5 у автора не работало, а хук paintkit работает.

### HUD иконка (754710, ipatinhu)
```cpp
namespace hud
{
    struct panel_t
    {
        uintptr_t base = 0, data = 0; int count = 0;
        bool valid() const { return data && count > 0 && count <= 64; }
    };
    inline uintptr_t find_element(const char* name)
    {
        using fn_t = uintptr_t(__fastcall*)(const char*);
        static auto fn = (fn_t)scan("4C 8B DC 53 48 83 EC ? 48 8B 05");
        return fn ? fn(name) : 0;
    }
    inline void clear_icon(uintptr_t panel, int slot)
    {
        static auto site = scan("E8 ? ? ? ? 8B F8 C6 84 24");
        if (!site) return;
        static auto fn = (void(__fastcall*)(uintptr_t, int, int64_t))(site + 5 + *(int32_t*)(site + 1));
        fn(panel, slot, 0);
    }
    inline panel_t weapon_panel()
    {
        panel_t p{};
        if (auto e = find_element("HudWeaponSelection"))
        {
            p.base = e - 0x98;
            p.data = *(uintptr_t*)(p.base + 0x58);
            p.count = *(int*)(p.base + 0x50);
        }
        return p;
    }
    inline void clear_icon_for(C_BaseEntity* weapon)
    {
        auto p = weapon_panel();
        if (!weapon || !p.valid()) return;
        for (int i = p.count - 1; i >= 0; --i)
        {
            int h = *(int*)(p.data + i * 72 + 0x38);
            if (h < 0) continue;
            if (EntitySystem()->GetBaseEntity(h & 0x7FFF) == weapon) { clear_icon(p.base, i); return; }
        }
    }
}
```
Проще (Maxedg1): `FindElement("HudWeaponSelection")->ResetHUD()` (vfunc 3) — сбрасывает весь HUD. Killfeed-иконка: слушать player_death и подменять weapon.

### Что ломается после раунда/смерти/событий
- Spawn-оружие наследует старые материалы → нужен Clear+Rebuild (762409).
- Смена агента меняет дефолтные перчатки → перчатки не применяются, переприменять после смены агента (769568).
- Покупка дефуза → перчатки дефолтные до конца матча, m_EconGloves не меняется (772679, не решено; похоже, рендер берёт не m_EconGloves).
- Удаление последнего стикера (1→0): композит не пересобирается → `CCompositeMaterialOwner::ReleaseCompositeMaterials()` только на переходе >0→0, на мировом и HUD оружии (769568).
- Крах ножа после второй смерти / в конце матча (744107, 763619) — не прочитаны.
- Апдейт 2026-09-23 ломал changer'ы; `GetEconItemSystem` vfunc 129, «что-то поменялось на 0x610» (LoverofHate).

---

## 10. ImGui DX11 Present, шрифты, W2S

Треды 720196 (шрифт/антиалиасинг), 770675 (краш D3DCompile), 725060 (aspect ratio), 705670 (W2S) **не прочитаны** — Chrome-разрешение истекло. Ниже стандартная реализация без UC-специфики.

```cpp
using Present_t = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT);
using ResizeBuffers_t = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
Present_t oPresent; ResizeBuffers_t oResize;
ID3D11Device* g_dev; ID3D11DeviceContext* g_ctx; ID3D11RenderTargetView* g_rtv; HWND g_hwnd; WNDPROC oWndProc;

void CreateRTV(IDXGISwapChain* sc)
{
    ID3D11Texture2D* bb = nullptr;
    sc->GetBuffer(0, IID_PPV_ARGS(&bb));
    D3D11_RENDER_TARGET_VIEW_DESC d{}; d.Format = DXGI_FORMAT_R8G8B8A8_UNORM; d.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    g_dev->CreateRenderTargetView(bb, &d, &g_rtv);
    bb->Release();
}

HRESULT __stdcall hkPresent(IDXGISwapChain* sc, UINT sync, UINT flags)
{
    static bool init = false;
    if (!init)
    {
        sc->GetDevice(IID_PPV_ARGS(&g_dev)); g_dev->GetImmediateContext(&g_ctx);
        DXGI_SWAP_CHAIN_DESC sd; sc->GetDesc(&sd); g_hwnd = sd.OutputWindow;
        CreateRTV(sc);
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        ImFontConfig cfg; cfg.OversampleH = 3; cfg.OversampleV = 1; cfg.PixelSnapH = true;
        io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\verdana.ttf", 14.f, &cfg, io.Fonts->GetGlyphRangesCyrillic());
        ImGui_ImplWin32_Init(g_hwnd); ImGui_ImplDX11_Init(g_dev, g_ctx);
        oWndProc = (WNDPROC)SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, (LONG_PTR)hkWndProc);
        init = true;
    }
    ImGui_ImplDX11_NewFrame(); ImGui_ImplWin32_NewFrame(); ImGui::NewFrame();
    Render(ImGui::GetBackgroundDrawList());
    ImGui::Render();
    g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    return oPresent(sc, sync, flags);
}

HRESULT __stdcall hkResizeBuffers(IDXGISwapChain* sc, UINT n, UINT w, UINT h, DXGI_FORMAT f, UINT fl)
{
    if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
    HRESULT hr = oResize(sc, n, w, h, f, fl);
    CreateRTV(sc);
    return hr;
}

bool W2S(const Vector& p, ImVec2& out)
{
    const auto& m = *g_viewMatrix;
    float w = m[3][0] * p.x + m[3][1] * p.y + m[3][2] * p.z + m[3][3];
    if (w < 0.001f) return false;
    float x = m[0][0] * p.x + m[0][1] * p.y + m[0][2] * p.z + m[0][3];
    float y = m[1][0] * p.x + m[1][1] * p.y + m[1][2] * p.z + m[1][3];
    auto ds = ImGui::GetIO().DisplaySize;
    out.x = ds.x * 0.5f + x / w * ds.x * 0.5f;
    out.y = ds.y * 0.5f - y / w * ds.y * 0.5f;
    return true;
}
```
Камни (общие): RTV пересоздавать в ResizeBuffers; формат RTV у CS2 swapchain может быть sRGB/HDR — брать из `sd.BufferDesc.Format`; view matrix читать в том же кадре, что и позиции (иначе дрожание ESP); DisplaySize при stretched-разрешении ≠ backbuffer (тема 725060). Pattern тредов не подтверждён — если нужно, дочитаю после разрешения в Chrome.
