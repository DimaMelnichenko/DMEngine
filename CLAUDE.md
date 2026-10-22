# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Проект

DMEngine — самописный 3D-движок на C++17 / Direct3D 12 под Windows (Win32-окно, Raw Input, ImGui).
Бэкенд — D3D12 в современном виде (Agility SDK, DXC и SM 6.6, bindless, enhanced barriers, D3D12MA, PIX), устройство слоя —
`docs/d3d12.md`; переписан с D3D11 2026-09-30, последний коммит на D3D11 — `0aadc42` (в истории git; эталон кадров теперь — снимки D3D12, `docs/d3d12.md` §4).
Сейчас в основном используется как полигон для рендеринга: террейн (CDLOD),
небо, расстановка травы и декора (compute + indirect draw), частицы.

**Цель** — «Долина ручьёв» (название — Dimok, 2026-10-23): живая горная долина по мотивам Valley Benchmark от Unigine
([статья на Хабре](https://habr.com/ru/companies/unigine/articles/184614/): долина 8 × 8 км, рельеф с эрозией, лес,
луга и цветы по маскам), но живее: звери, птицы, насекомые, рыбы, настоящие ручьи; позже — деревушка с жителями со своим
распорядком дня. С ней сверяются решения и порядок этапов в `TODO.md`: большой открытый мир, растительность, вода,
живность, сутки (время дня и ночи — общие часы для неба и жизни). Картинки-ориентиры — как долина должна выглядеть:
`docs/reference/alpine_valley_sunset.png` (вся долина) и `docs/reference/valley_of_streams.png` (нижняя подробная часть:
ручьи по лугу); что для них не хватает — пункты «картинка-ориентир» и «ориентир 2» в `TODO.md`.

## Текущая работа

План доработок и их статус — в `TODO.md`. В начале сессии прочитайте его. Выполненный пункт отметьте `[x]`
и перенесите в `DONE.md` — журнал выполненного в порядке выполнения (в конец, с итогом: что сделано, где в коде, что
показала проверка); найденные по ходу проблемы добавьте в соответствующий раздел `TODO.md`.

Закрывая пункт (целиком или частью), пройдитесь по всему `TODO.md` поиском по именам и понятиям сделанного (таблицы,
классы, файлы, слова вроде «русло», «кривая», «очистка»): то же самое часто записано подпунктом в других пунктах. Сделанное
там вычеркните (или отметьте «сделано, см. `DONE.md`»), устаревшее после смены подхода перепишите под новый, лишнее
уберите; в отчёте назовите, какие ещё пункты затронуты.

Архитектурные уязвимости и недочёты, замеченные при написании кода (скрытое состояние, дублирование логики,
жёстко зашитые значения, то, что мешает добавлять фичи), тоже записывайте в `TODO.md` — с местом в коде и
предлагаемым решением. Раздел «Дальше, по приоритету» разбит на этапы по зависимостям: ставьте пункт в тот этап,
после которого его можно сделать без переписывания, и поднимайте выше, если от него зависят другие; если пункт меняет
подход к уже запланированным, пересмотрите и их порядок.

Документация по функционалу движка для людей — `docs/` (оглавление `docs/README.md`, пишется по-русски). Меняя
подсистему, у которой есть раздел, обновите и его; новую подсистему описывайте там же по образцу `docs/scatter.md`.

## Коммиты в этом репозитории

Исключение из глобального запрета на коммиты (`~/.claude/CLAUDE.md`) — только для DMEngine, по решению владельца:
- **Коммитить можно** законченный пункт: собранный (Debug и Release), проверенный запуском, с обновлёнными `docs/`,
  `TODO.md` / `DONE.md` и `graphify update .`.
- **Сообщение** — один короткий заголовок по-русски, по сути, не больше 10 слов. Без тела и без строк
  `Co-Authored-By` и других пометок об ИИ.
- **Файлы — явными путями** (`git commit -- <пути>`): только то, что относится к пункту. Не коммитить `imgui.ini`,
  `.run/*.run.xml` (их меняет CLion), `DownloadResources\` и чужие незаконченные правки кода в рабочей копии.
- **Правки `base.db3` от Dimok** — он сохранил их в редакторе сознательно («Save level»): не оставлять в рабочей копии, а
  коммитить отдельным коммитом (заголовок — что подобрано), до или после коммита пункта.
- **Без публикации:** `git push`, `git tag`, MR / PR, переписывание истории, уже отправленной в origin, — только
  владелец. Коммит как следующий шаг не предлагать: делать его в конце пункта и называть в отчёте.
- **Копия репозитория на NAS** (Synology Git Server — git по SSH, без веб-интерфейса): голый репозиторий
  `~/git/DMEngine.git` пользователя `dimok`. У `origin` два адреса отправки — GitHub и NAS, `git push` идёт в оба;
  remote `nas` — отдельно (`git push nas v2` — дослать, если NAS был недоступен: вне домашней сети отправка на NAS
  падает, GitHub при этом обновлён). Настройка на другой машине: в `~/.ssh/config` — `Host nas` (`HostName 192.168.1.80`,
  `User dimok`, ключ, который есть в `authorized_keys` NAS, `IdentitiesOnly yes`), затем
  `git remote add nas nas:git/DMEngine.git`, `git remote set-url --add --push origin https://github.com/DimaMelnichenko/DMEngine`,
  `git remote set-url --add --push origin nas:git/DMEngine.git`; проверка — `git push --dry-run origin v2` (две цели).

## Сборка и запуск

Главное; подробности — `docs/build.md`.

- Проект только на CMake (`CMakeLists.txt`), только MSVC x64; CLion (toolchain Visual Studio, amd64, Ninja) и Visual
  Studio открывают его напрямую. Зависимости — `FetchContent` при первом configure (нужны сеть и git): DirectXTex
  (`may2026`), SQLiteCpp (`3.3.3`), tinyexr (`v1.0.13`), Agility SDK 1.619 (`D3D12Core.dll` и debug-слой — в `D3D12\`
  рядом с exe), DXC v1.9 (`dxcompiler.dll`, `dxil.dll`), WinPixEventRuntime, Dear ImGui 1.92.9b docking и ImGuizmo
  (архивы — из `DownloadResources\`, если есть, иначе с GitHub), D3D12 Memory Allocator 3.2, EnTT (`v3.16.0`), Jolt
  Physics (`v5.6.0`), doctest (`v2.4.11`); остальное — Windows SDK.
- Цели CMake: `DMCore` — статическая библиотека, ядро без устройства (свойства, лог, данные уровня, время суток, рельеф и
  вода на CPU, ветер, звук, камера, ходьба, ввод, мир симуляции, столкновения, анимация; список файлов — в
  `CMakeLists.txt`), `DMEngine` — exe поверх неё, `DMEngineTests` — тесты логики на CPU (doctest); общие флаги —
  `DMEngineSettings`.
- **Из терминала собирать только скриптом**, а не вызывать cmake вручную:
  ```
  Tools\build.cmd debug        (или release; из Git Bash: cmd //c "Tools\build.cmd debug")
  ```
  Скрипт находит Visual Studio (vswhere, `vcvars64`), конфигурирует `cmake-build-cli-<debug|release>` в UTF-8 (метка
  `configured-utf8`; без неё ninja теряет зависимости от заголовков), сбрасывает `NoDefaultCurrentDirectoryInExePath`
  (иначе у DirectXTex молча падает `CompileShaders.cmd`) и собирает; код выхода — результат. Папки
  `cmake-build-debug*` — профили CLion: из терминала в них не собирать («Permission denied» при перезагрузке CMake).
- **Тесты логики на CPU** — `Tools\test.cmd [debug|release] [параметры doctest]` (из Git Bash —
  `cmd //c "Tools\test.cmd release"`, `-tc=WalkMode*` — по имени): тесты — `tests/test_<подсистема>.cpp`, тестовый exe
  берёт ядро целиком (`/WHOLEARCHIVE:DMCore`) — ссылка файла ядра на устройство, ImGui, DirectXTex или базу — ошибка
  линковки; `docs/tests.md`. Линтера нет.
- **Проверка изменений рендера** — скил `render-check` и `docs/d3d12.md` §4: снимки пяти контрольных камер совпадают с
  эталоном ±1, «GPU average» не хуже, Debug молчит в `log.txt`. Одной командой — `python Tools/check_frames.py`
  (эталон до правки — `check_frames.py reference`; `--config Debug` — с debug-слоем; в Release — с закреплённой частотой
  GPU и состоянием GPU под каждой камерой). GPU-based validation (`-GpuValidation` / `--gpu-validation`) — только для
  правок слоя D3D12. Эталон D3D11 — коммит `0aadc42`.
- **Запуск** — `.\Tools\run.ps1 [-Config Release] [-Seconds 8] [-Screenshot кадр.png] [-Keys 2,4] [-Camera x,y,z,pitch,yaw]
  [-Level имя] [-Resolution 1920x1080|desktop] [-NoGui] [-NoMouse] [-NoWind] [-NoParticles] [-FreezeTime] [-NoColorGrading]
  [-Preset имя] [-GpuValidation] [-StableClocks] [-Sound]` (запрещены скрипты — `powershell -ExecutionPolicy Bypass -File
  …`): движок с удалённым управлением, снимок заднего буфера, штатный выход, из `log.txt` — ошибки, заглушки, время
  инициализации, «GPU average» и «CPU average». **Серия проверок за один запуск** — `python Tools/engine.py start
  [--config Debug] [--nogui] …`, команды `camera x,y,z,pitch,yaw` (смена плана: экспозиция сразу, снимок и замер ждут 10
  кадров), `screenshot файл.png [gui] [кадров]`, `capture`, `stat gpu 3`, `stat cpu 3`, `list` / `get` / `set "окно/свойство"
  значение`, `press`, `key`, `wait`, `timestep`, `audio record`, `pix`, сценарий — `engine.py run файл.txt`, выход —
  `engine.py stop`; ответ — не дольше 60 с, иначе строки сторожа кадра (`FrameWatchdog`). Всё о командах и канале
  `\\.\pipe\DMEngine` — `docs/remote.md`. Для сравнения кадров — `--nowind --noparticles --freezetime` (трава,
  частицы и вода стоят), без окон — `--nogui`; `screenshot … gui` — с окнами ImGui. Окно и задний буфер — `-resolution`
  (`engine.py` и `run.ps1` по умолчанию 1920×1080, `desktop` — весь монитор). Пресеты окружения (`LevelPresets`, `-preset`):
  Dimok подбирает окружение в своём, проверки — с заблокированным `Reference`. Камера и уровень при старте — секции
  `[Camera]` и `[Level]` в `settings.ini` или `-camera` / `-level`; ближняя и дальняя плоскости — `ScreenNear` /
  `ScreenDepth` в `[General]` (0,1 м и 100 км). Сравнение снимков — `python Tools/compare_frames.py a.png b.png
  [--tolerance 1] [--diff карта.png]`; ±1 в сотнях пикселей между запусками — округление экспозиции.
- **Часть невызываемого кода сохранена намеренно**: вызовы закомментированы в последних коммитах — временно выключенные
  фичи (например, тело `LibraryLoader::save`). Не удалять без согласования: сначала `git blame`. Прочий мёртвый код удалён.
- Debug-сборка — устройство с debug-слоем D3D12 (из redist Agility SDK): ошибки и предупреждения — в `log.txt`
  (callback `ID3D12InfoQueue1`, одинаковые — первые три раза), при потере устройства — последние команды (DRED).
  «Лог чист» — только при строке «debug layer is enabled» в `log.txt`. Крах в Release: смещение из журнала Windows
  (`Get-WinEvent`, Id 1000) ищется в `DMEngine.map` рядом с exe (линкер с `/MAP`).
- Запускать из корня проекта: пути относительные (`settings.ini`, `base.db3`, `Shaders\`, `Textures\`, `Meshes\`,
  `Sounds\`; для CLion — `.run/DMEngine.run.xml`). `Textures\`, `Meshes\`, `Sounds\` и `DownloadResources\` в git не
  хранятся; без них — заглушки (в `log.txt` перечислено, что не загрузилось). Копия — на NAS: `python Tools/nas_sync.py
  [status|push|pull] [--dry-run]` (без удалений; из PowerShell или cmd — из песочницы Git Bash SSH в локальную сеть не
  ходит); на новой машине — `pull`. Рецепты данных — скил `asset-import`: карта высот и рельеф (`docs/terrain.md`), дальние
  горы (`docs/far_terrain.md`), модели и растения (`docs/models.md`), звуки (`docs/audio.md`). Blender 5.0 —
  `C:\Program Files\Blender Foundation\Blender 5.0\blender.exe` (не в PATH).
- Лог каждого запуска — `log.txt` (макрос `LOG(x)`, `src/Logger/Logger.h`), не в git, как и снимки клавишей P.
- Шейдеры (`Shaders/*.vs|.ps|.gs|.cs`, include — `*.sh`) компилирует DXC во время выполнения (SM 6.6, HLSL 2021,
  `D3D/ShaderCompiler.h`): правка шейдера пересборки не требует; кэш DXIL — `cache/shaders/` (ключ — в том числе отпечаток
  всего `Shaders\`). Ресурсы — bindless: `DM_SRV( тип, имя, слот )` / `DM_UAV` (`Shaders/bindless.sh`), ресурсы сцены
  t100… — по индексам из b10, константные буферы — `register( SLOT_CB_… )`, сэмплеры статические. Общие номера слотов —
  `Shaders/slots.h`, буферы констант — одним описанием для C++ и HLSL в `Shaders/<подсистема>_constants.h` (`DM_CONSTANTS`
  / `DM_FIELD`; раскладку в Debug сверяет `ShaderCompiler`). Пайплайны собираются при загрузке по спискам прогрева
  (`Renderer::warmPipelines`, кэш `cache/pipelines.bin`); собранный в кадре — «ленивый» (строка в лог) — дополнить
  список прогрева. Подробно — `docs/d3d12.md`.
- Горячие клавиши (`Engine::bindingKeys`): Esc — выход с вопросом (окно «Exit DMEngine?»), Q — каркас, P — скриншот, G — вид игры без редактора, I — полёт,
  F — полёт ↔ ходьба (`docs/physics.md`), Ctrl+S — «Save level», Ctrl+Z / Ctrl+Y — отмена и повтор, 5 / 6 / 7 — гизмо,
  End — экземпляр на землю, `` ` `` — консоль; камера смотрит мышью при зажатой правой кнопке.
- **Редактор** (`src/Engine/Graphics/GUI/`, `docs/gui.md`), как редактор UE: меню File / Edit / View / Window, панели
  Outliner (окна свойств по разделам), Details, Output (консоль), Stats, Camera; вьюпорт — выбор кликом, гизмо, отладочные
  виды. «Save level» (Ctrl+S, `Scene::saveLevel`) пишет в базу только изменённые колонки окон, сохраняемых с уровнем.

## Архитектура

Карта и правила. Полный обзор связей — `docs/architecture.md`, подробности подсистем — `docs/<подсистема>.md`
(оглавление — `docs/README.md`). **Перед правкой подсистемы прочитать её `docs/*.md`** и спросить граф
(`graphify query`); меняя подсистему, обновить её раздел в `docs/`.

**Цикл.** `main.cpp` → `DMSystem` (окно, `Config`, база, ввод) → `GS::Engine::initialize` / `tick`. `Engine` владеет
окном, камерой, GUI и горячими клавишами; содержимое уровня — `Scene` (`Scene/Scene.h`: грузит уровень из базы, владеет
светом, миром симуляции и объектами сцены, зовёт их `update`), отрисовка — `Renderer` (`Renderer.h`). Кадр
(`Renderer::render`, `docs/passes.md`): константы кадра → `compute()` объектов → сбор мешей с видов и раскладка по
проходам → тени (каскады, дальний каскад) → объёмный туман → depth prepass → GTAO → `opaque` → `opaqueDepthRead` → `sky` →
экранные отражения → `transparent` → `PostProcess` (экспозиция, bloom, лучи, цветокоррекция и тонмаппинг) → GUI.

**Объект сцены** — `SceneObject` (`Scene/SceneObject.h`): `update` (логика и состояние), `frameConstants` (вклад в
константы кадра), `compute` (`FrameContext`), `collectMeshes( view, collector )` — секции мешей `MeshBatch` или свой
вызов `CustomBatch` с маской проходов, `renderCustom`, `properties`. Объекты не рисуют себя и не знают своих проходов
(как mesh draw commands в UE): раскладку, сортировку и инстансинг делает `Renderer`. Вид — `RenderView` (главный,
каскады теней, дальний каскад, съёмка сцены — `kind`); выбор, зависящий от вида, хранится по `view.index`. Свои проходы
рендерера (тени, туман, GTAO, SSR, постобработка) — `RenderFeature`. Новый объект — член `Scene` и строка в
`Scene::initialize`: его окно GUI подхватит сам.

**Правила, которые легко нарушить:**
- `ID3D12*` и `D3D12_*` — только в `src/Engine/Graphics/D3D/` (фасад `DMD3D::instance()`; ресурсы GPU у объектов —
  непрозрачные `Buffer`, `Texture`, виды из `D3D/GpuResources.h`); `DXGI_FORMAT` и `D3D_PRIMITIVE_TOPOLOGY` — везде.
- Цели ставит только `DMD3D::beginPass( PassDesc )` — объявление прохода с чтениями и записями (барьеры — по нему);
  порядок в коде — состояние → `setPass` → рисование; другое состояние на проход — `ScopedRenderState`.
- Логика, меняющая сцену, — в `SceneObject::update`; в отрисовке — только команды GPU. Новый проход или вид — в
  `Renderer`, а не в объектах.
- Освещение — только `Shaders/lighting.sh` (`Surface` → `evaluateLighting`); новая составляющая — туда, а не в материалы.
  Единицы физические (люксы, канделы, кд/м²): буфер сцены хранит яркость × экспозицию прошлого кадра — шейдер с
  освещением умножает на `preExposure()`.
- Глубина обратная (Reversed-Z): 1 у ближней плоскости, очистка в 0, «ближе» — `GREATER`; так же карты теней.
- Новый параметр уровня — поле в `Scene/Level/LevelSettings.h` и строка описания `SettingsSchema` в `LevelSettings.cpp`
  (окно, база, «сменилось ли», ключи кэшей — по ней), колонка в базе; всё, что подбирается на глаз, — поле окна, а не
  константа шейдера. Заголовки `ObjectLibrary` знают только `Scene/Level/`; загрузчик — только `Scene.cpp` и `Engine.cpp`.
- Новые ассеты — строки базы (`base.db3`), а не код; модели — glTF через `Tools/import_gltf.py` (`docs/models.md`).
- Общие слоты — `Shaders/slots.h`, константы — `Shaders/<подсистема>_constants.h`; пайплайн, собранный в кадре, — в
  список прогрева.
- Логика без устройства — в `DMCore` с тестами в `tests/`; логику из класса с устройством, которую надо проверить, —
  чистой функцией или структурой (`docs/tests.md`).
- Синглтоны — в одном стиле (`Class::instance()`, `Class::destroy()`): `DMD3D`, хранилища `Assets::textures()` /
  `meshes()` / `models()` / `materials()` / `materialInstances()` (`Graphics/Assets.h`; `destroy()` — до устройства),
  `DBConnector::instance()`, `Input::instance()`. Новым подсистемам зависимости — через конструктор и контекст вызова.

**Подсистемы** (класс — что — документ):

| Подсистема | Классы | Документ |
|---|---|---|
| Слой D3D12, проходы | `DMD3D`, `PassDesc`, `ShaderProgram`, `FullscreenShader` | `d3d12.md`, `passes.md` |
| Материалы, модели | `Material`, `PBRMaterial` (`PBR`, `PBRTree`), `ImpostorMaterial`, `ModelInstances`, `VertexPool` | `materials.md`, `models.md` |
| Свет и тени | `DMLightDriver`, `SunPosition`, `ShadowCascades`, `TerrainSunShadow` | `lighting.md`, `shadows.md` |
| Небо, облака, туман | `SkyAtmosphere`, `SkyLight`, `HDRIBackdrop`, `VolumetricCloud`, `VolumetricFog` | `sky.md`, `clouds.md`, `fog.md` |
| Постобработка, GTAO, SSR | `PostProcess`, `AmbientOcclusion`, `ScreenSpaceReflections`, `ReflectionCaptures` | `postprocess.md`, `ambient_occlusion.md`, `screen_reflections.md`, `reflection_captures.md` |
| Рельеф | `TerrainPipeline` (данные: эрозия, правки, русла, маски, поле ветра), `CDLODTerrain`, `FarTerrain` | `terrain.md`, `far_terrain.md` |
| Вода | `WaterSimulation` (`StaticWater`, `WaterSurface`, `WaterFoam`), `TerrainHydrology` | `water.md` |
| Расстановка и лес | `Scatterer`, `ScatterPass`, импостеры, полог (`TerrainCanopy`) | `scatter.md` |
| Ветер | `Wind`, `TerrainWind`, `FoliageInteraction` | `wind.md` |
| Частицы | `ParticleSystem` | `particles.md` |
| Звук | `AudioDevice`, `AudioMixer`, `AmbientSound`, `Soundscape` | `audio.md` |
| Мир, физика, анимация | `World`, `WorldClock`, `PhysicsWorld`, `WalkMode`, анимация | `simulation.md`, `physics.md`, `animation.md` |
| Редактор, удалённое управление | `GUI`, `Viewport`, `EditHistory`, `ConsoleCommands` | `gui.md`, `remote.md` |
| Съёмка сцены, тесты | `Renderer::requestCapture`, `DMEngineTests` | `scene_capture.md`, `tests.md` |

Данные сцены — SQLite `base.db3`: `LibraryLoader` грузит уровень (`Levels` → `LevelDescription`), текстуры, материалы,
модели с LOD и секциями, экземпляры материалов; заглушки отсутствующих ресурсов — слот 0 хранилищ (шахматка, куб).
Система свойств (`src/Common/Properties`) — окна GUI строятся по `PropertyContainer` объекта; правку видно по номеру
`Property::revision`. Подробно — `docs/architecture.md`.

## Соглашения

- Include-пути: `src`, `src/3rdParty`, `src/Common`, `src/Engine/Graphics`, `src/Engine/Graphics/Common`,
  `src/Engine/Graphics/Scene` и корень проекта. Поэтому встречается `#include "Utils\DMTimer.h"`
  (из `src/Common`) или `"Materials\Material.h"` (из `Scene`). В include используются обратные слеши.
- Именование: большая часть графики — в пространстве имён `GS`, его достаточно, поэтому новые классы — без префикса
  (`Scene`, `Renderer`, `Material`, `ShaderProgram`); методы и свободные функции — camelCase (`initialize`, `setPass`),
  члены — `m_` + camelCase, типы и перечисления — с заглавной (`BlendMode::opaque`). Старые имена — префикс `DM`
  (`DMModel`, `DMMesh`, `DMTexture`, `DMCamera`, `DMLight`, `DMD3D`, `DMSystem`) и методы с заглавной (`Initialize`,
  `Run`), — переименовываются, когда класс правится, а не отдельным коммитом на всё (так `DMGraphics` стал `Engine`); имя, занятое типом
  слоя GPU (`Texture`, `Buffer` в `D3D/GpuResources.h`), не берётся (у текстуры хранилища — например, `TextureAsset`).
- `DirectX` (DirectXMath, DirectXTex, DirectXCollision): в заголовках — с квалификацией (`DirectX::XMFLOAT3`), в `.cpp` —
  `using namespace DirectX;` после include; `using namespace` в заголовках не пишется.
- Все исходники и шейдеры в **UTF-8 без BOM**, концы строк LF (закреплено в `.editorconfig`).
  Компилятор запускается с `/utf-8`, поэтому строковые литералы тоже в UTF-8 — так они попадают в `log.txt`
  и ImGui. Исключение: batch-файлы (`*.cmd`) с CRLF, иначе cmd ошибается на метках (`.editorconfig`, `.gitattributes`).
  Сообщения, которые печатают скрипты `Tools/`, — ASCII: их запускают и Windows PowerShell 5.1, и cmd в кодировке 866.
- `NOMINMAX` задан глобально: `std::min` / `std::max` пишутся без скобок.
- Логика без устройства — в ядре `DMCore` (план шага L1 этапа 14, одобрен Dimok 2026-10-17): слой симуляции, мировые часы, навигация,
  столкновения — файлом в списке `DMCore`, с тестами в `tests/`; рендер о ней не знает, объект сцены берёт у неё
  состояние. Логику, которую надо проверить, а она в классе с устройством, — выносить чистой функцией или структурой
  (`Graphics/DrawSortKey.h`, `Scene/Model/ModelReseat.h`, `Scene/Wind/WindParameters.h`; `docs/tests.md`).
- Сторонний код: `imGUI/`, `src/3rdParty/` и зависимости из `FetchContent`. Его не правим.

## graphify

This project has a knowledge graph at graphify-out/ with god nodes, community structure, and cross-file relationships.

Rules:
- For codebase questions, first run `graphify query "<question>"` when graphify-out/graph.json exists. Use `graphify path "<A>" "<B>"` for relationships and `graphify explain "<concept>"` for focused concepts. These return a scoped subgraph, usually much smaller than GRAPH_REPORT.md or raw grep output.
- If graphify-out/wiki/index.md exists, use it for broad navigation instead of raw source browsing.
- Read graphify-out/GRAPH_REPORT.md only for broad architecture review or when query/path/explain do not surface enough context.
- After modifying code, run `graphify update .` to keep the graph current (AST-only, no API cost).
