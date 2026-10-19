# Сборка, запуск, проверка

Подробности сборки, запуска движка для проверки, удалённого управления, debug-слоя, шейдеров, ресурсов и их рецептов,
горячих клавиш и редактора. Короткая версия с главным — раздел «Сборка и запуск» в `CLAUDE.md` (до 2026-10-19 этот
текст был там целиком); процедуры — скилы `.claude/skills/` (`render-check`, `finish-item`, `asset-import`). Команды
удалённого управления — [remote.md](remote.md), проверка рендера — [d3d12.md](d3d12.md) §4, тесты — [tests.md](tests.md),
редактор — [gui.md](gui.md), модели — [models.md](models.md), ресурсы на NAS — `Tools/nas_sync.py`.

- Проект только на CMake (`CMakeLists.txt`), только MSVC x64. CLion (toolchain Visual Studio, amd64, Ninja) и
  Visual Studio открывают его напрямую. DirectXTex (тег `may2026`), SQLiteCpp (тег `3.3.3`, со встроенным sqlite3)
  и tinyexr (тег `v1.0.13`, чтение `.exr`; свой CMake не берётся — библиотека из `tinyexr.cc` и miniz собирается
  в `CMakeLists.txt`), DirectX 12 Agility SDK 1.619 (пакет NuGet как zip: рантайм `D3D12Core.dll` и debug-слой копируются
  в `D3D12\` рядом с exe, заголовки — раньше Windows SDK), DXC v1.9 (`dxcompiler.dll` и `dxil.dll` рядом с exe),
  WinPixEventRuntime, Dear ImGui 1.92.9b ветки docking (ядро и бэкенды Win32 / DX12 собираются в движке; архив — из
  `DownloadResources\imgui-1.92.9b-docking.zip`, если он там есть, иначе с GitHub), ImGuizmo (ветка master, гизмо
  вьюпорта; `DownloadResources\ImGuizmo-master.zip`, иначе с GitHub), D3D12 Memory Allocator 3.2, EnTT (тег `v3.16.0` —
  последний на C++17, только заголовки: мир симуляции), Jolt Physics (тег `v5.6.0`, его CMake в `Build\`: рантайм `/MD`,
  без LTCG и устройств GPU — столкновения) и doctest (тег `v2.4.11`, только заголовок — для тестов)
  подтягиваются через `FetchContent` при первом configure: нужны сеть и git.
  Остальное берётся из Windows SDK.
- Цели CMake: `DMCore` — статическая библиотека, ядро без устройства (свойства, лог, данные уровня, время суток, рельеф
  и вода на CPU, ветер, микшер и синтез звука, камера, ходьба, ввод, мир симуляции на EnTT, мировые часы, столкновения
  на Jolt и скелетная анимация; как модуль Core в UE); `DMEngine` — exe движка поверх
  неё; `DMEngineTests` — тесты логики на CPU (doctest) против ядра; общие include-пути, defines и флаги трёх целей —
  `DMEngineSettings`. Файлы ядра лежат на своих местах в `src/` — ядро задаётся списком `DMCore` в `CMakeLists.txt`.
- **Из терминала собирать только скриптом**, а не вызывать cmake вручную:
  ```
  Tools\build.cmd debug        (или release; из Git Bash: cmd //c "Tools\build.cmd debug")
  ```
  Скрипт сам находит Visual Studio через vswhere и вызывает `vcvars64`, при первом запуске конфигурирует
  `cmake-build-cli-<debug|release>` и собирает; код выхода — результат сборки, сообщения компилятора в UTF-8.
  Он же сбрасывает `NoDefaultCurrentDirectoryInExePath`, без чего у DirectXTex молча падает `CompileShaders.cmd`.
  Папку сборки скрипт конфигурирует в UTF-8 и ставит метку `configured-utf8`: ninja узнаёт строки `/showIncludes`
  по префиксу, который CMake записывает в кодировке консоли, и при несовпадении теряет зависимости от заголовков.
  Папку без метки скрипт один раз конфигурирует заново и собирает с нуля.
  Папки `cmake-build-debug*` принадлежат профилям CLion: в них из терминала не собирать, иначе сборка
  сталкивается с перезагрузкой CMake в CLion («Permission denied»).
- **Проверка изменений рендера** — по `docs/d3d12.md` §4 «Как проверять»: снимки пяти контрольных камер совпадают
  с эталоном ±1, «GPU average» не хуже, Debug-сборка молчит в `log.txt` (debug-слой D3D12; GPU-based validation —
  `-GpuValidation` у `run.ps1`, `--gpu-validation` у `engine.py` — только для правок слоя D3D12: ресурсы, барьеры,
  дескрипторы, проходы). Одной командой — `python Tools/check_frames.py` (эталон до правки — `check_frames.py
  reference`; `--config Debug` — с проверкой debug-слоя; в Release — с закреплённой частотой GPU, `-stableclocks`
  (`SetStablePowerState`, нужен режим разработчика Windows: на ноутбуке без неё время скачет до ×2,4), и под каждой камерой —
  состояние GPU за замер: частота, мощность, температура, ограничения, чужие процессы — `Tools/gpu_monitor.py`). Эталон D3D11 —
  коммит `0aadc42` в истории git.
- **Запуск для проверки** — `.\Tools\run.ps1 [-Config Release] [-Seconds 8] [-Screenshot кадр.png] [-Keys 2,4]
  [-Camera x,y,z,pitch,yaw] [-Level имя] [-Resolution 1920x1080|desktop] [-NoGui] [-NoMouse] [-NoWind] [-NoParticles]
  [-FreezeTime] [-NoColorGrading] [-Preset имя] [-GpuValidation] [-StableClocks] [-Sound]` (если PowerShell запрещает скрипты: `powershell -ExecutionPolicy Bypass -File Tools\run.ps1 ...`).
  Запускает exe из корня проекта с удалённым управлением (`-remote`), ждёт конца инициализации и ещё `-Seconds`, по
  желанию нажимает клавиши (команда `key`, скан-коды `DIK_*`, как в DirectInput: 2 — «1», 4 — «3», 5 — «4») и снимает
  задний буфер (команда `screenshot`, с окнами ImGui, если нет `-NoGui`), закрывает движок штатно (`quit`, лог дописан;
  принудительно — только если не вышел за 30 с) и печатает из `log.txt` ошибки, число заглушек, время инициализации и строку «GPU average»
  (среднее время GPU кадра и проходов за 3 с после прогрева — для сравнения производительности до и после правок;
  в конце — сколько мешей нарисовано и сколькими вызовами) и «CPU average» (то же для CPU: кадр без ожидания swap chain,
  update и compute объектов, сбор с видов, раскладка, запись команд — `stat cpu`, `docs/remote.md`). Достоверен итог кадра и проходов: метки времени
  не ждут окончания работы, и часть работы объекта (пиксели террейна) попадает в время следующего.
  Обычно камера поворачивается мышью; `-NoMouse` (`-nomouse` у exe) отключает это и скрывает указатель, со `-Screenshot`
  он включается сам — снимки с одной точки совпадают до пикселя, если не движется трава: ветер уровня качает её, для
  сравнения кадров — `-NoWind` (`-nowind` у exe, `--nowind` у `engine.py start`, в сессии `set "Wind/Enabled" false`),
  `-NoParticles` (`-noparticles`, `--noparticles`: частицы движутся) и `-FreezeTime` (`-freezetime`, `--freezetime`: время
  игры стоит — рябь воды на месте, снимки с водой совпадают между запусками); `-NoColorGrading` (`-nocolorgrading`,
  `--nocolorgrading`, в сессии `set "Post process/Color grading/Enabled" false`) — без цветокоррекции уровня, только кривая
  тонмаппинга; звук инструменты по умолчанию глушат (`-mute`: вывод молчит, микшер и `audio record` работают; `-Sound` /
  `--sound` — со звуком, `docs/audio.md`); `-Preset имя` (`-preset`, `--preset`, в сессии `preset имя`) — пресет окружения уровня (`LevelPresets`:
  время суток, небо, постобработка, ветер, туман, облака; `docs/gui.md`, «Пресеты окружения»): Dimok подбирает окружение в
  своём (по умолчанию — строки уровня, `[Level] Preset=` в `settings.ini`), проверки — с заблокированным `Reference`
  (`check_frames.py` запускает с `--preset Reference`; «Save level» его не пишет). В редакторе окна пресета подкрашены
  бирюзовым: их правки эталон не меняют. Ближняя и дальняя плоскости камеры — `ScreenNear` / `ScreenDepth` в секции `[General]` `settings.ini` (0,1 м и 100 км — дальние горы вокруг карты; Reversed-Z;
от дальней зависит только сфера неба модели уровня, у объёма воздушной перспективы своя глубина 64 км, у тумана над небом —
`sky_distance`). Стартовая камера — секция `[Camera]` в `settings.ini` (`Position=x,y,z`,
  `Rotation=pitch,yaw` в градусах, pitch > 0 — взгляд вниз), уровень — секция `[Level]` (`Name`); параметры `-Camera`
  и `-Level` скрипта (`-camera`, `-level` у exe) их переопределяют, так что снимок с нужной точки не требует правки кода.
  Списки (`-Keys`, `-Camera`) скрипт разбирает сам: через `powershell -File` они приходят одной строкой.
  `-NoGui` (`-nogui` у exe) снимает кадр без окон ImGui — для сравнения картинки лучше с ним.
- **Серия проверок за один запуск** — удалённое управление (`docs/remote.md`, как консоль и Remote Control в UE):
  `python Tools/engine.py start [--config Debug] [--nogui]` (по умолчанию — Release), затем команды — `camera x,y,z,pitch,yaw`,
  `screenshot файл.png [кадров]`, `capture x,y,z,pitch,yaw файл.png` (кадр с другой камеры), `audio record файл.wav секунд`
  (смесь звука; разбор — `Tools/audio_check.py`), `stat gpu 3`, `stat cpu 3`, `list` / `get` / `set "окно/свойство" значение`, `press "окно/кнопка"`, `pix [файл.wpix]` (захват PIX, движок с
  `--pix`; разбор — `Tools/pix_events.py`, `docs/d3d12.md` §4), `key G`, `timestep 0.0166`
  (фиксированный шаг времени для серий кадров), — по одной или
  сценарием (`python Tools/engine.py run файл.txt`), и `python Tools/engine.py stop` (штатный выход, ошибки из лога).
  Ответ на команду — не дольше 60 с (`--timeout`; `run.ps1` — тоже 60 с): не ответил — главный цикл стоит, скрипт
  печатает строки сторожа кадра и выходит с кодом 2, `start` (проверяет первый кадр командой `wait 1`) и `stop` такой
  движок закрывают. Сторож кадра — `FrameWatchdog` (`Utils/FrameWatchdog.h`, как ThreadHeartBeat в UE): шаг кадра
  дольше 10 с — строка «Frame watchdog» в `log.txt`: шаг (`stage` в `Engine::tick`, ожидания GPU и swap chain,
  создание PSO), последняя метка (`DMD3D::beginEvent`: проходы, объекты в compute), ход GPU по fence и в первый раз —
  стек главного потока с символами (dbghelp, PDB рядом с exe).
  Движок с `-remote` слушает канал `\\.\pipe\DMEngine`, команды выполняет главный поток в начале кадра. Снимок —
  задний буфер. Окно и задний буфер — `Resolution` в секции `[General]` `settings.ini` или параметр `-resolution`:
  `desktop` (по умолчанию) — окно без рамки на весь основной монитор в его разрешении (на этой машине 3840×2160);
  `1920x1080` — обычное окно с заголовком и такой клиентской областью в физических пикселях (процесс объявляет поддержку
  DPI, `DMSystem::InitializeWindows`): на экране с масштабом 150 % окно не растягивается и не закрывает экран. Задний
  буфер — клиентская область окна. `engine.py start` и `run.ps1` по умолчанию передают `-resolution 1920x1080` (снимки
  и замеры сравнимы с прежними цифрами «Release, 1920×1080»), `--resolution desktop` / `-Resolution desktop` — весь
  экран. `camera` — смена плана: экспозиция сразу, снимок и замер ждут 10 кадров. Для нескольких кадров или
  подбора параметров это быстрее отдельных запусков `run.ps1` (сессия из трёх снимков и двух замеров — ~12 с).
  Сравнение снимков по пикселям — `python Tools/compare_frames.py a.png b.png [--tolerance 1] [--diff карта.png]`
  (или две папки с одноимёнными снимками); ±1 в сотнях пикселей между запусками — округление экспозиции, не ошибка.
- Мёртвый код (несобираемые файлы, невызываемые функции, старый закомментированный код, неиспользуемые
  шейдеры и данные) удалён. **Часть невызываемого кода сохранена намеренно**: его вызовы закомментированы
  в последних коммитах, это временно выключенные фичи. Например, закомментированное тело
  `LibraryLoader::save`. Не удаляйте такой код без согласования: сначала проверьте `git blame`, когда его
  выключили.
- Debug-сборка создаёт устройство с debug-слоем D3D12 (слой — из redist Agility SDK рядом с exe, компонент Windows
  «Средства графики» не нужен); GPU-based validation — по `GpuValidation=true` в `settings.ini` или параметру
  `-gpuvalidation` (для правок слоя D3D12; только учёт состояний ресурсов — правка шейдеров слоем на `scatter.cs` не
  кончается, `TODO.md`; первый кадр ~13 с). Ошибки
  и предупреждения слоя приходят callback'ом `ID3D12InfoQueue1` и пишутся
  в `log.txt` (одинаковые — первые три раза): без отладчика их больше нигде не видно; при потере устройства в лог
  уходят последние команды (DRED). Release-сборка создаёт устройство без слоя. Крах в Release: смещение из журнала
  Windows (`Get-WinEvent`, Id 1000) ищется в `DMEngine.map` рядом с exe (линкер с `/MAP`).
- **Тесты логики на CPU** — `Tools\test.cmd [debug|release] [параметры doctest]` (из Git Bash:
  `cmd //c "Tools\test.cmd release"`; `-tc=WalkMode*` — тесты по имени): собирает конфигурацию скриптом сборки и запускает
  `DMEngineTests.exe` в каталоге сборки (его `log.txt`, лог движка в корне цел), код выхода — результат; весь набор —
  доли секунды. Тесты — `tests/test_<подсистема>.cpp` (CMake подхватывает новые файлы сам); тестовый exe берёт ядро
  целиком (`/WHOLEARCHIVE:DMCore`), поэтому ссылка файла ядра на устройство, ImGui, DirectXTex или базу — ошибка
  линковки. Подробно — `docs/tests.md`. Линтера нет. Проверка рендера — сборка и запуск приложения; кадры и время GPU
  контрольных камер против эталона — `Tools/check_frames.py`.
- Запускать из корня проекта: все пути относительные к рабочей папке (`settings.ini`, `base.db3`,
  `Shaders\`, `Textures\`, `Meshes\`). Для CLion это задаёт общая
  конфигурация запуска `.run/DMEngine.run.xml`. Каталоги `Textures\`, `Meshes\` и `Sounds\` (записи звука — `Tools/pack_sound.py`
  из `DownloadResources\Sounds\` через ffmpeg, `docs/audio.md`) в git не хранятся;
  без них движок запускается на заглушках (см. «Заглушки ресурсов»), а в `log.txt` перечислено, что не загрузилось.
  Их копия вместе с `DownloadResources\` (скачанные архивы) и итогом эрозии — на NAS (решение 2026-10-19):
  `python Tools/nas_sync.py [status|push|pull] [--dry-run]` — сравнить, залить, забрать (без удалений; запускать из
  PowerShell или cmd — из песочницы Git Bash SSH в локальную сеть не ходит). На новой машине — `pull`; новые архивы,
  текстуры, меши и записи — `push`.
  Тестовые данные террейна: исходную карту высот `Textures\terrain\heightmap.dds` (1024×1024: горная долина без
  эрозии, секунды, numpy; карта может быть и из World Machine, Gaea) — `python Tools/gen_heightmap.py`; эрозию (капли,
  осыпание) делает движок при загрузке на GPU и пишет рельеф и карты эрозии в `Textures\terrain\eroded\` (`height`,
  `flow`, `wear`, `deposition`, `talus`; это и кэш), а экземпляры `LevelModels` пересаживает на новую землю — закрепить
  «Save level»; splat-карту и маски расстановки и леса по типам (`mask_grass`, `mask_forest_spruce` / `pine` / `birch`,
  `mask_shrubs_riparian` / `slope` …) движок тоже считает сам — правилами по итоговому рельефу и руслам (`TerrainMasks`,
  `docs/terrain.md`, «Слои и маски по рельефу»; копия — `Textures\terrain\generated\`); дальние горы вокруг карты (котловина
  с просветом на север) — `python Tools/gen_far_terrain.py` (по рельефу карты после эрозии: `Textures\terrain\far_heightmap.dds`
  и строка `FarTerrain` уровня, `docs/far_terrain.md`); процедурные тестовые слои
  `Textures\terrain\layers\*.dds` — `python Tools/gen_terrain_textures.py` (нужен numpy). Слои из фото-текстур (Poly Haven,
  freepbr; архивы — в `DownloadResources\`, не в git) собирает `Tools/pack_terrain_layer.py` через Blender — команды
  нынешних слоёв в `docs/terrain.md`. Тестовые модели уровня
  `Test` (`TestRock`, `TestPanel`) — сцена Blender без окна и импорт:
  `blender -b --factory-startup --python Tools/blender_test_model.py -- Meshes/source/test_models.glb`, затем
  `python Tools/import_gltf.py Meshes/source/test_models.glb --level Test --position 470,11.73,238` (повторно — без
  `--level`: после новой долины экземпляры пересажены на землю). Параметры движка, которых нет в glTF (ветер,
  пропускание, смена LOD), — в сценариях и опциях `--material-param` (`extras` glTF), см. `docs/models.md`. Пучок травы
  набора `Meadow` (`GrassClump`) — `blender -b --factory-startup --python Tools/blender_grass.py -- Meshes/source/grass.glb`,
  затем `python Tools/import_gltf.py Meshes/source/grass.glb --scatter --lod-ranges 6,14 --shadow-min-lod 2`; ромашка (`Camomile`, альфа-лепестки) — так же
  со скриптом `Tools/blender_camomile.py` и файлом `Meshes/source/camomile.glb`. Модели Poly Haven в расстановке
  (одуванчики `dandelion_01`, камни `rock_moss_set_01`; архивы `.blend` — в `DownloadResources\`,
  скачать — `python Tools/polyhaven_download.py <ассет> --resolution 2k`) — `Tools/export_polyhaven.py` через Blender,
  затем `import_gltf.py --scatter`, команды — в `docs/models.md`. Растения Megascans с Fab (трава
  `Grass` sltiN — `dry_grass`; архив FBX или папка из Unreal Launcher — в `DownloadResources\`) —
  `Tools/export_megascans.py` через Blender, затем
  `import_gltf.py --scatter`, команды — в `docs/models.md`. Ели уровня
`Test` (`Fir_A`, `Fir_B`, `Fir_C`: своя геометрия, кора и карточки хвои из `fir_tree_01` Poly Haven) —
`blender -b --factory-startup --python Tools/blender_fir.py -- --archive DownloadResources/fir_tree_01_2k.blend.zip --out Meshes/source/fir.glb`,
затем `python Tools/import_gltf.py Meshes/source/fir.glb --level Test --position 480,0,345 --snap-to-terrain --lod-ranges 35,90`
(`--snap-to-terrain` ставит экземпляры на землю уровня). Blender 5.0 стоит в
  `C:\Program Files\Blender Foundation\Blender 5.0\blender.exe` (не в PATH). Панорама уровня `TestHDRI` —
  Kloofendal 48d Partly Cloudy с Poly Haven (CC0, https://polyhaven.com/a/kloofendal_48d_partly_cloudy), 2k `.hdr`
  (и `.exr` для проверки) в `Textures\hdri\kloofendal_48d_partly_cloudy_2k.*`; без неё `TestHDRI` освещает атмосфера.
- Лог каждого запуска перезаписывается в `log.txt` (макрос `LOG(x)` из `src/Logger/Logger.h`); в git его нет
  (`.gitignore`), как и снимков клавишей P (`screenshot<N>.jpg` в корне).
- Шейдеры (`Shaders/*.vs|.ps|.gs|.cs`, include — `*.sh`) компилируются во время выполнения компилятором DXC в DXIL
  Shader Model 6.6, HLSL 2021 (`D3D/ShaderCompiler.h`, `dxcompiler.dll` + `dxil.dll` рядом с exe): для правки шейдера
  пересборка не нужна. Кэш DXIL — `cache/shaders/<хэш>.dxil` (ключ — файл, точка входа, defines, флаги и отпечаток
  всего каталога `Shaders\`: любая правка пересобирает всё); в Debug — отладочная информация без оптимизации
  (исходник виден в PIX), в Release — `-O3`. Ресурсы привязываются bindless: шейдер объявляет их макросами
  `DM_SRV( тип, имя, слот )` / `DM_UAV( … )` из `Shaders/bindless.sh` (static-переменные из `ResourceDescriptorHeap`
  по индексам из root-констант b8 — таблицы привязок вызова, которую `DMD3D::setSRV` / `setUAV` заполняют по
  слотам; ресурсы сцены t100… — из константного буфера b10, копии их индексов в кольце констант, поэтому их число
  root signature не ограничивает), константные буферы — `register( SLOT_CB_… )` (root CBV b0…b7), сэмплеры
  статические (s0…s8). Номера
  слотов, общие для C++ и HLSL, — `Shaders/slots.h` (`DM_SRV( …, SLOT_LIGHTS )` в шейдере, `SLOT_LIGHTS` в C++);
  новый общий слот заводите там же. Буферы констант — тоже одним описанием для C++ и HLSL: `Shaders/<подсистема>_constants.h`
  (макросы `DM_CONSTANTS` / `DM_FIELD` из `Shaders/shader_constants.h`: в HLSL — cbuffer с полями `g_<имя>`, в C++ — структура
  `GS::<буфер>`); раскладку в Debug сверяет `ShaderCompiler` по отражению шейдера (`docs/d3d12.md` §3.6). Пайплайны (PSO) собираются при загрузке уровня по спискам прогрева
  (`Renderer::warmPipelines` — цветные фазы материалов с целями сцены, фазы «только глубина» без цели цвета;
  `FullscreenShader::load` и террейн — со своими форматами) и кэшируются на диске `ID3D12PipelineLibrary`
  (`cache/pipelines.bin`, пишется при штатном выходе); собранный в кадре пайплайн — «ленивый»: строка в лог и
  счётчик в «Statistic» — дополните список прогрева.
- Горячие клавиши (`Engine::bindingKeys`): Esc — выход, Q — wireframe, P — скриншот (кадр с окнами ImGui в
  `screenshot<N>.jpg`; ставится в ту же очередь, что команда `screenshot`: копировать задний буфер можно только в кадре),
  1 — видимость террейна, 3 / 4 — расчёт / отрисовка всех наборов расстановки (трава, камешки; по умолчанию включены),
  I — режим полёта (камера смотрит мышью всегда), G — вид игры без редактора, Ctrl+S — сохранить уровень (правки окон),
  левая кнопка по сцене — выбрать экземпляр модели, 5 / 6 / 7 — гизмо: перемещение / поворот / масштаб, End — выбранный
  экземпляр на землю («Snap to terrain», `ModelInstances::snapToTerrain`), F — полёт ↔
  ходьба по рельефу (`Scene/Camera/WalkMode.h`: персонаж мира столкновений — капсула Jolt, земля — те же треугольники,
  что `TerrainHeightSource::surfaceHeight`; модели уровня и сущности преграждают путь, ступеньки, уклон, вода глубже
  «Max wade depth» не пускает, `docs/physics.md`; включение в воздухе — падение до земли),
  `` ` `` — консоль. Камера смотрит мышью при зажатой правой кнопке над сценой (`Engine::updateMouseLook`: только у
  активного окна, курсор спрятан и закреплён `ClipCursor` на месте, после Alt+Tab свободен); пока
  в окнах редактора набирается текст, горячие клавиши и WASD молчат (`Input::setKeyboardBlocked`).
- **Редактор** (`src/Engine/Graphics/GUI/`, подробно — `docs/gui.md`), как редактор UE: главное меню (File — действия
  `GUI::addAction`, их же вызывает команда `action`; Edit — отмена и повтор правок, Ctrl+Z / Ctrl+Y, `Editor::EditHistory`,
  команды `undo` / `redo`; View — переключатели `addToggle`; Window — панели и «Reset layout»; Help), докинг панелей: Outliner (окна свойств по разделам Environment / Scene / Audio / Rendering, флажок видимости объекта),
  Details (свойства выбранного таблицей: поиск, единицы, подсказки, отметка изменённого после загрузки или сохранения и
  сброс — `Property::markSaved` / `modified` / `resetToSaved`), Output (консоль на `ConsoleCommands` с историей и
  дополнением; лог — `Logger::recent`), Stats, Camera, Texture Library; оверлей FPS и времени кадра. Раскладка по
  умолчанию — кодом (`DockBuilder`), запоминается в `imgui_editor.ini` (не в git). Команда `select "<окно>"` выбирает
  окно в Outliner — для снимков Details сценарием. «Save level» (Ctrl+S, `Scene::saveLevel`) пишет в базу окна,
  сохраняемые с уровнем (`SceneObject::savedWithLevel`; Details пишет «Saved with the level»): окружение
  (`LibraryLoader::saveLevelEnvironment`) и объекты сцены — террейн, расстановку, воду, частицы
  (`LibraryLoader::saveLevelScene`, `ObjectLibrary/LibraryLoaderScene.cpp`); объект отдаёт настройки по своим свойствам,
  о базе не знает; пишутся только колонки, изменённые после загрузки или прошлого сохранения (снимок `LevelSaveData`,
  `RowUpdate` в `ObjectLibrary/SettingsDatabase.h`): NULL в базе остаётся NULL, производное (направление светил по времени
  суток) не пишется. Имя свойства — без «/» (разделитель пути `set`), единицы — `Property::setUnit` (окно пишет их в скобках
после имени, «Far border (m)»). Экземпляры моделей —
  окно «Model instances» (`ModelInstances::instanceProperties`: положение, поворот углами, масштаб → `LevelModels`;
  подокно «Add» — экземпляр модели уровня перед камерой, «Delete» — убрать; «Save level» вставляет и удаляет строки).
  Числа — поля как в Blender (`PropertyWidgets::numberField`, один шаблон контролов `controlCell` на все окна):
  перетаскивание вбок, клик — ввод, стрелки по краям — шаг, Shift — точнее, Ctrl — шагами; у свойства с границами —
  заливка доли. Кнопки окна — `PropertyContainer::addButton` (как CallInEditor в UE; действие с отменой —
  `EditHistory::record`, команда `press "окно/кнопка"`): так добавляются и убираются источники света. Вьюпорт
  (`GUI/Viewport.h`, вызывается между `GUI::Begin` и `End`, получает `Scene` целиком; значки и гизмо источников света и
  эмиттеров частиц с местом): клик по сцене — луч по формам столкновений, иначе к границам экземпляров ближе попадания
  (`ModelInstances::pick`), рамка, подокно в Details (`GUI::focusProperties`), гизмо ImGuizmo пишет в те же свойства;
  отладочные виды — View → Debug views (и метки звука «Sound sources»: певцы, места воды, области слоёв с «Show area»);
  команда `pick x y` — выбор сценарием.
