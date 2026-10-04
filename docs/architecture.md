# Архитектура

Internal DLL для CS2 (x64, MSVC v145, C++20, `/EHa`, MinHook, ImGui DX11). Корень include — `src/`. Контракт модулей: `docs/research/recode-spec.md`.

## Слои
Зависимости идут только сверху вниз: `ui` → `features` → `systems` → `core`.

| Слой | Папка | Содержимое |
|---|---|---|
| core | `src/core` | память и паттерны, schema, convar, шина событий, хуки, бинды, настройки, математика, лог |
| systems | `src/systems` | игровое состояние: сущности, локальный игрок, глобалы, prediction, ввод/usercmd, view (углы и выстрел), трейсы |
| features | `src/features` | фичи; каждая подписывается на события в `features.cpp` |
| ui | `src/ui` | шрифты, тема, метрики, виджеты, меню |

Старые модули, оставленные как есть: `items.*`, `kv.*`, `vpk.*`, `icons.*` (база предметов, VPK, иконки), `ui/svg.*`, `ui/icons_svg.h`, `core/log.*`.

### core
- `memory` — `read/write/ref/call/call_vfunc`, модули, экспорты, интерфейсы, поиск паттернов, RTTI-vtable (`find_vtable`), `is_readable`.
- `addresses` + `patterns` — реестр паттернов в синтаксисе владельца (`"module.dll:HEX ?? * > +OFF ~"`). `PATTERN(patterns::name)` резолвит и кэширует адрес на месте использования; при `~` или незагруженном модуле повторяет попытку не чаще раза в 500 мс. `addresses::globals::*` — интерфейсы и глобалы (entity system, global vars, CSGOInput, item system, trace manager, view matrix). Инструменты Settings → Patterns: verify по образу модуля с диска, генератор уникальной сигнатуры (hde64), экспорт в `%APPDATA%\Internal-cs2\patterns.txt`; работа в фоновом потоке.
- `schema` — SchemaSystem_001: `SCHEMA("Class", "m_field"_hash)` возвращает смещение с учётом базовых классов, кэш под мьютексом, отсутствующие поля логируются и видны в `schema::missing()`.
- `convar` — `CONVAR("name")`: поиск ConVarRef по строке в `.rdata` client.dll/engine2.dll, при неудаче пустой convar (`get<T>()` = `T{}`).
- `events` — шина событий (ниже).
- `hooks` — реестр хуков, детуры, запуск и выгрузка; `runtime.h` — внутреннее состояние рантайма.
- `keys` — реестр биндов (`keys::bind`: key + mode always/hold/toggle/off), таблица клавиш из WndProc, захват клавиши для UI.
- `settings` — структуры `settings::g_*` и реестр полей по строковым ключам.
- `math`, `cstypes`, `hash` (MurmurHash2 lower), `log` (кольцевой лог и тосты).

### systems
- `g_entities` — снимок игроков (контроллер, пешка, команда, здоровье, origin, bbox, оружие, видимость) на стадии FSN `update`, хранится под мьютексом.
- `g_local` — локальный контроллер/пешка, eye, флаги, move type, оружие и его vdata, tick base.
- `globals` — `tick_count`, `interval`, `in_game`.
- `g_prediction` — сетевое состояние перед тиком (`pre()`): флаги, скорость, origin, friction, stamina, max speed, gravity scale. Движковой prediction нет.
- `g_input` — `input::frame` (буфер subtick-ввода до сборки cmd: кнопки, forward/left, события с `when` и абсолютными углами) и `input::usercmd` (protobuf CUserCmd: history, base, кнопки, attack1 index). `find_cmd` берёт cmd по паттернам владельца, иначе используется cmd, пойманный хуком GetUserCmd.
- `g_view` — единственное место, где меняются углы и нажимается атака. Фичи вызывают `aim(angle, silent, priority, owner)` (побеждает больший приоритет), `fire(when)`, `block_fire()`. `apply(frame)` вставляет press/release атаки и non-silent углы, `apply(cmd)` пишет silent-углы во все записи input history (опционально плавно), кнопки и attack1 index. Semi-auto: после выстрела следующий тик пропускается.
- `g_tracing` — line/hull трейс через GameTraceManager + CTraceFilter, `is_visible` (мировой трейс, затем выстрельный с допуском 8u), `trace_player_bbox` для мувмента, `fire_bullet` (autowall) под SEH; при сбое `bullets_ready()` = false и фичи откатываются на видимость + формулу урона.

### features
Все объекты фич — `inline` глобалы `g_*` в заголовке своей папки.

| Папка | Фичи |
|---|---|
| `features/changer` | `econ_item_system` (схема предметов, локализация, скины по оружию; отдельный поток), guns, knives (subclass, HUD-оружие, анимации), gloves, agents; `on_frame_stage`, `on_unload` с восстановлением оригиналов |
| `features/combat` | `shared` (контекст оружия, inaccuracy/spread, punch), `hitbox` (кости, хитбоксы, multipoint), `hitchance` (порт Neverlose hitchance/force shot), `spread`, `rage`, `legit`, `trigger` |
| `features/movement` | jumpbug, bunnyhop, airstrafe, test_strafer (subtick strafer), quickstop, fastladder |
| `features/visuals` | ESP (снимок на FSN, отрисовка в Present), glow (запись `CGlowProperty` с восстановлением, хук IsGlowing при наличии), chams/hands tint (хук DrawSceneObject) |
| `features/misc` | thirdperson (CameraThink + OverrideView), overlay (watermark, список биндов) |

### ui
`ui/metrics.h` — все размеры (масштаб × DPI), `ui/fonts` — реестр шрифтов по ролям, `ui/theme` — токены цветов от акцента, `ui/render` — примитивы рисования, `ui/ui` — виджеты, `ui/menu*.cpp` — страницы Rage, Legit, Visuals, Movement, Skins, Misc, Settings (patterns, events, hooks, schema, log, тема, Save/Load, Unload), `ui/catalog` — данные предметов для страницы скинов (econ, пока не готов — `items::`).

## Шина событий (`core/events`)
Типы: `frame_stage`, `create_move`, `create_move_post`, `override_view`, `present`, `level_init`, `level_shutdown`, `local_pawn_changed`, `menu_toggle`, `unload`. Подписчики хранятся в векторе на тип, отсортированы по приоритету (равные — в порядке подписки), заполняются при старте до включения хуков. Каждый вызов обёрнут в SEH: упавший подписчик отключается и пишется в лог, Settings → Events показывает вызовы, среднее/пиковое время и кнопку повторного включения.

Приоритеты (`events::priority`): `first` -100, `systems` -50, `movement_pre` -20, `combat` 0, `movement` 20, `view` 50, `normal` 60, `last` 100.

Порядок в `create_move` (кадр `input::frame*`): view begin → combat context → jumpbug → rage → legit → trigger → bhop → airstrafe → subtick strafer → quickstop → fastladder → view apply → thirdperson. В `create_move_post` (`input::usercmd*`): rage → view apply → view end. В `frame_stage`: local → entities → changer, rage, esp, glow. В `present`: esp → overlay → menu. `unload`: сохранение настроек → changer, glow, thirdperson → остановка потока паттернов.

## Хуки (`core/hooks.cpp`)
Обязательные: Present (DXGI vt 8), FrameStageNotify, CreateMove (CCSGOInput vt 5), MergeSubtick. Опциональные: ResizeBuffers, GetUserCmd, OverrideView, CameraThink, DrawSceneObject, IsGlowing. Для FSN/CreateMove/DrawSceneObject есть запасной путь через vtable. Каждый детур первым проверяет `hooks::unloading` и держит RAII-счётчик активных вызовов. Статус хуков — Settings → Hooks.

- Present: инициализация ImGui, шрифтов, меню, иконок, подмена WndProc; кадр ImGui строится только при `features::wants_overlay()`; публикует `menu_toggle` и `present`.
- WndProc: клавиша меню (`settings::g_ui.menu_key`, по умолчанию Insert), `keys::on_key`, при открытом меню ввод уходит в ImGui и не передаётся игре.
- FrameStageNotify: оригинал, затем `frame_stage`; на стадии `update` трекер сессии публикует `level_init`/`level_shutdown`/`local_pawn_changed`; при выгрузке один раз публикует `unload`.
- CreateMove: `g_local.update()`, поточный гейт, оригинал (внутри MergeSubtick), поиск cmd, `create_move_post`, при изменениях — пересчёт cmd (vfunc 6).
- MergeSubtick: заполняет `g_prediction`, открывает `input::frame` и публикует `create_move`.

## Потоки
- Игровой поток: FrameStageNotify, CreateMove — все записи в память игры и фичи.
- Поток рендера: Present/WndProc — читает только снимки под мьютексом (`g_entities.players()`, `g_local.get()`, буфер ESP).
- Поток загрузчика (`dllmain`): ждёт client.dll, вызывает `hooks::initialize()`, следит за End, выполняет выгрузку.
- Фоновые: econ item system, загрузка `items::`, воркер паттернов. Все join при выгрузке.

## Запуск и выгрузка
Запуск: MinHook → `addresses::resolve_all()` → `schema::initialize()` (повтор до 10 с) → `settings::register_all()` + `load()` → `g_input`/`g_tracing.initialize()` → `features::register_all()` + `initialize()` → поток `items::Load` → установка и включение хуков.

Выгрузка (End или кнопка Unload → `hooks::unloading`): ожидание `unload` на игровом потоке (восстановление скинов, glow, камеры, сохранение настроек) → отключение хуков → возврат WndProc → ожидание выхода из детуров → удаление хуков → join потоков → освобождение ImGui/D3D → `FreeLibraryAndExitThread`. Если детуры не вышли, DLL остаётся в памяти.

## Файлы настроек
Папка `%APPDATA%\Internal-cs2\`:
- `settings.ini` — все поля `settings::g_*` по ключам (`rage.*`, `legit.*`, `trigger.*`, `movement.*`, `visuals.*`, `misc.*`, `ui.*`, `bind.*`), запись атомарная (tmp + MoveFileEx). Сохраняется при закрытии меню, кнопкой Save и при выгрузке.
- `inventory.ini` — инвентарь чейнджера (`uid def paint seed wear stattrak eqT eqCT nametag|stickers`) и агенты, под `settings::g_changer_mutex`.
- `patterns.txt` — экспорт реестра паттернов.

## Как добавить фичу
1. Настройки: поле в нужной структуре `src/core/settings.h` и `add("раздел.ключ", поле)` в `settings::register_all()` (`src/core/settings.cpp`); бинд — поле `keys::bind` с ключом `bind.*`.
2. Код: класс с `on_create_move(input::frame&)` / `on_frame_stage(int)` / `on_present(ImDrawList*)` в папке своей категории, `inline` глобал `g_*` в её заголовке, реализация в новом `.cpp` (добавить в `Internal-cs2.vcxproj`).
3. Подписка: строка `events::subscribe(type, "имя", обработчик, приоритет)` в `src/features/features.cpp`; восстановление памяти игры — подписка на `unload`.
4. Доступ к игре только через `memory::*`, `SCHEMA`, `PATTERN`, `CONVAR`, `INTERFACE_`; углы и атака — только через `systems::g_view`. Отрисовка оверлея — учесть в `features::wants_overlay()`.
5. UI: контролы на нужной странице в `src/ui/menu*.cpp`, размеры только из `ui/metrics.h`.

## Как добавить паттерн
1. В `src/core/patterns.h`: `extern const ::addresses::entry имя;`.
2. В `src/core/patterns.cpp`: `const ::addresses::entry имя{ "имя", "module.dll:HEX..." };` и указатель в массив для `patterns::all()`.
3. Использование: `PATTERN(patterns::имя)` в месте вызова (не сохранять адрес в своих глобалах). Проверка — Settings → Patterns → Verify; новую сигнатуру можно получить там же через генератор.
