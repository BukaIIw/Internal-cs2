# UC: [CS2] Вычисление значений Spread (пример, сигнатуры старые, использовать только как реализацию)

```cpp
int64_t pContext;
float unk_val = 0;
int iMode = 1;
// client.dll "48 89 5C 24 08 57 48 83 EC 30 0F 29 74 24 20 45 8B"
BacktrackLocalPlayer((__int64)pLocalWeapon, (__int64)&pContext, unk_val, iMode);

// client.dll "48 81 EC F8 00 00 00 F3 0F 10 09"
// viewangles <- angles when you are shooting
auto random_seed = GetRandomSeed(&viewangles, render_tick);

g_pWinApi->m_RandomSeed(random_seed + 1);   // tier0 RandomSeed export

float flRadiusCurveDensity = g_pWinApi->m_RandomFloat(0, 1.f);   // tier0 RandomFloat export
float fTheta3 = g_pWinApi->m_RandomFloat(0.0f, 2.0f * M_PI);
float flSpreadCurveDensity = flRadiusCurveDensity;

auto weapon_accuracy_shotgun_spread_patterns = (int64_t)(client_dll + 0x186C930); // convar
auto unk_addr3 = (int64_t)(client_dll + 0x1676AF8);

int* pItem = (int*)((int64_t)pLocalWeapon + 0x1090);

// client.dll "48 81 EC 38 01 00 00 48 85 C9 75 0A 33 C0 48 81 C4 38 01 00 00 C3 48 89"
auto pWeaponData = GetWeaponData(pItem);
auto nWeaponBullets = *(DWORD*)(pWeaponData + 268);

// client.dll "E8 ?? ?? ?? ?? 48 85 C0 75 E9"
auto v91 = GetConvarValue(weapon_accuracy_shotgun_spread_patterns, -1);
float theta_1;
auto flRecoilIndex = *(float*)((int64_t)pLocalWeapon + 5828);
float my_flSpreadCurveDensity = 0;

if (*v91)
{
    auto v92 = 0 + nWeaponBullets * (int)flRecoilIndex;
    auto v93 = *(int*)(uintptr_t(pItem) + 0x1BA);
    // client.dll "66 89 54 24 10 55 48"
    RandomizeSomeShit((__int64)unk_addr3, v93, iMode, v92, &theta_1, &my_flSpreadCurveDensity);
}

float flSpread, flInaccuracy;
auto flSpread1 = (*(double(__fastcall**)(__int64))((*(int64_t*)pLocalWeapon) + 2856))((int64_t)pLocalWeapon);   // vfunc 357 GetSpread
auto flInaccr1 = (*(double(__fastcall**)(__int64))((*(int64_t*)pLocalWeapon) + 3216))((int64_t)pLocalWeapon);   // vfunc 402 GetInaccuracy
flSpread = *(float*)&flSpread1;
flInaccuracy = *(float*)&flInaccr1;

double fRadius1 = my_flSpreadCurveDensity * flSpread;
double fRadius0 = flRadiusCurveDensity * flInaccuracy;

double y_res_value = sinf(fTheta3) * fRadius0 + sinf(theta_1) * fRadius1;
double x_res_value = cosf(fTheta3) * fRadius0 + cosf(theta_1) * fRadius1;
```

Компенсация (обратно к FireBullet, client.dll "48 89 5C 24 18 48 89 54 24 10 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 90"):

```cpp
AngleVectors(res_vec, vecDirShooting, vecRight, vecUp);
// pseudo: vecDir = vecDirShooting - vecRight * spreadX + vecUp * spreadY
vecAntiDir = vecDirShooting + vecRight * pr_x - vecUp * pr_y;
vecAntiDir.NormalizeInPlace();
VectorAngles(vecAntiDir, vecAntiSpread);
```

Нюансы автора: считать лучше в FrameStageNotify (player_render_tick обновляется каждый кадр). GetRandomSeed зависит от угла выстрела: меняешь угол — сид меняется, nospread надо считать от финального угла. Нет условий для negev/revolver. Проверено хуком FireBullet_CS2: аргументы 13 и 14 = spreadX, spreadY.
