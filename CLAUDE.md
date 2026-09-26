# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Проект

DMEngine — самописный 3D-движок на C++17 / Direct3D 11 под Windows (Win32-окно, DirectInput, ImGui).
Сейчас в основном используется как полигон для рендеринга: террейн (CDLOD),
небо, расстановка травы и декора (compute + indirect draw), частицы.

## Текущая работа

План доработок и их статус — в `TODO.md`. В начале сессии прочитайте его. Выполненный пункт отметьте `[x]`
и перенесите в «Сделано», найденные по ходу проблемы добавьте в соответствующий раздел.

Архитектурные уязвимости и недочёты, замеченные при написании кода (скрытое состояние, дублирование логики,
жёстко зашитые значения, то, что мешает добавлять фичи), тоже записывайте в `TODO.md` — с местом в коде и
предлагаемым решением. Раздел «Дальше, по приоритету» разбит на этапы по зависимостям: ставьте пункт в тот этап,
после которого его можно сделать без переписывания, и поднимайте выше, если от него зависят другие; если пункт меняет
подход к уже запланированным, пересмотрите и их порядок.

Документация по функционалу движка для людей — `docs/` (оглавление `docs/README.md`, пишется по-русски). Меняя
подсистему, у которой есть раздел, обновите и его; новую подсистему описывайте там же по образцу `docs/scatter.md`.

## Сборка и запуск

- Проект только на CMake (`CMakeLists.txt`), только MSVC x64. CLion (toolchain Visual Studio, amd64, Ninja) и
  Visual Studio открывают его напрямую. DirectXTex (тег `may2026`) и SQLiteCpp (тег `3.3.3`, со встроенным sqlite3)
  подтягиваются через `FetchContent` при первом configure: нужны сеть и git. Остальное берётся из Windows SDK.
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
- **Запуск для проверки** — `.\Tools\run.ps1 [-Config Release] [-Seconds 8] [-Screenshot кадр.png] [-Keys 2,4]
  [-Camera x,y,z,pitch,yaw] [-Level имя] [-NoGui] [-NoMouse]` (если PowerShell запрещает скрипты: `powershell -ExecutionPolicy Bypass -File Tools\run.ps1 ...`).
  Запускает exe из корня проекта, по желанию нажимает клавиши (скан-коды DirectInput: 2 — «1», 4 — «3», 5 — «4») и снимает
  окно, закрывает движок и печатает из `log.txt` ошибки, число заглушек, время инициализации и строку «GPU average»
  (среднее время GPU кадра и проходов за 3 с после прогрева — для сравнения производительности до и после правок;
  в конце — сколько мешей нарисовано и сколькими вызовами). Достоверен итог кадра и проходов: метки времени
  не ждут окончания работы, и часть работы объекта (пиксели террейна) попадает в время следующего.
  Обычно камера поворачивается мышью; `-NoMouse` (`-nomouse` у exe) отключает это и скрывает указатель, со `-Screenshot`
  он включается сам — снимки с одной точки совпадают до пикселя. Стартовая камера — секция `[Camera]` в `settings.ini` (`Position=x,y,z`,
  `Rotation=pitch,yaw` в градусах, pitch > 0 — взгляд вниз), уровень — секция `[Level]` (`Name`); параметры `-Camera`
  и `-Level` скрипта (`-camera`, `-level` у exe) их переопределяют, так что снимок с нужной точки не требует правки кода.
  Списки (`-Keys`, `-Camera`) скрипт разбирает сам: через `powershell -File` они приходят одной строкой.
  `-NoGui` (`-nogui` у exe) снимает кадр без окон ImGui — для сравнения картинки лучше с ним.
- Мёртвый код (несобираемые файлы, невызываемые функции, старый закомментированный код, неиспользуемые
  шейдеры и данные) удалён. **Часть невызываемого кода сохранена намеренно**: его вызовы закомментированы
  в последних коммитах, это временно выключенные фичи. Например, закомментированные `GUI::renderSceneObject`
  и тело `LibraryLoader::save`. Не удаляйте такой код без согласования: сначала проверьте `git blame`, когда его
  выключили.
- Debug-сборка создаёт устройство с debug-слоем D3D11 (нужен компонент Windows «Средства графики» / Graphics Tools;
  без него — устройство без слоя и запись в лог). Ошибки и предупреждения слоя `DMD3D::logDebugMessages` после
  каждого кадра переносит в `log.txt` (одинаковые — первые три раза): без отладчика их больше нигде не видно.
  Release-сборка создаёт устройство без слоя.
- Тестов и линтера нет. Проверка изменений — сборка и запуск приложения.
- Запускать из корня проекта: все пути относительные к рабочей папке (`settings.ini`, `base.db3`,
  `Shaders\`, `Textures\`, `Meshes\`, `Scene\Lights.ini`). Для CLion это задаёт общая
  конфигурация запуска `.run/DMEngine.run.xml`. Каталоги `Textures\` и `Meshes\` в git не хранятся;
  без них движок запускается на заглушках (см. «Заглушки ресурсов»), а в `log.txt` перечислено, что не загрузилось.
  Тестовые данные террейна создают скрипты: карту высот `Textures\terrain\heightmap.dds` (1024×1024) —
  `python Tools/gen_heightmap.py`, затем текстуры слоёв `Textures\terrain\layers\*.dds` и splat-карту
  `Textures\terrain\splatmap.dds` — `python Tools/gen_terrain_textures.py` (нужен numpy). Тестовые модели уровня
  `Test` (`TestRock`, `TestPanel`) — сцена Blender без окна и импорт:
  `blender -b --factory-startup --python Tools/blender_test_model.py -- Meshes/source/test_models.glb`, затем
  `python Tools/import_gltf.py Meshes/source/test_models.glb --level Test --position 470,90.5,238`. Пучок травы
  набора `Meadow` (`GrassClump`) — `blender -b --factory-startup --python Tools/blender_grass.py -- Meshes/source/grass.glb`,
  затем `python Tools/import_gltf.py Meshes/source/grass.glb --scatter`; ромашка (`Camomile`, альфа-лепестки) — так же
  со скриптом `Tools/blender_camomile.py` и файлом `Meshes/source/camomile.glb`. Blender 5.0 стоит в
  `C:\Program Files\Blender Foundation\Blender 5.0\blender.exe` (не в PATH).
- Лог каждого запуска перезаписывается в отслеживаемый `log.txt` (макрос `LOG(x)` из `src/Logger/Logger.h`).
- Шейдеры (`Shaders/*.vs|.ps|.gs|.hlsl`) компилируются во время выполнения
  (`D3DCompileFromFile` / `D3DCompile` в `DMShader`). Для правки шейдера пересборка не нужна. Флаги — `shaderCompileFlags()`
  (`Scene/Shaders/ShaderUtils.h`): в Debug отладочная информация без оптимизации (исходник виден в RenderDoc / PIX),
  в Release — `D3DCOMPILE_OPTIMIZATION_LEVEL3`. Номера слотов, общие для C++ и HLSL (константные буферы кадра,
  объекта, материала; данные инстансов; свет и освещение окружением), — макросы `Shaders/slots.h`
  (`register( SLOT_LIGHTS )` в шейдере, `SLOT_LIGHTS` в C++); новый общий слот заводите там же.
- Горячие клавиши (`DMGraphics::bindingKeys`): Esc — выход, Q — wireframe, P — скриншот,
  1 — видимость террейна, 3 / 4 — расчёт / отрисовка всех наборов расстановки (трава, камешки; по умолчанию включены),
  I — курсор для работы с ImGui, G — показать / скрыть окна ImGui (как Game View в редакторе UE).

## Архитектура

**Цикл.** `main.cpp` → `DMSystem` (окно, `Config` из `settings.ini`, `dbConnect().init()`, `getInput()`)
→ `GS::DMGraphics::Initialize` / `Frame`. `DMGraphics` владеет окном, камерой, GUI и горячими клавишами,
а содержимое уровня и отрисовку отдаёт двум классам:
- `Scene` (`Scene/Scene.h`) загружает ресурсы уровня из БД (`loadResources`), владеет светом и объектами сцены
  и вызывает их `update()`. Состав уровня описывает строка таблицы `Levels` (см. «Данные сцены»); объект,
  которого у уровня нет, остаётся неинициализированным и ничего не делает;
- `Renderer` (`Renderer.h`) отправляет команды GPU: общие данные конвейера (сэмплеры, свет, константы кадра и вида)
  → `compute()` всех объектов → сбор мешей с вида и раскладка по проходам с сортировкой → проходы `opaque` → `sky`
  (фон на дальней плоскости: глубина `LESS_EQUAL` без записи — только там, где сцена ничего не нарисовала) →
  `transparent` (alpha blending, глубина только читается) в HDR-буфер сцены
  (`R16G16B16A16_FLOAT`) → `PostProcess`: экспозиция и тонмаппинг (AgX / ACES) в задний буфер sRGB. Затем
  `DMGraphics` рисует GUI и вызывает `EndScene`. Шейдеры объектов пишут линейный цвет без экспозиции; настройки —
  `[PostProcess]` в `Scene\Lights.ini` и окно GUI «Post process», подробно — `docs/postprocess.md`. Каждый проход
  начинается с чистого состояния: `Renderer::executePass` заново ставит цель сцены с областью вывода
  (`DMD3D::setSceneTarget`) и отвязывает ресурсы материалов (`unbindTransientResources`); полноэкранные проходы
  (постобработка, небо) — `FullscreenShader`: сам ставит топологию, шейдеры и состояния, цель — `DMD3D::setRenderTarget`.
  Время CPU и GPU (`GpuProfiler`, запросы timestamp) каждого объекта и прохода — в окне «Statistic», те же области —
  метки событий в захвате RenderDoc / PIX.

**Виды и списки отрисовки** — как mesh draw commands в UE: объекты не рисуют себя сами и не знают, в каком проходе
их меши. Вид кадра — `RenderView` (`Scene/RenderView.h`, ≈ FSceneView: матрицы, положение, `DMFrustum`, `lodOrigin` —
откуда считаются LOD); сейчас один, главная камера, позже каскады теней. Объект сцены наследует `GS::SceneObject`
(`Scene/SceneObject.h`): `update` / `compute` (кадр — `FrameContext`: главный вид и время) / `collectMeshes( view,
collector )` / `renderCustom( context )` / `properties`, видимость. За каждый вид объект отдаёт в `MeshCollector`
(`Scene/MeshBatch.h`) меши — `MeshBatch` (≈ FMeshBatch: меш в `VertexPool`, материал, параметры, мировая матрица,
режим материала, расстояние) — или свой вызов `CustomBatch` с маской проходов (террейн, расстановка, небо, частицы).
`Renderer` раскладывает их по проходам (`MeshPass`: меши — по режиму материала, `passFor`; свои вызовы — по маске)
с 64-битным ключом сортировки (непрозрачные — объект в порядке сцены, материал, вариант шейдера, растеризатор, меш;
прозрачные — от дальних к ближним между всеми объектами; `MeshBatch::instanceGroup` — «этот меш с этими
параметрами») и рисует меши одной функцией `Renderer::drawMesh` (`setPass( phaseFor )`, `setParams`, растеризатор по
двусторонности и зеркальности — `materialRasterState`, матрица объекта). Одинаковые непрозрачные меши подряд — одним
`DrawIndexedInstanced` (`drawMeshInstanced`: матрицы экземпляров в структурном буфере t16, вариант вершинного
шейдера `INST_MATRIX`, который материал собирает сам — `supportsInstancing`, `phaseFor( params, true )`). Свой вызов получает `RenderContext` (вид, проход, растеризатор кадра, константы, `VertexPool`); перед
ним привязан общий `VertexPool` с топологией TRIANGLELIST, свои буферы объект привязывает сам. Новый проход или вид
(тени, depth prepass) добавляется в `Renderer`, а не в объекты.
Сейчас объекты (в порядке сцены): `SkyAtmosphere` (процедурное небо фоном и освещение окружением от него;
его `compute()` идёт первым и привязывает IBL к слотам PS t101…t103; фон — полноэкранный треугольник на дальней
плоскости в проходе `sky`), `SkySphere` (модель неба уровня, если задана — тогда атмосфера только освещает; сфера
растягивается до 0,9 дальней плоскости вида, `RenderView::farPlane`), `CDLODTerrain`,
`ModelInstances` (экземпляры моделей уровня: LOD по расстоянию от точки LOD вида, отсечение по frustum вида — границы
меша `AbstractMesh::bounds` считаются при загрузке, меши в список отрисовки),
`Scatterer` (по объекту на набор расстановки уровня: трава, камешки), `DMParticleSystem`. Новый объект добавляется членом `Scene` и строкой в `Scene::initialize`;
его свойства GUI подхватит сам.

Логику, которая меняет состояние сцены, пишите в `SceneObject::update()`, а в `render()` оставляйте только команды GPU.

**Глобальные синглтоны** — основной способ связи подсистем:
- `DMD3D::instance()` — устройство/контекст D3D11, привязка SRV (`setSRV(SRVType::ps|vs|cs…, slot, srv)`), скриншоты,
  состояния растеризатора/глубины/блендинга (`setState(RasterState | DepthState | BlendState)`).
  Проход, которому нужно другое состояние, меняет его через RAII-объект `ScopedRenderState`
  (`ScopedRenderState state( DepthState::disabled, RasterState::frontCulling );`): в деструкторе он восстановит
  предыдущие состояния;
- `GS::System::textures() / meshes() / models() / materials()` — хранилища ресурсов
  (`DMResourceStorage<T>`, доступ по id или по имени; путь хранилища — подкаталог: `Textures`, `Meshes`,
  `Models`, `Shaders`);
- `pipeline().shaderConstant()` — общие constant buffers (`Shaders/ConstantBuffers.h`);
- `dbConnect().db()` — `SQLite::Database` на `base.db3`; `getInput()` — DirectInput + `KeyEventNotifier`.

**Данные сцены в SQLite (`base.db3`).** `ObjectLibrary/LibraryLoader` по id загружает из БД текстуры,
материалы, шейдеры материала (`MaterialShaderView`: файл, тип стадии, defines), определения
и значения параметров материала (`MaterialParameterDefView`, `MaterialParamsValueView`), меш и модель с LOD
(`ModelProperties`: lod, range, material, mesh, material instance). Материалы грузятся все, что есть в таблице
`Materials`; класс шейдера выбирает `MaterialStorage::createMaterial` по колонке `class`. Параметры LOD —
экземпляр материала: `ModelProperties.material_instance_id` → `MaterialInstance` (определения параметров — от его
`id_material`, значения — `MaterialParameterInstance`); NULL — параметры `material_id` со значениями по умолчанию
(`MaterialParameterDef.default_value`). Экземпляры 5 и 12 — наследие старой схемы, где эта колонка означала
`Materials.id`; у SkySphere экземпляр 5 не совпадает с материалом. Подробно — `docs/materials.md`. Цветовое
пространство текстуры задаёт `Textures.sRGB` (1 — цвет, 0 — данные), а не метаданные файла. Новые ассеты добавляются
строками в БД, а не кодом; модели из Blender — экспорт glTF и `Tools/import_gltf.py` (файлы мешей и текстур + строки
моделей, LOD, экземпляров материала `PBR`, расстановки: положение, поворот и масштаб объекта Blender — экземпляр
в `LevelModels`, связанные дубликаты — экземпляры одной модели; повторный импорт обновляет), подробно — `docs/models.md`. Свет — `Scene\Lights.ini`: источники `[LightN]` (`Type` Dir / Point / Spot, `Color`,
`Direction` — куда идёт свет, `Position`, `AttenuationRadius`, `InnerConeAngle` / `OuterConeAngle` — имена как в UE
и KHR_lights_punctual; первый направленный — солнце для неба, его яркость подобрана под экспозицию 0 EV), небо `[Sky]`,
постобработка `[PostProcess]`; ini читается через `ResourceMetaFile` (`GetPrivateProfileString`).

Состав уровня (`LibraryLoader::loadLevel` → `LevelDescription`): строка `Levels` ссылается на террейн (`Terrain`,
слои материала — `TerrainLayers`), модель неба (`Models`) и частицы (`Particles`: материал, текстура, плотность);
NULL — этого у уровня нет. Экземпляры моделей уровня — `LevelModels`: строка на экземпляр (`position`, `rotation` —
кватернион `x,y,z,w` как в glTF, `scale`), у модели их может быть сколько угодно. Модель (`DMModel`: LOD, меши,
материалы) — общий ресурс без положения; положение держит экземпляр (`DMTransform` в `ModelInstances`, у неба — в
`SkySphere`), мировая матрица и матрица нормалей (обратная транспонированная) уходят в константный буфер объекта
(`ConstantBuffers::setPerObjectBuffer`). Наборы расстановки — `LevelScatterSets` → `ScatterSets` + слои `ScatterLayers`
(см. «Расстановка»). Грузятся только модели уровня, неба и расстановки. Тестовый уровень `Test`: террейн, частицы,
наборы `Meadow` (трава — пучки `GrassClump` из Blender, и ромашки `Camomile`) и `Debris` (камешки), Box в начале координат (его LOD видны ближе 50 м), модели Cube, Sphere,
Plane с материалом `PBR` перед стартовой камерой и перед ними таблица шаров PBR (`PBR_Dielectric_R01…R09`,
`PBR_Metal_R01…R09`: roughness 0,1…0,9), слева от неё импортированные из glTF `TestRock` (два LOD) и `TestPanel`
+ `TestPanel_Frame`.

**Заглушки ресурсов.** Слот `placeholderId` (0) в хранилищах текстур и мешей занимает процедурная заглушка:
пурпурно-чёрная шахматка (`DMTextureStorage::createPlaceholder`) и куб от −0,5 до 0,5 (`MeshStorage::createPlaceholder`).
Обе создаются в `DMGraphics::Initialize` сразу после D3D. `DMResourceStorage::get(id)`, `get(name)` и `operator[]`
вместо отсутствующего ресурса возвращают слот 0. `LibraryLoader` при ошибке загрузки файла пишет в лог и продолжает.
Id в `base.db3` начинаются с 1, поэтому со слотом 0 не пересекаются. Пока нет настоящих мешей, вместо меша подставляется
его примитив (колонка `Meshes.primitive`: `box`, `sphere`, `plane` — вписаны в куб от −0,5 до 0,5; `card` —
вертикальная карточка высотой 1 на y = 0, для травы; `MeshStorage::createPrimitive`); строка без файла — чистый примитив.
Рядом с заглушкой `DMTextureStorage::createDefaults` создаёт белую текстуру и плоскую нормаль 1×1 (id 1000001, 1000002):
их материал `PBR` подставляет вместо не заданных текстур (значение параметра 0), а шахматка по-прежнему означает
«файл не найден».

**Материал = `DMShader`.** Шейдер собирается из проходов (`addShaderPassFromFile(stage, "main", file, defines)`),
параметры материала передаются через `shader->setParams(lodBlock->params)`. Специализированные шейдеры
(`Scene/Shaders/DM*Shader`, `DMComputeShader`) наследуются от него или работают рядом. Основной материал моделей —
`PBR` (`PBRMaterial` + `Shaders/PBRLit.ps`): metallic/roughness как в glTF 2.0 и Default Lit в UE5, параметры названы
как в glTF; материал 9 `PBRInstance` — его инстансный вариант для расстановки. Режим материала — тоже параметры
с именами glTF: `AlphaMode` (0 OPAQUE, 1 MASK, 2 BLEND — Blend Mode в UE), `AlphaCutoff`, `DoubleSided`; материал
сообщает его `DMShader::renderState( params )` (`Scene/Shaders/MaterialRenderState.h`), вариант шейдера —
`phaseFor( params )`: `PBRMaterial` сам собирает второй пиксельный шейдер с `clip` (define `ALPHA_MASK`), у непрозрачных
отсечения нет. Рисуют `setPass( phaseFor( params ) )`, затем `setParams( params )`, двусторонние — без отсечения граней
(`materialRasterState`). Прочие классы — `Texture`, `Color`
(без освещения: небо, отладка), `Particle`. Подробно — `docs/materials.md`.

**Освещение считается в одном месте** — `Shaders/lighting.sh`: шейдер материала заполняет `Surface` (базовый цвет,
металличность, шероховатость, нормаль, затенение, свечение) и возвращает `evaluateLighting(surface)` — прямой свет
всех источников (BRDF — `Shaders/brdf.sh`; затухание — обратный квадрат с плавным обрезанием по радиусу, Karis 2013;
конус прожектора) плюс освещение окружением от неба (`Shaders/ibl.sh`, подробно — `docs/sky.md`). Так делают
`PBRLit.ps` и `terrain.ps`; новую составляющую освещения (тени, туман) добавляйте туда, а не в материалы. Раскладка
источника — `struct Light` в шейдере и `DMLightDriver::LightBuffer` (с `static_assert` на размер), подробно —
`docs/lighting.md`.

**Система свойств** (`src/Common/Properties`). `Property` хранит значение в `std::variant`
(bool, float, XMFLOAT2/3/4, int32, uint32) плюс границы и `GUIControlType`. `PropertyContainer` —
именованная карта свойств с вложенными контейнерами. Объекты, которые нужно крутить в рантайме
(террейн, модели, частицы), отдают `properties()`, а `DMGraphics` регистрирует их через
`m_GUI.addPropertyWatching(...)`. После этого ImGui (`Graphics/GUI/GUI.cpp`) сам строит контролы по типу свойства.

**Террейн** — `CDLODTerrain` (`Scene/Terrain/`, Strugar 2009): квадродерево над картой высот,
корень покрывает весь террейн, лист — 32 текселя, диапазон каждого уровня вдвое больше предыдущего. Узлы выбираются
на CPU в `update()` по расстоянию до AABB и frustum. При загрузке карта высот копируется с GPU (`DirectX::CaptureTexture`),
из копии строится текстура R32_FLOAT с мипами для вершинного шейдера и min/max высот узлов. Рисуются одним
`DrawIndexedInstanced` патча 16×16 (`GridMesh`). Вершина уровня L читает мип L; к концу диапазона уровня морфинг
в `Shaders/cdlod.vs` сдвигает её на сетку уровня L + 1 и переводит высоту в мип L + 1, поэтому уровни стыкуются без
скачков и трещин. Условие отсутствия трещин — диапазон уровня не меньше диагонали его узла / `morphStartRatio`,
`calcRanges()` не даёт «LOD distance» опуститься ниже. Настройки — строка таблицы `Terrain`, на которую ссылается уровень
(`heightmap` — имя текстуры в таблице `Textures`, `splatmap` — файл, `height_multiplier`, `height_offset`,
`width_multiplier`). Свойство «Show LOD» раскрашивает уровни (`Shaders/cdlod_lod.ps`). Общее для шейдеров террейна
(constant buffer `CDLODTerrain::Parameters`, выборка карты высот, выход VS) — `Shaders/cdlod.sh`.

**Материал террейна** — `TerrainMaterial` (`Terrain/TerrainMaterial.h`) + `Shaders/terrain.ps`. До четырёх слоёв из
таблицы `TerrainLayers` (`layer` 0…3 — канал splat-карты, `albedo` и `normal` — файлы, `tiling` — метров на повтор).
Текстуры слоёв собираются при загрузке в два `Texture2DArray`: «альбедо RGB + высота A» и «нормаль RGB (соглашение
DirectX) + шероховатость A»; все слои приводятся к размеру первого и к R8G8B8A8_UNORM, вместо ненайденного файла
подставляется шахматка или плоская нормаль. Мипы строятся без WIC (`TEX_FILTER_FORCE_NON_WIC`): WIC масштабирует
с премультипликацией альфы, а в альфе здесь данные, поэтому splat-карту тоже грузит материал, а не хранилище текстур.
В шейдере нормаль рельефа считается по карте высот (мип под размер пикселя), текстура слоя проецируется сверху,
а на крутых склонах ещё и вдоль X и Z (triplanar, нормали по UDN), слои смешиваются по высоте. Настраиваются
в GUI: «Triplanar sharpness», «Height blend».

Почему CDLOD, а не тесселяция или geometry clipmaps: аппаратная тесселяция как основа
LOD из практики ушла (UE5 её удалил, Far Cry 5 отказался из-за стоимости); современные движки рисуют сетку по карте
высот с морфингом между уровнями. Прежние террейны (GeoClipMap и тесселяционный) удалены, они есть в истории git.

**Расстановка (трава, цветы, камешки, веточки)** — `Scatterer` (`Scene/Scatterer/`), по объекту сцены на набор
(`ScatterSets`: имя; проход и отсечение граней слоя задаёт режим его материала). Слой набора
(`ScatterLayers`, свой `ScatterPass`: буфер инстансов, compute и `DrawIndexedInstancedIndirect`) — LOD модели, маска
плотности, шаг сетки `cell_size`, кольцо `near_border…far_border` вокруг камеры с плавным исчезанием (`*_fade`),
размер, `jitter`, предел случайного поворота по осям `rotation_x/y/z` (градусы) и `align_to_terrain`. Трава и ромашки,
кольца одного растения (пучок травы: LOD0 вблизи, LOD1 дальше), камешки — всё это слои. Каждый кадр `Shaders/scatter.cs`
раскладывает инстансы слоя по сетке, привязанной к миру (смещение, размер и поворот — хеш координат ячейки, поэтому
при движении камеры они на месте), маска даёт вероятность и размер, отсечение — по плоскостям frustum
(`DMFrustum::planes`), поворот — кватернион (`Shaders/instance.sh`, `INST_ROTATE`). Высоту и UV масок даёт
`TerrainHeightSource` (`Scene/Terrain/TerrainHeightSource.h`, реализует `CDLODTerrain`; шейдерная часть —
`Shaders/terrain_height.sh`), так что расстановка не зависит от устройства террейна. Как в Witcher 3 и UE5: у каждого
типа своя сетка, дальность и плавное исчезание; крупные камни, о которые спотыкаются, — модели уровня, а не расстановка.
Маски в координатах карты высот; тестовые создаёт `Tools/gen_terrain_textures.py`. Подробно — `docs/scatter.md`.

**Подсистемы сцены** (`src/Engine/Graphics/Scene/`): `Terrain` (`CDLODTerrain`), `Scatterer` (расстановка, см. выше),
`Particle` (`DMParticleSystem`), `Sky` (`SkySphere`), `Light` (`DMLightDriver`, свет в structured buffer), `Camera`,
`TextureObjects`, `Model`/`Mesh` (`ModelInstances`; общие вершинный и индексный буферы в `VertexPool`).

## Соглашения

- Include-пути: `src`, `src/3rdParty`, `src/Common`, `src/Engine/Graphics`, `src/Engine/Graphics/Common`,
  `src/Engine/Graphics/Scene`, `src/Engine/Graphics/Scene/Common` и корень проекта. Поэтому встречается `#include "Utils\DMTimer.h"`
  (из `src/Common`) или `"Shaders\DMShader.h"` (из `Scene`). В include используются обратные слеши.
- Большая часть графики находится в пространстве имён `GS`, классы с префиксом `DM`.
- Все исходники и шейдеры в **UTF-8 без BOM**, концы строк LF (закреплено в `.editorconfig`).
  Компилятор запускается с `/utf-8`, поэтому строковые литералы тоже в UTF-8 — так они попадают в `log.txt`
  и ImGui. Исключение: batch-файлы (`*.cmd`) с CRLF, иначе cmd ошибается на метках (`.editorconfig`, `.gitattributes`).
  Сообщения, которые печатают скрипты `Tools/`, — ASCII: их запускают и Windows PowerShell 5.1, и cmd в кодировке 866.
- `NOMINMAX` задан глобально: `std::min` / `std::max` пишутся без скобок.
- Сторонний код: `imGUI/`, `src/3rdParty/` и зависимости из `FetchContent`. Его не правим.

## graphify

This project has a knowledge graph at graphify-out/ with god nodes, community structure, and cross-file relationships.

Rules:
- For codebase questions, first run `graphify query "<question>"` when graphify-out/graph.json exists. Use `graphify path "<A>" "<B>"` for relationships and `graphify explain "<concept>"` for focused concepts. These return a scoped subgraph, usually much smaller than GRAPH_REPORT.md or raw grep output.
- If graphify-out/wiki/index.md exists, use it for broad navigation instead of raw source browsing.
- Read graphify-out/GRAPH_REPORT.md only for broad architecture review or when query/path/explain do not surface enough context.
- After modifying code, run `graphify update .` to keep the graph current (AST-only, no API cost).
