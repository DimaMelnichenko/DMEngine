# Архитектура движка

Как устроен движок целиком: цикл кадра, сцена и рендерер, виды и списки отрисовки, устройство D3D12 за фасадом, хранилища
ресурсов, данные уровня в SQLite, материалы, освещение, тени, система свойств, рельеф и расстановка. Это обзор связей между
подсистемами; подробности каждой — в её файле (оглавление — [README.md](README.md)): проходы кадра — [passes.md](passes.md),
слой D3D12 — [d3d12.md](d3d12.md), рельеф — [terrain.md](terrain.md), вода — [water.md](water.md), расстановка —
[scatter.md](scatter.md), небо — [sky.md](sky.md), материалы — [materials.md](materials.md), тени — [shadows.md](shadows.md).
Короткая карта с правилами, которые легко нарушить, — раздел «Архитектура» в `CLAUDE.md` (до 2026-10-19 этот текст был
там целиком).

**Цикл.** `main.cpp` → `DMSystem` (окно, `Config` из `settings.ini`, `DBConnector::instance().init()`, `Input::instance()`)
→ `GS::Engine::initialize` / `tick`. Пока `initialize` грузит уровень, окно показывает экран загрузки: шаги
объявляет код загрузки — `SlowTask` (`Utils/SlowTask.h`, как `FScopedSlowTask` в UE), рисует `Engine::drawLoadingFrame`;
новый долгий этап загрузки — часть или задача `SlowTask` на границе этапа (`docs/gui.md`, «Экран загрузки»).
`Engine` владеет окном, камерой, GUI и горячими клавишами, а содержимое уровня и отрисовку отдаёт двум классам:
- `Scene` (`Scene/Scene.h`) загружает ресурсы уровня из БД (`loadResources`), владеет светом и объектами сцены
  и вызывает их `update()`. Состав уровня описывает строка таблицы `Levels` (см. «Данные сцены»); объект,
  которого у уровня нет, остаётся неинициализированным и ничего не делает;
- `Renderer` (`Renderer.h`) отправляет команды GPU: общие данные конвейера (сэмплеры, свет, константы кадра и вида —
  `ConstantBuffers` (`Graphics/ConstantBuffers.h`), член `Renderer`; своим вызовам объектов — `RenderContext::constants`)
  → `compute()` всех объектов → сбор мешей с вида и раскладка по проходам с сортировкой → глубина каскадов теней
  солнца (`renderShadows`: на каждый каскад сбор с его вида и проход `csmShadowDepth` в срез карты) → туман уровня
  (`VolumetricFog`, как Volumetric Fog в UE: свет в ячейках сетки над экраном по карте теней и накопление вдоль лучей,
  `SLOT_VOLUMETRIC_FOG`, t110; только у уровня со строкой `ExponentialHeightFog`, `docs/fog.md`) → depth prepass
  (`depthPrepass`: только глубина непрозрачных и Masked вариантом `depthPhaseFor`, без цели цвета; `DepthPrepass` в
  `settings.ini`, флажок в окне «Renderer») → экранное затенение окружением по глубине prepass (`AmbientOcclusion`, GTAO
в половине кадра, `Shaders/gtao.cs`, ресурс сцены t126 — материалы гасят им свет неба, `Surface::screenOcclusion`;
настройки — в `PostProcessSettings`, `docs/ambient_occlusion.md`) → `opaque` (после prepass — `DepthState::readOnlyEqual`: освещается только
  ближайшая поверхность, Masked без `clip`; меш без варианта глубины и свой вызов без бита prepass пишут глубину сами;
  подробно — `docs/passes.md`) → `opaqueDepthRead` (после prepass — непрозрачные, которым нужна глубина сцены в шейдере:
  `Material::readsSceneDepth`, импостеры; глубина только для чтения и видна шейдерам — `SLOT_SCENE_DEPTH`, t107) → `sky`
  (фон на дальней плоскости: глубина 0 и `DepthState::readOnlyNearOrEqual` без записи — только там, где сцена ничего
  не нарисовала) → экранные отражения (`ScreenSpaceReflections`, `Graphics/ScreenSpaceReflections.h`, только если есть
  поверхности с битом `MeshPass::reflectionPrepass` — вода: пирамида глубины сцены (Hi-Z: ближняя и дальняя глубина
  ячейки, t128), проход `reflectionPrepass` — глубина поверхности и нормаль без ряби в свои цели, лучи в compute
  (`Shaders/screen_reflections.cs`, `screen_reflection_trace.sh`); результат — uv попадания, вес, доля пути, t127: цвет и
  сдвиг по ряби берёт материал, `docs/screen_reflections.md`) →
  `transparent` (alpha blending, глубина только читается; если его шейдеры читают цвет сцены — `Material::readsSceneColor`,
  `CustomBatch::readsSceneColor`, — перед ним копия HDR-буфера `SceneTargets::copyColor` в `SLOT_SCENE_COLOR`, t108, и
  глубина только для чтения в `SLOT_SCENE_DEPTH`, как SceneColor в UE) в HDR-буфер сцены. Глубина везде обратная (Reversed-Z, как в
  UE): буфер `D32_FLOAT`, 1 у ближней плоскости и 0 у дальней (`DMCamera` строит проекцию с переставленными
  плоскостями, `RenderView::nearPlane` / `farPlane` — из камеры), очистка в 0, «ближе» — `GREATER`; так же карты теней
  (ортография каскада — 1 у ближней к свету, сэмплер сравнения `GREATER_EQUAL`, наклонное смещение со знаком минус)
  (`R16G16B16A16_FLOAT`) → `PostProcess` — цепочка полноэкранных проходов с промежуточными целями (`RenderTarget`,
  `D3D/RenderTarget.h`): автоэкспозиция (гистограмма яркости и адаптация в compute, EV100, как Auto Exposure
  Histogram в UE; ночью темнее по кривой компенсации от EV100 сцены — Exposure Compensation Curve в UE), bloom
  (уровни ½ … ¹⁄₆₄, Jimenez 2014), лучи-звёзды вокруг ярких бликов (Kawase 2003: яркое над высоким порогом в четверти кадра,
  проходы вдоль осей звезды), лучи солнца (god rays, как Light Shafts в UE: маска неба у солнца и радиальное размытие к его
  экранной точке, только когда солнце в кадре — художественный эффект, физические лучи даёт туман уровня), затем ночное зрение (сдвиг Пуркинье: в темноте цвет уходит в синеватый монохром,
  Krawczyk 2005), цветокоррекция и тонмаппинг (AgX / ACES) — одной выборкой таблицы 32³ (`ColorGradingLut`, как Combine LUTs
  и Color Grading в UE: баланс белого, насыщенность, контраст, гамма, усиление и сдвиг по теням, полутонам и светам в AP1;
  строится в compute после правки подокна «Color grading») — в задний буфер sRGB; новый
  проход — ещё одна цель и шаг в `PostProcess::render`. Затем `Engine` рисует GUI и вызывает `DMD3D::endFrame`.
  Свои проходы рендерера со своими ресурсами (тени, туман, затенение окружением, экранные отражения, постобработка) — возможности рендерера
  (`RenderFeature`, `Graphics/RenderFeature.h`): список `Renderer::features`, общие точки — начало кадра с вкладом в
  константы, новый размер кадра, смена плана; окна свойств по списку регистрирует `Engine`; кадр в `Renderer::render`
  — шагами (`preparePipeline`, `prepareShadows`, `computeObjects`, `renderShadows`, `renderScene`), `docs/passes.md`.
  Съёмка сцены с другой камеры (как SceneCapture2D в UE) — `Renderer::requestCapture( SceneCapture )`: вид (`RenderView::Kind::capture`,
  `RenderView::perspective`) и свои цели (`SceneTargets`); в конце кадра проходы `opaque` без prepass и `sky`, без травы, воды,
  частиц и облаков, тени — только дальние, ресурсы главного вида подменены (`SceneObject::beginCaptureView`); проверка — команда
  `capture`; подробно — `docs/scene_capture.md`.
  Буфер сцены (HDR-цвет и глубина) — `SceneTargets` у `Renderer` (`Graphics/SceneTargets.h`): устройство о нём не знает.
  Глубина видна шейдерам (SceneDepthTexture в UE): текстура `R32_TYPELESS`, вид `depthView` (`R32_FLOAT`) и цель только
  для чтения `depthReadTarget` (`TextureViewDesc::readOnlyDepth`) — проход с ней читает ту же глубину шейдерами (layout
  `DIRECT_QUEUE_GENERIC_READ`, `docs/d3d12.md` §3.7, `docs/passes.md`).
  Задний буфер — swap chain flip model (`DMD3D::createSwapChain`: два буфера `R8G8B8A8_UNORM` с sRGB-видом, без
  vsync — tearing, начало кадра по waitable object — `DMD3D::waitForNextFrame`); `WM_SIZE` → `Engine::resize`:
  задний буфер, буфер сцены, глубина, уровни bloom и проекция камеры заново по правилу D3D12 (дождаться GPU,
  отпустить ссылки на задние буферы, `ResizeBuffers`).
  Единицы света физические (солнце — люксы, лампы — канделы, небо и свечение — кд/м²), поэтому буфер сцены хранит
  яркость × экспозицию прошлого кадра (pre-exposure, как в UE): экспозиция живёт на GPU (буфер `ExposureState`, t105,
  `Shaders/exposure.sh`), шейдеры с освещением умножают результат на `preExposure()` в конце `evaluateLighting`,
  небо — в `sky_background.ps`, частицы — в `particles.vs`; материалы без освещения (`Texture`, `Color`) пишут «цвет на экране» как
  есть. Настройки — строка `PostProcessSettings` уровня и окно GUI «Post process», подробно — `docs/postprocess.md`. Каждый проход
  начинается объявлением `PassDesc` (`D3D/GpuPass.h`: имя, цели цвета и глубины с областью вывода, что читает, что пишет;
  без целей — compute) и `DMD3D::beginPass` — единственный способ поставить цели: он же ставит барьеры целей, чтения и записи
  объявления и очищает слоты вызова таблицы привязок (t0…t16, u0…u7); ресурсы прохода привязываются после него (`Renderer::executePass`,
  `ShadowCascades::beginCascade`, `PostProcess`, небо, расстановка). Список проходов кадра — команда консоли `passes`
  (в `log.txt`, с числом барьеров у каждого прохода). Полноэкранные проходы (постобработка, небо) — `FullscreenShader`: сам ставит
  топологию, шейдеры и состояния. Время CPU и GPU (`GpuProfiler`: запросы timestamp, результат 2–3 кадра назад) каждого
  объекта и прохода — в окне «Statistic» (счётчики кадра — `FrameStats`, `Graphics/FrameStats.h`: пишут `Renderer` и
  `Engine`, показывает GUI; рендерер от GUI не зависит), те же области — метки PIX в захвате (`PIXBeginEvent`), ресурсы с именами (`DMD3D::setName`).

**Виды и списки отрисовки** — как mesh draw commands в UE: объекты не рисуют себя сами и не знают, в каком проходе
их меши. Вид кадра — `RenderView` (`Scene/RenderView.h`, ≈ FSceneView: матрицы, положение, `DMFrustum`, `lodOrigin` —
откуда считаются LOD; `index` — номер вида в кадре, место данных объекта на вид; `kind` — что за вид, по нему объект решает,
что рисовать, а не по номеру): главная камера (0, `main`), четыре каскада теней (1…4, `shadow`, `lodOrigin` — главной
камеры), дальний каскад (5, `farShadow`: только лес расстановки) и съёмка сцены с другой камеры (следующим номером,
`capture`; предел — `DM_MAX_RENDER_VIEWS` 7, `Shaders/view_limits.h`). Объект сцены наследует `GS::SceneObject`
(`Scene/SceneObject.h`): `update` / `frameConstants` (вклад в константы кадра `FrameParameters` — то, что читают чужие
шейдеры: тень облаков, детальная вода, следы в траве, поле ветра; собирает `Scene::frameConstants` по главному виду до
compute, рендерер о самих объектах не знает) / `compute` (кадр — `FrameContext`: главный вид и время) / `collectMeshes( view,
collector )` / `renderCustom( context )` / `properties`, видимость. За каждый вид (`collectMeshes` зовётся на каждый
вид кадра: выбор, зависящий от вида, объект хранит по `view.index`, как `CDLODTerrain`) объект отдаёт в `MeshCollector`
(`Scene/MeshBatch.h`) секции мешей — `MeshBatch` (≈ FMeshBatch: меш секции в `VertexPool`, материал, параметры, мировая матрица,
режим материала, расстояние) — или свой вызов `CustomBatch` с маской проходов (террейн, расстановка, небо, частицы).
`Renderer` раскладывает их по проходам (`MeshPass`: меши — по режиму материала, `passFor`; свои вызовы — по маске)
с 64-битным ключом сортировки (`Graphics/DrawSortKey.h`; непрозрачные — объект в порядке сцены, материал, вариант шейдера, растеризатор, меш;
прозрачные — от дальних к ближним между всеми объектами; `MeshBatch::instanceGroup` — «этот меш с этими
параметрами») и рисует меши одной функцией `Renderer::drawMesh` (`setPass( phaseFor )`, в проходах только глубины —
тени и depth prepass, `isDepthOnlyPass` — `depthPhaseFor`: вариант «только глубина», −1 — материал тень не отбрасывает
и в prepass не рисуется; `setParams`, растеризатор по
двусторонности и зеркальности — `materialRasterState`, матрица объекта). Одинаковые непрозрачные меши подряд — одним
`DrawIndexedInstanced` (`drawMeshInstanced`: матрицы экземпляров в структурном буфере t16, вариант вершинного
шейдера `INST_MATRIX`, который материал собирает сам — `supportsInstancing`, `phaseFor( params, true )`). Свой вызов получает `RenderContext` (вид, проход, растеризатор кадра, константы, `VertexPool`, `depthFromPrepass` —
глубина уже записана prepass); перед
ним привязан общий `VertexPool` с топологией TRIANGLELIST, свои буферы объект привязывает сам. Новый проход или вид
(тени прожекторов, экранные эффекты) добавляется в `Renderer`, а не в объекты.
Звук — `AudioDevice` (`src/Engine/Audio/`, член `Engine` до сцены, как Audio Mixer в UE): свой микшер (`AudioMixer`: голоса —
генераторы с целями громкости по каналам, нить ведёт к ним плавно) и вывод XAudio2 (`AudioOutput`, своя нить, блоки 10 мс);
голоса заводят объекты сцены, окно «Audio» — громкость и «Mute» пользователя (`[Audio]` в `settings.ini`, `-mute`) и «Clear solo»;
весь звук — раздел Audio в Outliner (`SceneObject::audio`): у частей звуков окружения и слоёв певцов «Enabled» (сохраняется) и
прослушивание «Mute» / «Solo» (`AudioAudition` у микшера, не сохраняется), слои певцов добавляются и удаляются кнопками, записи
и места — флажками, окно «Sounds» — громкость записей и «Play»; подробно — `docs/audio.md`, «Окна редактора».
Мир симуляции — `World` у `Scene` (`src/Engine/Simulation/`, ядро `DMCore`, как World с Mass в UE, на EnTT): сущности с
компонентами (`Simulation/Components.h`: `Transform`, `PreviousTransform`, `ModelVisual`), системы (`SimulationSystem`:
`WanderSystem` — блуждание в круге, Reynolds 1999), службы мира без синглтонов (рельеф — `TerrainHeightSource`) и мировые
часы `WorldClock` — одно время для неба, звуков и распорядка: календарь со скоростью «Time scale» и время игры
(`FrameContext::gameTime`, `cb_gameTime`; `-freezetime` его останавливает и мир не шагает). Столкновения — служба мира
`PhysicsWorld` (Jolt Physics за фасадом без типов Jolt, как Chaos у World в UE): земля теми же треугольниками, что
`surfaceHeight` (детальные плитки у русел — своими картами высот), тела моделей уровня по простой коллизии
`Models.collision` (`hull` по умолчанию, `box`, `mesh`, `capsule` — ствол, радиус — `Models.collision_radius`, `none`;
форма — раз на модель, кэш `ModelCollision` у `Scene`, масштаб — у тела), тела экземпляров уровня (`ModelInstances`) и
постоянных слоёв расстановки (лес — по копии пула с GPU, камни русел — с CPU; `Scatterer::buildBodies`), персонажи
`CharacterVirtual` (ходьба `WalkMode` и сущности с компонентом `Character`); поведение пишет `Locomotion`, передвигает
`MovementSystem`; луч `castRay` — и выбор во вьюпорте; отладочный вид — Debug views → «Collision»; подробно — `docs/physics.md`.
Скелетная анимация — ядро `src/Engine/Animation/` (как Skeleton, AnimSequence и Blend Space в UE): скелет и клипы (`Skeletons`,
`Animations`, файлы `.skel` / `.anim` рядом с мешами; импорт — `Tools/import_gltf.py` выносит движение корня), служба мира
`World::animations()`, компонент `AnimationState` и `AnimationSystem` после передвижения — шаг по фактическим скорости и
повороту (`LocomotionBlendSpace`: ряды аллюров с вариантами влево и вправо, фаза держит смешанный шаг на скорости — копыта не
скользят), переход между клипами, цепочки (`playSequence`); клип-действие ведёт сущность движением корня
(`MovementSystem`, `AnimationClip::rootMotionDelta`); позу собирает
отрисовка (`evaluatePose` в `compute` у `WorldEntities` — с LOD анимации: дальше реже, невидимые без позы — и у
`ModelInstances` — экземпляр уровня с клипом `LevelModels.animation_id`) в `SkinningPalette`, скиннинг — вариант материала
`SKINNED` (`Shaders/skinning.sh`, веса — третий поток `VertexPool`; стадо — одним инстансным вызовом, первая кость — в
`InstanceTransform`); команды `world speed` / `anim`, вид «Skeleton»; подробно — `docs/animation.md`. Кадр начинается с
`Scene::advanceWorld`: правки окна «Sun position» (органы часов) — в часы, шаг часов, шаги систем фиксированной длительности
(60 Гц, не больше 4 за кадр, остаток — дальше; «Fix Your Timestep»). Рисует сущности объект сцены `WorldEntities` (общей с
`ModelInstances` функцией `collectModelMeshes`, `Scene/Model/ModelDraw.h`; место — смешанное по `World::interpolation`),
окно «World», команда `world` (`spawn` — бродяги для проверки); подробно — `docs/simulation.md`.
Сейчас объекты (в порядке сцены): небо — `SkyAtmosphere` (процедурное небо фоном и освещение окружением от него,
таблицы Hillaire 2020 как в Sky Atmosphere UE5; его `compute()` идёт первым: при смене атмосферы — таблицы пропускания
и Ψ, каждый кадр — небо вокруг камеры, Sky-View, и объём воздушной перспективы главного вида — t106; при смене солнца —
cubemap из Sky-View в `SkyLight` — гармоники и префильтр по шагу за кадр с тремя результатами и плавным переходом
от прежнего к новому (`blendFrames`), слоты PS t101…t103; ночью — ещё луна (вторая таблица Sky-View), звёзды и
свечение ночного неба; то, что читает каждый пиксель (объём, префильтр), — half-float с нормировкой на яркость неба,
`SkyAtmosphere::skyNormalization`, масштабы кадра `cb_skyLightScale` / `cb_skyScale` / `cb_aerialPerspectiveScale`,
подробно — `docs/sky.md`; фон
из Sky-View — полноэкранный треугольник на дальней плоскости в проходе `sky`) или вместо него `HDRIBackdrop` (HDRI-панорама уровня: фон и cubemap для `SkyLight`, без воздушной перспективы),
облака — `VolumetricCloud` (как Volumetric Cloud в UE5, только с атмосферой: строка `VolumetricCloud`,
`Levels.volumetric_cloud`, `Tools/volumetric_cloud.py`; шумы при загрузке, каждый кадр — карта тени облаков вокруг камеры
(слот сцены `SLOT_CLOUD_SHADOW`, t111, константы `cb_cloudShadow`; её читают `sunShadow` и `volumeShadow` — все
приёмники тени солнца) и луч главного вида через слой в половине кадра, в проходе `sky` — поверх фона неба; плывут по
ветру уровня, `-nowind` их останавливает; `docs/clouds.md`),
`SkySphere` (модель неба уровня, если задана — тогда атмосфера только освещает; сфера
растягивается до 0,9 дальней плоскости вида, `RenderView::farPlane`), `TerrainPipeline` (данные рельефа — конвейер
рельефа и воды, мешей нет: его `compute()` привязывает ресурсы сцены рельефа), `CDLODTerrain` (отрисовка рельефа),
`FarTerrain` (дальние горы вокруг карты, 64 × 64 км по своей карте высот 32 м: строка `FarTerrain`, `Levels.far_terrain`;
та же сетка узлов CDLOD — карта дырой в их дереве (`CDLODQuadtree::setHole`), общая таблица дальностей
(`CDLODTerrain::setFarLevels`) и общий код вершины `Shaders/cdlod_patch.sh` — стык без трещин; материал — правила по высоте и
уклону на слоях по ролям (`Shaders/far_terrain.*`, смесь слоёв — `terrain_material.sh`, общая с картой); эрозия гор при
загрузке с кэшем `far_eroded\` — реки (`FluvialErosion`, FastScape, рельеф из подъёма), ледники (`GlacialErosion`, приближение
мелкого льда на GPU), морозные зубцы гребней, ледники наших дней (тот же лёд без эрозии: поверхность — в рельеф, толщина —
материалу; морены — по линиям течения льда от концов отрогов и вдоль стен, трещины — в шейдере поперёк течения); только
главный вид; `docs/far_terrain.md`),
`ModelInstances` (экземпляры моделей уровня: LOD по расстоянию от точки LOD вида — в полосе перехода оба LOD с
дизерингом, если у материала `DitheredLODTransition`; у каскадов теней дальности × `Models.shadow_lod_scale` — у елей 0,5,
его же берёт расстановка; отсечение по frustum вида — границы
меша `AbstractMesh::bounds` считаются при загрузке, меши в список отрисовки; общая функция `collectModelMeshes`),
`WorldEntities` (сущности мира симуляции с моделью — та же функция, место смешано по доле шага; не в съёмке сцены),
`FoliageInteraction` (трава расступается: поле следов 48 м вокруг камеры — compute `Shaders/foliage_interaction.cs`,
ресурс сцены `SLOT_FOLIAGE_INTERACTION`, t124, окно — `cb_foliageInteraction`; растения с `WindWeight` > 0 наклоняются от
корня в `vertexWorldPosition` — `Shaders/foliage_interaction.sh`; давит камера у земли, звери — `addInteractor`; окно
«Foliage interaction», не сохраняется; `docs/wind.md`),
`WaterSimulation` (вода — части на общем поле `WaterField`: трубы `WaterPipes`, статичная вода с течением и кэшем
`StaticWater`, поле пены, которую несёт течение, `WaterFoam` (при загрузке, `Shaders/water_foam.cs`; счёт по плиткам детальной воды при загрузке — `WaterGrid`, `Shaders/water_detail_grid.sh`), поверхность `WaterSurface`, кривые русел в редакторе `StreamCurves`; режим строки `mode`: `static` — на `Test`, как реки-сплайны и озёра UE Water, но одним полем:
уровень ручьёв (Маннинг) и озёр (перелив) — одно поле по сети с подпором, растр, поверхность — те же тайлы, что у
симуляции; вода для травы и мокрой земли — тот же растр, всё из конвейера рельефа и воды (`TerrainHydrology`), без шагов;
течение ручьёв и озёр — установившийся расчёт при загрузке (или из кэша `cache\water\flow_<ключ>.bin` по отпечатку входа и
настроек, `StaticWater::solve`), `Shaders/water_flow.cs`: мелкая вода под «жёсткой крышкой» на сетке MAC
одной областью, ручей держит сила тяжести вдоль уклона (равновесие — скорость по Маннингу), расход — от сети ручьёв; поток в
руслах, слияния, струя ручья в озере и тяга к истоку, подокно «Flow»; камни русла — часть кривых (`TerrainHydrology`: дно,
борта, валуны; ставит их слой расстановки с `ScatterLayers.placement` = `stream_stones`), тормозят течение и пенят воду
у себя, `docs/water.md`, «Камни в русле»; `simulated` — вода по рельефу, как в From Dust:
модель виртуальных труб на сетке карты высот с трением по Маннингу,
источники по карте водосбора и помощники — родник, ледниковое озеро (строки `WaterSources`, `Tools/water_source.py`),
озёра до уровня перелива и прогрев при загрузке, шаги по времени кадра; русла горных ручьёв — растровая правка рельефа `channels` по
сети стока D8 — ступень конвейера рельефа и воды при загрузке (`Scene/Terrain/TerrainHydrology.h`, вызывает
`TerrainPipeline::buildHeights`: карта высот → эрозия `TerrainErosion` → ручные правки `TerrainEdits` → русла, ручьи и озёра по итоговому рельефу →
вода; узкое дно, врез с крутыми бортами, извилины на пологом, ступени и плёсы на крутом (step-pool, уступы из камней), галька и без травы; настройки — строка `WaterChannels`,
подокно «Channels»; русла — по кривым из базы (`Streams` / `StreamPoints`, как Landscape Splines): генератор предлагает их
по стоку, когда сменился отпечаток (`StreamGeneration`: параметры генератора и приток из базы, без высот рельефа — у разных
машин рельеф чуть разный) или по `Tools/stream_edit.py regenerate`, правленные и ручные остаются; правка — точки во вьюпорте (View →
«Stream curves»: гизмо с мягким выделением, Insert / Delete), подокна «Streams» и «Channels» — с перестройкой русел, воды и
леса без перезагрузки (`Scene::rebuildChannels`), и `Tools/stream_edit.py`; приток симуляции — водосбор вдоль оси русел; выгрузка — команда `terrain hydrology`); глубина с памятью ~30 с — для травы и мокрой земли; текстура воды —
слот сцены `SLOT_WATER`, t109, `Shaders/water.sh`; уровень поверхности — `SLOT_WATER_LEVEL`, t115: по нему глубина в
точке (`waterDepthAt`, урез внутри клетки) — мокрая земля, галька дна озёр, трава в воде; у русел (режим `static`) вода —
на плитках детальной земли 0,25 м (`SLOT_WATER_DETAIL`, t116, `cb_waterDetail`; `Shaders/water_detail.cs`, второй
ExecuteIndirect поверхности ближе `detail_distance`), течение в них — тот же расчёт на детальной сетке, вложенный в
сетку 1 м (`water_flow.cs` с `FLOW_DETAIL`: камни — поднятое дно, крупный масштаб тянется к сетке 1 м); строка `WaterSimulation` уровня, `Levels.water_simulation`;
поверхность — свой вызов в `transparent` с цветом и глубиной сцены: тайлы сетки с водой отбирает compute, один
ExecuteIndirect, берег — пересечение продолженной за урез плоскости воды с рельефом, `water.vs` / `water.ps`: поглощение
по толщине, экранные отражения (луч — проход `ScreenSpaceReflections`, `water.ps` сдвигает попадание по ряби), блик с искрами (грани ряби мельче пикселя — дискретный блеск, `sparkleWeight`), рябь и пена, которые несёт течение (рябь — плитками, повёрнутыми и вытянутыми по
течению, карта `WaterRipples`; стоячие волны над перекатами — неподвижный узор на тех же плитках, длина из дисперсии волн, сила по числу Фруда; пена — текстура `water_foam` на тех же плитках, `Tools/pack_water_foam.py`, без неё —
узор по ряби); террейн по воде темнеет и блестит — мокрая земля, `waterWetness`; расстановка в воде
не растёт — `waterFoliageClear` в `scatter.cs`; подсветка «Show water» у террейна; `docs/water.md`),
`Scatterer` (по объекту на набор расстановки уровня: трава, камешки), `ParticleSystem` (частицы на GPU, как Niagara в UE:
эмиттеры уровня — строки `LevelParticleEmitters` → `ParticleEmitters`, `Tools/particle_emitter.py`; пул, стек мёртвых и
список живых в compute `Shaders/particles.cs`, рождение по маске вокруг камеры или на быстрой воде, ветер, вихри, течение,
столкновения; один ExecuteIndirect на эмиттер в `transparent` с глубиной сцены — `CustomBatch::readsSceneDepth`; на
`Test` — пыльца, хвоя, брызги; `-noparticles` — нет; `docs/particles.md`), `ReflectionCaptures` (карты отражений места, как
Sphere Reflection Capture в UE: строки `LevelReflectionCaptures`, `Tools/reflection_capture.py`; грани — съёмкой сцены без неба
по грани за кадр (`SceneObject::requestCapture`), покрытие в альфу и расстояния от точки съёмки в свою карту, префильтр
`CubePrefilter` (общий с `SkyLight`); две ближайшие к камере — ресурсы сцены t131…t134, `cb_reflectionCapture*[2]`; вода там,
где промахнулись экранные отражения, — луч по карте расстояний до того, во что упёрся (distance impostors), и небо сквозь
непокрытое, `Shaders/reflection_capture.sh`; точку ставят над водой подальше от моделей; `docs/reflection_captures.md`),
`AmbientSound` (звуки окружения синтезом: ветер у камеры, шелест травы и кроны леса по сторонам — по ветру уровня, полю ветра
и маскам, `Wind::gust`, `TerrainPipeline::maskValues`; ручьи — места воды 8 × 8 м по растру `staticWater`, громкость — по
потере мощности течения, слышны самые громкие, заглушение гребнем; звучат записями по кругу (`LoopGenerator`: спокойная вода и
пороги — по уклону воды места; записи — `SoundRecordings`, их даёт `Soundscape`; без записей — синтез пузырьков); строка `AmbientSound`, `Levels.ambient_sound`, `Tools/ambient_sound.py`;
мешей нет — цели голосов микшера в `update`, `docs/audio.md`), `Soundscape` (певцы из записей, как Soundscape в UE: слои
`SoundscapeLayers` — записи `Sounds` (у записи — своя громкость), маски места, правила `SoundRules` (активность — их
произведение; тип правила — строка таблицы `soundRuleTypes` в ядре, `Audio/SoundRules.h`; сейчас — часы суток и ветер уровня: так скрипят деревья); певец садится вокруг слушателя и поёт фразы записи с паузами;
`Tools/soundscape.py`, записи — `Tools/pack_sound.py` через ffmpeg в `Sounds\` (не в git, как `Textures\`), `docs/audio.md`). Новый объект добавляется членом `Scene` и строкой в `Scene::initialize`;
его свойства GUI подхватит сам.

Логику, которая меняет состояние сцены, пишите в `SceneObject::update()`, а в `render()` оставляйте только команды GPU.

**Глобальные синглтоны** — основной способ связи подсистем, все в одном стиле — статические члены класса
(`Class::instance()`, при явном разрушении — `Class::destroy()`; у хранилищ — `Assets::textures()` и т. п.):
- `DMD3D::instance()` — устройство D3D12 за фасадом (`Graphics/D3D/`: очереди, кадры в полёте с fence, кучи дескрипторов,
  кольцо констант, проходы, пайплайны). Ресурсы GPU объекты держат только непрозрачными
  (`D3D/GpuResources.h`: `Buffer`, `Texture`, виды `ShaderView` / `TargetView` / `StorageView`, `ShaderStage`, `InputLayout`)
  и создают по описаниям без типов D3D12 (`BufferDesc` с маской `BufferUsage`, `TextureDesc`, `TextureViewDesc`,
  `VertexElement`): `createBuffer`, `createTexture`, `createShaderView` / `createTargetView` / `createStorageView`;
  картинки DirectXTex — `D3D/TextureImages.h`. Константы кадра (`BufferUsage::constant | cpuWrite`, `createShaderConstantBuffer`)
  — участки кольца `D3D/ConstantRing.h`: `Device::updateResource*` пишет участок, `setConstantBuffer` в том же кадре привязывает
  его со смещением (без записи в кадре привязывать нечего); структурные данные кадра (`BufferUsage::cpuWrite` без `constant`:
  инстансы, патчи, свет — `DMStructuredBuffer`) — тоже участки кольца, с временным SRV на кадр (`setSRV( слот, буфер )`), поэтому
  их пишут каждый кадр перед привязкой; редко меняющиеся константы — `BufferUsage::constant` и `updateBuffer`; участок текстуры без пересоздания (живые
  правки: маски кисти) — `updateTexture`. Привязка — `setSRV( slot, view )` (стадии нет: таблица привязок одна на вызов), `setConstantBuffer`,
  `setUAV`, `setVertexBuffer(s)` / `setIndexBuffer`; вызовы — `draw*`, `dispatch`; цели — только через
  `beginPass( PassDesc )` (`D3D/GpuPass.h`; задний буфер — `backBufferTarget()`, буфер сцены — `SceneTargets` рендерера); `setSRV` вида ресурса, который пишет текущий проход, завершает проход над ним (цели снимаются,
  UAV отвязывается — так ресурсы сцены t101…t106 привязываются сразу после своих проходов); копия заднего буфера
  (`captureBackBuffer` — снимки пишет `Engine::saveScreenshot`); состояния растеризатора/глубины/блендинга
  (`setState(RasterState | DepthState | BlendState | RenderState)`; смещение глубины — `RenderState::depthBias`).
  Реализация `DMD3D` — по файлам: `DMD3D.cpp` (устройство, кадры, swap chain), `DMD3DPasses.cpp`, `DMD3DPipelines.cpp`,
  `DMD3DResources.cpp`, `DMD3DCommands.cpp`. Правило: `ID3D12*` и `D3D12_*` — только внутри
  `src/Engine/Graphics/D3D/` (`grep -rlE "ID3D12|D3D12_" src` вне него пуст), `device()` / `commandList()` /
  `shaderVisibleHeap()` — для этого каталога и привязки ImGui (`imgui_impl_dx12`). `DXGI_FORMAT` и
  `D3D_PRIMITIVE_TOPOLOGY` допустимы везде. Виды (`ShaderView`, `StorageView`, `TargetView`) — постоянные дескрипторы
  в кучах DMD3D (bindless): освобождаются вместе с видом.
  Состояния и шейдеры ставятся в контекст одним объектом пайплайна (`D3D/GpuPipeline.h`: `PipelineDesc` — стадии фазы,
  раскладка, состояния, топология; кэш `DMD3D::pipeline`, привязка `setPipeline`) в `ShaderProgram::setPass`; `setState`
  лишь запоминает состояние, поэтому порядок в коде объекта — состояние → `setPass` → рисование. Набор пайплайнов
  собирается при загрузке (`Renderer::warmPipelines`, свои списки у объектов), созданный в кадре — «ленивый»: строка
  в лог и счётчик в «Statistic» — дополните список прогрева.
  Проход, которому нужно другое состояние, меняет его через RAII-объект `ScopedRenderState`
  (`ScopedRenderState state( DepthState::disabled, RasterState::frontCulling );`): в деструкторе он восстановит
  предыдущие состояния;
- `GS::Assets::textures() / meshes() / models() / materials()` — хранилища ресурсов, реестр ассетов
  (`Graphics/Assets.h`, как AssetManager в UE; `Assets::destroy()` в `Engine` — до устройства; `DMResourceStorage<T>`, доступ по id или по имени; путь хранилища — подкаталог: `Textures`, `Meshes`,
  `Models`, `Shaders`);
- `DBConnector::instance().db()` — `SQLite::Database` на `base.db3`; `Input::instance()` — Raw Input (`WM_INPUT` из `DMSystem::wndProc`: клавиши по скан-кодам `DIK_*`, смещения мыши)
  + `KeyEventNotifier`; обычные сообщения окна (ImGui, ввод текста, Ctrl+S, раскладка, Alt+Tab) не перехватываются.

**Данные сцены в SQLite (`base.db3`).** `ObjectLibrary/LibraryLoader` по id загружает из БД текстуры,
материалы, шейдеры материала (`MaterialShaderView`: файл, тип стадии, defines), определения
и значения параметров материала (`MaterialParameterDefView`, `MaterialParamsValueView`), меш и модель с LOD
(`ModelProperties`: строка на секцию LOD — lod, section, range, material, mesh, material instance; секции — как sections
Static Mesh в UE и primitives glTF, `DMModel::Section`). Материалы грузятся все, что есть в таблице
`Materials`; класс шейдера выбирает `MaterialStorage::createMaterial` по колонке `class`. Параметры секции —
экземпляр материала: `ModelProperties.material_instance_id` → `MaterialInstance` (определения параметров — от его
`id_material`, значения — `MaterialParameterInstance`); NULL — параметры `material_id` со значениями по умолчанию
(`MaterialParameterDef.default_value`). Экземпляр в памяти один на все секции с ним (`Assets::materialInstances()`,
`DMModel::Section::params` — указатель): правка в окне (у моделей уровня — их экземпляры, у слоя расстановки — подокно
«Materials») видна у всех его моделей и LOD, «Save level» пишет изменённое в `MaterialParameterInstance`
(`LibraryLoader::saveMaterialInstances`). Экземпляр 12 — наследие старой схемы, где эта колонка означала
`Materials.id`; у SkySphere — свой экземпляр `Texture` с `Albedo` = `sky3`. Подробно — `docs/materials.md`. Цветовое
пространство текстуры задаёт `Textures.sRGB` (1 — цвет, 0 — данные), а не метаданные файла; мипы с сохранением
покрытия альфы (для Masked: трава, лепестки) — `Textures.preserve_alpha_coverage` (порог); сжатие на GPU —
`Textures.compression` (`default` — BC7, `normalmap` — BC5, `none`), готовая текстура — в `cache\textures\` по отпечатку
файла и настроек (`docs/materials.md`). Новые ассеты добавляются
строками в БД, а не кодом; модели из Blender — экспорт glTF и `Tools/import_gltf.py` (файлы мешей и текстур + строки
моделей, LOD, экземпляров материала `PBR`, расстановки: положение, поворот и масштаб объекта Blender — экземпляр
в `LevelModels`, связанные дубликаты — экземпляры одной модели; источники `KHR_lights_punctual` — строки `LevelLights` по
имени; модель со скином — скелет и клипы, `docs/animation.md`; повторный импорт обновляет), подробно — `docs/models.md`.
Слои: что уровень прочитал из базы — только данные, `Scene/Level/` (`LevelDescription`, `LevelSettings.h` — настройки
неба, панорамы, постобработки, ветра, солнца и слоёв расстановки без объектов сцены; у объектов те же структуры под
именами `SkyAtmosphere::Settings` и т. п.). Заголовки `ObjectLibrary` включают только их, а сам загрузчик знают лишь
`Scene.cpp` и `Engine.cpp`: новый параметр уровня — поле в `LevelSettings.h`, а не включение объекта в загрузчик.
Свет и окружение — тоже данные уровня, как сущности уровня в UE: источники — строки `LevelLights` (`type` directional /
point / spot, `enabled`, `color` и `intensity` раздельно, `direction` — куда идёт свет, `position`, `attenuation_radius`,
`inner_cone_angle` / `outer_cone_angle` — имена как в UE и KHR_lights_punctual; у направленного ещё `cast_shadows`
и настройки каскадных теней, `atmosphere_sun_light` — как Atmosphere Sun Light в UE: цвет и интенсивность над
атмосферой, у земли × пропускание по лучу к солнцу, `SkyAtmosphere::sunTransmittance` на CPU по общим с HLSL параметрам
`Shaders/atmosphere_constants.h`; первый включённый направленный — солнце для неба и теней; луна — направленный с
`atmosphere_sun_light_index` 1 (второе светило атмосферы: положение, фаза и освещённость по дате — `SunPosition::moon`,
своя таблица Sky-View, диск с картой NASA в фоне; тени ночью — от неё, `DMLightDriver::shadowLight`); `intensity` направленного —
люксы, точечного и прожектора — канделы, затухание — `1 / max(d², 0,01²)`), время суток — строка `SunPosition`
(`Levels.sun_position`, как Sun Position в UE: широта, долгота, часовой пояс, `north_offset`, дата, `time_of_day`,
`time_scale` — скорость цикла дня и ночи, время идёт у мировых часов (`WorldClock`: подокно «Sun position» — их органы,
`SunPosition` считает светила по их времени), пауза — «Time paused» в GUI →
направление первого направленного, `Light/SunPosition.h`, формулы NOAA; NULL — Pitch / Yaw солнца), небо — строка `SkyAtmosphere`
(`Levels.atmosphere`; запекается для солнца 1 лк и умножается на освещённость от солнца — `cb_skyScale` у фона, `cb_skyLightScale` у освещения окружением; там же сила воздушной перспективы)
или панорама — строка `HDRIBackdrop` (`Levels.hdri_backdrop`: файл, `intensity` — кд/м² на единицу панорамы, она же
`cb_skyScale` и `cb_skyLightScale`, `rotation`, `max_luminance` — срез солнца для освещения окружением; уровень `TestHDRI`), постобработка — `PostProcessSettings`
(`Levels.post_process`; NULL — значения по умолчанию), туман — строка `ExponentialHeightFog` (`Levels.height_fog`, как
Exponential Height Fog с Volumetric Fog в UE: два слоя по высоте, альбедо, анизотропия, объём и его дальность; NULL —
тумана нет; `Tools/height_fog.py`, `docs/fog.md`), облака — строка `VolumetricCloud` (`Levels.volumetric_cloud`;
NULL — облаков нет), ветер — строка `Wind` (`Levels.wind`, как Wind Directional Source
в UE: направление, сила, порывы волнами; класс `Wind` у `Scene`, константы кадра `cb_wind*` и время игры `cb_gameTime` —
мировых часов, изгиб в `vertexWorldPosition` у материалов с `WindWeight` > 0 — `Shaders/wind.sh`, подробно — `docs/wind.md`;
поле ветра по рельефу — ступень конвейера `TerrainWind` (поток над рельефом, подветренная тень, вертикаль — до масок:
по ним снег в правилах слоёв; лес — после масок и моделей), ресурс сцены `SLOT_WIND_FIELD`, подокно «Field» окна «Wind»,
кэш `generated\wind.dds`;
деревья — материал `PBRTree` (define `WIND_TREE`): слои Games wind SpeedTree по данным второго потока вершин `VertexPool` —
начала и веса ветвей двух уровней, доля высоты, рябь, атрибуты glTF `_WIND_*`, блок `WIND` файла меша).
В GUI это окна «Lights» (подокно на источник, Pitch / Yaw
вместо вектора; «Sun position» — время суток; кнопки «Add … light» и «Delete» — новый источник перед камерой и удаление,
«Save level» вставляет и удаляет строки `LevelLights`), «Sky atmosphere» или «HDRI backdrop», «Volumetric cloud», «Post process», «Height fog», «Wind»; кнопка «Save level»
(`GUI::addAction`) пишет их обратно вместе с окнами объектов сцены (`LibraryLoader::saveLevelEnvironment`; чтение и запись окружения — `ObjectLibrary/LibraryLoaderEnvironment.cpp`).
Пресеты окружения — таблица `LevelPresets` (именованные наборы строк времени суток, неба, постобработки, ветра, тумана и
облаков; `locked` — «Save level» не пишет, так заморожен эталон проверок `Reference`): выбор при запуске — `-preset`, на
лету — File → Preset и команда `preset`, «Save preset as» — копии строк; загрузчик — `ObjectLibrary/LibraryLoaderPresets.cpp`,
окна объектов — `apply` по `writeSettingProperties`; подробно — `docs/gui.md`, «Пресеты окружения». Размер карты теней — `ShadowMapResolution` в `settings.ini` (качество, а не
уровень). Источники раз за кадр обновляет `Scene::updateLights` в `Engine::tick` (правки GUI и время
суток, затем пропускание атмосферы для солнца), буфер `DMLightDriver::setBuffer` упаковывает в `preparePipeline`
и загружает на GPU, только когда источники изменились.

Состав уровня (`LibraryLoader::loadLevel` → `LevelDescription`, `Scene/Level/`): строка `Levels` ссылается на террейн (`Terrain`,
слои материала — `TerrainLayers`; строка и слои — `TerrainDescription`: конвейер рельефа и материал о базе не знают), модель неба (`Models`); NULL — этого у уровня нет; эмиттеры частиц — строки
`LevelParticleEmitters`. Экземпляры моделей уровня — `LevelModels`: строка на экземпляр (`position`, `rotation` —
кватернион `x,y,z,w` как в glTF, `scale`), у модели их может быть сколько угодно. Модель (`DMModel`: LOD из секций —
меш и материал) — общий ресурс без положения; положение держит экземпляр (`DMTransform` в `ModelInstances`, у неба — в
`SkySphere`), мировая матрица и матрица нормалей (обратная транспонированная) уходят в константный буфер объекта
(`ConstantBuffers::setPerObjectBuffer`). Наборы расстановки — `LevelScatterSets` → `ScatterSets` + слои `ScatterLayers`
(см. «Расстановка»). Грузятся только модели уровня, неба и расстановки. Тестовый уровень `Test`: террейн,
наборы `Meadow` (папоротник Megascans под ельником; трава — пучки `GrassClump` из Blender,
три LOD до 30 м с тенью от LOD2, дальше до 45 м — плоские импостеры без тени,
пучки сухой травы Megascans `dry_grass` своим слоем вдоль ручьёв — `mask_grass_riparian` и одуванчики `dandelion_01`) и `Debris` (камни `rock_moss_set_01`: галька по маске и камни русел — слой `stream_stones`, ~7 тыс.), Box в начале координат (его LOD видны ближе 50 м), модели Cube, Sphere,
Plane с материалом `PBR` перед стартовой камерой и перед ними таблица шаров PBR (`PBR_Dielectric_R01…R09`,
`PBR_Metal_R01…R09`: roughness 0,1…0,9), слева от неё импортированные из glTF `TestRock` (два LOD) и `TestPanel`
(две секции: панель и металлическая рамка), к северу от луга — роща елей (`Fir_A/B/C`, 23 экземпляра), на склонах — лес:
набор `Forest` (те же ели постоянным слоем по `mask_forest_spruce`, ~12,9 тыс. деревьев; до 90 м — модели, дальше до
1500 м — импостеры), частицы — пыльца над лугом, хвоя под ельником, брызги на быстрой воде (для сравнения кадров —
`-noparticles`); позади стартовой камеры, у опушки (600, 140), — зверь со скелетом, играет клип уровня (вне кадров
контрольных камер; `world spawn` его модели — бродяги шагом по скорости, `docs/animation.md`).

**Заглушки ресурсов.** Слот `placeholderId` (0) в хранилищах текстур и мешей занимает процедурная заглушка:
пурпурно-чёрная шахматка (`DMTextureStorage::createPlaceholder`) и куб от −0,5 до 0,5 (`MeshStorage::createPlaceholder`).
Обе создаются в `Engine::initialize` сразу после D3D. `DMResourceStorage::get(id)`, `get(name)` и `operator[]`
вместо отсутствующего ресурса возвращают слот 0. `LibraryLoader` при ошибке загрузки файла пишет в лог и продолжает.
Id в `base.db3` начинаются с 1, поэтому со слотом 0 не пересекаются. Пока нет настоящих мешей, вместо меша подставляется
его примитив (колонка `Meshes.primitive`: `box`, `sphere`, `plane` — вписаны в куб от −0,5 до 0,5; `card` —
вертикальная карточка высотой 1 на y = 0, для травы; `MeshStorage::createPrimitive`); строка без файла — чистый примитив.
Рядом с заглушкой `DMTextureStorage::createDefaults` создаёт процедурные текстуры: монохромный шум 256² для террейна
(`noiseId`, 1000000) и белую текстуру и плоскую нормаль 1×1 (`whiteId`, `flatNormalId` — 1000001, 1000002):
их материал `PBR` подставляет вместо не заданных текстур (значение параметра 0), а шахматка по-прежнему означает
«файл не найден».

**Материал = `Material`** (`Scene/Materials/`: ресурс хранилища `Assets::materials()`, класс по колонке `class` —
`MaterialStorage::createMaterial`). Программу шейдеров — стадии (`addShaderPassFromFile(stage, "main", file, defines)`),
фазы-варианты, раскладку, `setPass`, прогрев — он наследует от `ShaderProgram` (`Graphics/ShaderProgram.h`; ею же пользуются
шейдеры без материала: террейн, `FullscreenShader`), сам добавляет определения параметров (`parameters()`) и что зависит
от параметров секции: `setParams`, `renderState`, `phaseFor`, `depthPhaseFor`. Материал не рисует: вызов `DMD3D::draw*` —
у вызывающего (`Renderer::drawMesh`, свои вызовы объектов). Основной материал моделей —
`PBR` (`PBRMaterial` + `Shaders/PBRLit.ps`): metallic/roughness как в glTF 2.0 и Default Lit в UE5, параметры названы
как в glTF (сила отражения диэлектрика — `SpecularFactor`, KHR_materials_specular); материал 9 `PBRInstance` — его инстансный вариант для расстановки. Режим материала — тоже параметры
с именами glTF: `AlphaMode` (0 OPAQUE, 1 MASK, 2 BLEND — Blend Mode в UE), `AlphaCutoff`, `DoubleSided`; материал
сообщает его `Material::renderState( params )` (`Scene/Materials/MaterialRenderState.h`), вариант шейдера —
`phaseFor( params, options )` — фаза по ключу перестановки (биты `ShaderPermutation`: экземпляры, смена LOD, отсечение, только
глубина, запекание, скиннинг; ключ вызова — `Material::permutationKey`, фазы по ключам собирает материал — `ShaderProgram::setPermutation`,
как permutation domains в UE; `docs/materials.md`, «Перестановки»): `PBRMaterial` сам собирает второй пиксельный шейдер с
`clip` (define `ALPHA_MASK`), у непрозрачных отсечения нет; `ShaderPhaseOptions` — что зависит от вызова: инстансы, глубина из prepass (цвет без `clip`), смена LOD
дизерингом (`LOD_DITHER`, `Shaders/lod_dither.sh`, как Dithered LOD Transition в UE; флаг материала
`DitheredLODTransition`). Рисуют `setPass( phaseFor( params ) )`, затем `setParams( params )`, двусторонние — без отсечения граней
(`materialRasterState`). Прочие классы — `Texture`, `Color`
(без освещения: небо, отладка; `TextureMaterial`, `ColorMaterial`). Подробно — `docs/materials.md`.

**Освещение считается в одном месте** — `Shaders/lighting.sh`: шейдер материала заполняет `Surface` (базовый цвет,
металличность, шероховатость, нормаль и геометрическая нормаль без карты нормалей, затенение, свечение, пропускание —
доля и цвет света насквозь, как KHR_materials_diffuse_transmission в glTF и Two Sided Foliage в UE, по Ламберту и пиком
вперёд против солнца с сохранением энергии, `transmissionLobe`) и возвращает
`evaluateLighting(surface)` — прямой свет всех источников (BRDF — `Shaders/brdf.sh`; затухание — обратный квадрат
с плавным обрезанием по радиусу, Karis 2013; конус прожектора; у солнца — тень) плюс освещение окружением от неба
(`Shaders/ibl.sh`, подробно — `docs/sky.md`), затем туман уровня и воздушная перспектива до камеры (`applyFogging`,
`Shaders/height_fog.sh`: туман — из объёма `VolumetricFog` ближе его дальности и по формуле дальше, `docs/fog.md`;
воздушная перспектива — `Shaders/aerial_perspective.sh`:
выборка объёма 32 × 32 × 32 над экраном, который каждый кадр считает `SkyAtmosphere` по модели неба —
`Shaders/aerial_perspective.cs`, как Camera Aerial Perspective Volume в UE5; сила —
`SkyAtmosphere.aerial_perspective_view_distance_scale`). Так делают `PBRLit.ps` и `terrain.ps`; новую составляющую освещения
добавляйте туда, а не в материалы. Раскладка источника — `struct Light` в `Shaders/lights.sh` (общий с туманом)
и `DMLightDriver::LightBuffer` (с `static_assert` на размер), подробно — `docs/lighting.md`.

**Тени солнца** — `ShadowCascades` (`src/Engine/Graphics/ShadowCascades.h`, владеет `Renderer`), как Cascaded Shadow
Maps у directional light в UE: 4 каскада до Dynamic Shadow Distance (200 м, границы по Cascade Distribution Exponent),
каскад — ортографический `RenderView` вдоль солнца на описанную сферу своей части frustum; размер сферы постоянен,
центр привязан к сетке текселей (тень не дрожит); ближняя и дальняя плоскости — по `Scene::bounds` (террейн и модели).
Карта — `Texture2DArray` D32 2048², растеризатор `RasterState::csmShadowDepth` (без отсечения граней и по глубине,
наклонное смещение солнца — `RenderState::depthBias`: состояние прохода теней даёт `ShadowCascades::renderState`,
пайплайны для него прогревает `Renderer::warmPipelines` / `warmShadowPipelines`, объекты со своими вызовами — через
`SceneObject::warmPipelines( PassStates )`). Отбрасывают тень меши с `castsShadow` материалов с `depthPhaseFor` (у `PBR` — вершинный шейдер
«только глубина», `DEPTH_ONLY` в `LightShader.vs`: позиция и UV, позиция `precise`; Masked — ещё `mainDepth` в `PBRLit.ps`) и свои вызовы с битом `csmShadowDepth` (террейн; слои расстановки с `ScatterLayers.cast_shadow` — только
в каскады, которые задевает их кольцо, `RenderView::cascadeNear/Far`; экземпляр — в каскад, полосу глубины взгляда которого
задевает его сфера, протянутая вдоль света на длину тени, `cascadeDepthFar`). Направление на источник теней — `FrameContext::toShadowLight`: солнце, после заката — луна (`DMLightDriver::shadowLight`: светило над горизонтом с более ярким светом у земли). Приём — `Shaders/shadows.sh`
(каскад по глубине взгляда, смещения к солнцу и по нормали в текселях каскада, PCF 5 × 5 Castaño, смешение каскадов,
«Show cascades»): карта t104, сэмплер сравнения s8, константы b3. Дальше дистанции — дальние тени (как Far Shadow Cascades
в UE; `far_shadows` у солнца): лес — дальним каскадом на всю сцену (срез карты после четырёх, только импостеры), рельеф —
картой края тени по карте высот (`TerrainSunShadow`, `Shaders/terrain_sun_shadow.cs`, ресурс сцены t125: точка ниже края —
в тени, так её получают и кроны, и туман; считается заново при сдвиге солнца или новом рельефе; луч идёт и дальше
края карты по горам вокруг неё — их тень на карте отдельной картой, t129, — а на всё кольцо гор своя карта края тени, t130;
гор нет в каскадах — их тень `mountainShadow` берут и вблизи); в полосе конца дистанции тень каскадов переходит в дальнюю. Настройки — у солнца (`DMLight::ShadowSettings`,
колонки `LevelLights`, подокно солнца в «Lights»), в окне «Shadows» — только «Show cascades», подробно — `docs/shadows.md`.

**Система свойств** (`src/Common/Properties`). `Property` хранит значение в `std::variant`
(bool, float, XMFLOAT2/3/4, int32, uint32) плюс границы и `GUIControlType`. `PropertyContainer` —
именованная карта свойств с вложенными контейнерами. Объекты, которые нужно крутить в рантайме
(террейн, модели, частицы), отдают `properties()`, а `Engine` регистрирует их через
`m_GUI.addPropertyWatching( контейнер, раздел Outliner, объект )`. После этого панель Details (`GUI/PropertyWidgets.cpp`)
сама строит контролы по типу свойства; необязательно — `setUnit`, `setTooltip`, `setFormat`, `setLogarithmic`. Правили ли
окно — номер правки (`Property::revision`, растёт при смене значения любым путём; `PropertyContainer::revision` — с вложенными
окнами): объект хранит номер, по которому применил настройки, и сравнивает одно число, а не настройки целиком. Настройки
строки уровня — описанием полей (`SettingsSchema`, `Properties/SettingsSchema.h`, как UPROPERTY в UE): колонка, имя
свойства, член структуры, границы, единицы, подсказка — одной строкой в `Scene/Level/LevelSettings.cpp`; окно
(`addSettingProperties` / `readSettingProperties`) и база (`readSettings` / `updateSettings`, `ObjectLibrary/SettingsDatabase.h`)
— по нему, как и проверки «сменилось ли» и ключи кэшей (`settingsEqual`, `forEachSettingValue`); новый параметр — строка
описания, а не пять мест (переведены все строки настроек уровня; что осталось вручную — `docs/gui.md`).

**Террейн** — данные и отрисовка отдельно. Данные — конвейер рельефа и воды `TerrainPipeline` (`Scene/Terrain/TerrainPipeline.h`:
исходная карта → эрозия → правки → русла → слои и маски → материал слоёв → поле ветра, кэши по отпечаткам, итоговая карта
на CPU и GPU, детальные плитки, данные мазков кисти — сама кисть `TerrainPaintTool`; реализует `TerrainHeightSource` — к нему обращаются расстановка, вода, частицы,
ходьба, тень рельефа; правки окон перестраивают ступени без перезагрузки — `rebuildMasks`, `rebuildChannels`,
`rebuildWindField`). Отрисовка — `CDLODTerrain` (`Scene/Terrain/`, Strugar 2009): квадродерево над картой высот конвейера,
корень покрывает весь террейн, лист — 32 текселя, диапазон каждого уровня вдвое больше предыдущего. Узлы выбираются
на CPU на каждый вид (`collectMeshes` → `CDLODQuadtree::select`: общее с будущими дальними горами — область с началом,
коробки узлов даёт владелец) по расстоянию до AABB и frustum. При загрузке конвейер копирует карту высот с GPU
(`GpuImages::captureTexture`) и строит из итоговой карты текстуру R32_FLOAT с мипами для вершинного шейдера; min/max высот
узлов `CDLODTerrain` считает по тем же мипам на CPU (`buildNodeBounds`, заново — по новому `heightRevision`). Рисуются одним
`DrawIndexedInstanced` патча 16×16 (`GridMesh`). Вершина уровня L читает мип L; к концу диапазона уровня морфинг
в `Shaders/cdlod.vs` сдвигает её на сетку уровня L + 1 и переводит высоту в мип L + 1, поэтому уровни стыкуются без
скачков и трещин. Условие отсутствия трещин — диапазон уровня не меньше диагонали его узла / `morphStartRatio`,
`CDLODQuadtree::computeLod` не даёт «LOD distance» опуститься ниже. Настройки — строка таблицы `Terrain`, на которую ссылается уровень
(`heightmap` — имя текстуры в таблице `Textures`, `splatmap` — файл, `height_multiplier`, `height_offset`,
`width_multiplier`). Свойство «Show LOD» раскрашивает уровни (`Shaders/cdlod_lod.ps`), «Show water» подсвечивает воду
симуляции (`docs/water.md`). Общее для шейдеров террейна
(константы `CDLODBuffer` — `Shaders/cdlod_constants.h`, выборка карты высот, выход VS) — `Shaders/cdlod.sh`.

**Материал террейна** — `TerrainMaterial` (`Terrain/TerrainMaterial.h`) + `Shaders/terrain.ps`. До двенадцати слоёв из
таблицы `TerrainLayers` (`layer` 0…11 — канал `layer % 4` среза `layer / 4` splat-карты: она — массив из трёх RGBA весов и
среза мокрости, как weightmap в UE Landscape; число — `TERRAIN_MAX_LAYERS` в `Shaders/terrain_layer_constants.h`; `albedo` и `normal` — файлы, `tiling` — метров на повтор, `albedo_tint` — множитель альбедо,
как Layer Tint в UE, подокно «Layer tint»; сейчас восемь: две травы вперемешку
пятнами по шуму, осыпи, скала, снег, галька русел — роль `channel_bed`, ею красят русла конвейера и правки рельефа, — и
наносы на дне: дресва и ил нижних озёр по глубине, дресва медленных ручьёв — роли `lake_gravel`, `lake_silt`, поля правил
«Water beds», `docs/terrain.md`, «Наносы на дне»).
У русел и мазков кисти поверх splat-карты — детальный слой весов 0,25 м (`TerrainDetailSplat`: плитки детальной земли,
галька по врезу и дну, срез мокрости, мазки последними; t5 / t6 у `terrain.ps`, `docs/terrain.md`, «Детальный слой весов»); урез воды, галька дна и мокрая
земля — по точной земле (плитка или карта высот), а не по нарисованному рельефу. Шейдер читает текстуры только
четырёх самых весомых слоёв пикселя.
Текстуры слоёв собираются при загрузке в два `Texture2DArray`: «альбедо RGB + высота A» и «нормаль RG (соглашение
DirectX) + затенение окружением B + шероховатость A» (AO слоя гасит свет неба в щелях и даёт микротени прямого света —
`Surface::microShadows`; сила — «Layer occlusion», `TerrainLayers.occlusion`, и «Micro shadows», `docs/terrain.md`,
«Затенение окружением слоёв»); все слои приводятся к размеру первого, альбедо — `R8G8B8A8_UNORM_SRGB` (фото в sRGB,
высота в альфе линейная), нормаль — UNORM; вместо ненайденного файла подставляется шахматка или плоская нормаль. Мипы строятся без WIC (`TEX_FILTER_FORCE_NON_WIC`): WIC масштабирует
с премультипликацией альфы, а в альфе здесь данные. Веса splat-карты дают правила по итоговому рельефу — последняя ступень
конвейера рельефа и воды после русел (`TerrainMasks`, `Scene/Terrain/TerrainMasks.h`: скала, снег, осыпи, две травы по
уклону, высоте, влажности TWI и картам эрозии; там же маски расстановки и леса — текстуры хранилища по имени,
`DMTextureStorage::setGenerated`; открытость ветру — от направления ветра уровня, `mask_wind_exposure`; поля правил — строка `TerrainLayerRules`, описание полей `terrainLayerRulesSchema`, и
подокно «Layer rules» с пересчётом вживую; какой слой красит правило — роль `TerrainLayers.role`; кэш результата —
`generated\key.txt` по отпечатку входов; при перестройке русел вживую — заново; подробно —
`docs/terrain.md`, «Слои и маски по рельефу»); поверх правил — мазки кисти (`TerrainPaint`: поправка ± к слою земли или мокрости — сетка 0,25 м, к маске — 1 м,
плитки в таблице `TerrainPaint`, кисть — View → «Terrain paint» и подокно «Paint», команды `terrain paint` и `brush`,
«Ручная правка слоёв и масок»); покраска правками рельефа —
поверх (`TerrainMaterial`).
В шейдере нормаль рельефа считается по карте высот (мип под размер пикселя), текстура слоя проецируется сверху,
а на крутых склонах ещё и вдоль X и Z (triplanar, нормали по UDN), вдали — ещё и в крупном масштабе (distance
resampling, как в UE Landscape: мелкий повтор не складывается в сетку), слои смешиваются по высоте. Настраиваются
в GUI: «Triplanar sharpness», «Height blend», «Far texture scale», «Far blend» (полоса «начало — конец» парой полей). Подробно — `docs/terrain.md`.

Эрозия — первая ступень конвейера рельефа и воды (`Scene/Terrain/TerrainErosion.h`, `Shaders/terrain_erosion.cs`, как
Erosion в World Machine и Gaea): капли (Beyer 2015, целочисленные атомики — результат одинаков от запуска к запуску) и
осыпание на GPU по исходной карте; параметры — строка `TerrainErosion` (`Terrain.erosion`, NULL — карта уже готова;
подокно «Erosion»); кэш и выгрузка — `eroded\` рядом с картой высот по отпечатку (`key.txt`); шейдеры — одинаковые в
Debug и Release, строгий IEEE (`ShaderCompiler::compile( …, reproducible )`), поэтому кэш общий; сменился рельеф —
модели уровня пересаживаются (`ModelInstances::reseat`, `eroded\previous.dds` до «Save level»). Подробно —
`docs/terrain.md`, «Эрозия».

Правка рельефа — слои правок поверх карты высот (как Edit Layers и Landscape Splines в UE): строки `TerrainEdits` и
`TerrainEditPoints` (кривая Catmull-Rom через точки — русло, насыпь; одна точка — площадка; ширина, полоса перехода,
`raise_terrain` / `lower_terrain`, высоты абсолютные или от рельефа), загрузчик — в `LevelDescription::terrainEdits`,
наложение — `Scene/Terrain/TerrainEdits` на копию карты в `TerrainPipeline::buildHeights` до мипов: итоговый рельеф
видят вершины, узлы, расстановка и частицы (`TerrainHeight::heightMap` — вид итоговой карты). Та же полоса правки убирает
растительность (`clear_foliage`: маска `TerrainHeight::foliageClearMask`, её читает `scatter.cs` у всех слоёв, и у леса)
и красит слой splat-карты (`paint_layer`, как Paint Layer у Landscape Splines: `TerrainMaterial` смешивает до мипов;
русло `test_channel` — галька `dry_river_pebbles`, слой 5). Правки в базе —
`python Tools/terrain_edit.py add|set|list|enable|disable|delete`; растровая правка (`TerrainEdits.raster`: карта сдвига
высоты `R32_FLOAT`, `--raster`) — вместо кривой, так хранятся русла; подробно — `docs/terrain.md`.

Детальная земля у русел — плитки высот с шагом 0,25 м там, где русло режет рельеф (`TerrainHydrology::DetailTile`: рельеф
Catmull-Rom минус профиль русла по сплайну оси), массив и индекс — ресурсы сцены t113 / t114 (`Shaders/terrain_detail.sh`);
квадродерево у камеры делится ещё на шесть уровней мельче листа (−1…−6: до 1,6 см, «Finest mesh»; при ходьбе LOD — от
отрезка «глаза — ноги», самая мелкая сетка вокруг ног, «Finest mesh radius») везде, высота их вершин — плитка или
карта (`sampleFineHeight`); её же читают нормаль, расстановка и частицы (`terrainHeight`) и ходьба (`surfaceHeight`); подробно —
`docs/terrain.md`, «Детальная земля у русел». У камеры вершины ещё сдвигаются по высоте слоёв — дисплейсмент
(`terrainDisplacement`, `Shaders/terrain_layers.sh`; размах слоя — `TerrainLayers.displacement`, дальность — «Displacement
distance»; текстуры в пикселе — по точке без смещения); данные слоёв — ресурсы сцены t118…t123 (splat-карта, альбедо с
высотой, детальный слой весов, константы слоёв `TerrainLayerConstants` (`Shaders/terrain_layer_constants.h`), у шейдеров террейна — копия в cbuffer, шум
пятен яркости), по
ним на смещённой земле стоят кольцевые слои расстановки и частицы (`terrainSurfacePoint`, `terrain_height.sh`; постоянные
слои — без смещения), `docs/terrain.md`, «Дисплейсмент».

Почему CDLOD, а не тесселяция или geometry clipmaps: аппаратная тесселяция как основа
LOD из практики ушла (UE5 её удалил, Far Cry 5 отказался из-за стоимости); современные движки рисуют сетку по карте
высот с морфингом между уровнями. Прежние террейны (GeoClipMap и тесселяционный) удалены, они есть в истории git.

**Расстановка (трава, цветы, камешки, веточки)** — `Scatterer` (`Scene/Scatterer/`), по объекту сцены на набор
(`ScatterSets`: имя; проход и отсечение граней слоя задаёт режим его материала). Слой набора
(`ScatterLayers`, свой `ScatterPass`: пул инстансов, списки индексов «вариант × LOD» на каждый вид кадра — камера и каскады
теней, `FrameContext::views`, — и один `ExecuteIndirect` со счётчиком на группу секций с одним материалом, команды пишет
compute) — растение: одна или несколько моделей со всеми LOD и весами (`ScatterLayerModels`, как Mesh Entries у Static Mesh
Spawner в PCG UE: модель ячейки — по весам и случайному числу ячейки, с куртинами — веса × шум мира своей группы, виды
растут пятнами (`patch_scale`, `patch_strength`, `ScatterLayerModels.patch_group`), вес вида — и по слоям земли под ним
(`ground_layers`, `ground_preference`: сухая трава — на сухом лугу); у модели свои размер, разброс и
наклон по земле — `ScatterLayerModels.size_multiplier`, `size_variation`, `align_to_terrain`, NULL — как у слоя; фильтр по
слоям земли — `ScatterLayers.layer_filter` (роли `TerrainLayers.role`, как Landscape Grass Type в UE: трава — только на
травяных слоях, мазок снега по лугу её убирает); LOD экземпляра — по расстоянию и дальностям LOD
модели `ModelProperties.range`, как у моделей уровня, но ближе на случайную долю у каждого экземпляра; меняется только
меш, экземпляр на месте; в полосе перехода экземпляр — в списках перехода обоих LOD с дизерингом), маска плотности, шаг сетки `cell_size`, кольцо `near_border…far_border` вокруг камеры с плавным исчезанием (`*_fade`),
размер, `jitter`, предел случайного поворота по осям `rotation_x/y/z` (градусы), цвет земли у растения (`ground_color`,
`ground_color_height`, `ground_color_far`, `color_variation`: средний цвет слоёв террейна под экземпляром с пятнами
яркости земли — у корня и вдали, `terrainGroundColor`, `Shaders/scatter_ground.sh`, `docs/scatter.md`, «Цвет земли у
растений»), рваный край кольца и прореживание вдали (`edge_spread`, `thinning_start`, `thinning`: своя дальность
исчезания экземпляра, вдали меньше экземпляров — `ringFade`, `thinOut`; `dither_fade` — исчезание долей пикселей, а не
размером, `edgeFade`), `align_to_terrain` и `cast_shadow` (тень
солнца; флажок «Cast shadow» в окне набора — `Scatterer::properties`; у варианта ещё свой `ScatterLayerModels.cast_shadow`). Трава, ромашки, камешки — слои. Каждый кадр `Shaders/scatter.cs`
раскладывает инстансы слоя по сетке, привязанной к миру (смещение, размер и поворот — хеш координат ячейки, поэтому
при движении камеры они на месте), маска даёт вероятность и размер, отсечение — по плоскостям frustum каждого вида
(`DMFrustum::planes`; у каскадов ещё полоса расстояний и тень в кадр), поворот — кватернион (`Shaders/instance.sh`,
`INST_ROTATE`; инстанс вершинный шейдер берёт из пула по списку вида и root-константе b9 — `instanceSlot`). Высоту и UV масок даёт
`TerrainHeightSource` (`Scene/Terrain/TerrainHeightSource.h`, реализует `TerrainPipeline`; шейдерная часть —
`Shaders/terrain_height.sh`), так что расстановка не зависит от устройства террейна. Как в Witcher 3 и UE5: у каждого
типа своя сетка, дальность и плавное исчезание; крупные камни, о которые спотыкаются, — модели уровня, а не расстановка.
Маски в координатах карты высот; их считает движок по рельефу (`TerrainMasks`, текстуры хранилища по имени). Лес — постоянный слой
(`ScatterLayers.persistent`): раскладывается один раз на всю карту кластерами по 8 × 8 ячеек (`placeWorld`), каждый
кадр отбираются видимые кластеры и их экземпляры в те же списки видов (`cullPlaced`); камни русел — такой же постоянный
слой, но экземпляры кладёт CPU по камням конвейера (`ScatterLayers.placement` = `stream_stones`,
`ScatterPass::placeInstances`); отсечение экземпляров — по сфере
модели варианта (`ScatterPass::Variant::bounds`). Модели слоёв могут быть и моделями уровня: материал собирает вариант
для экземпляров расстановки (`Material::enablePlacedInstances`, `ShaderPhaseOptions::placed`). Дальше
`ScatterLayers.impostor_distance` — импостер (`ImpostorMaterial`: последний LOD варианта, карточка с тремя из 8 × 8 кадров
полуоктаэдра; кадры запекаются при загрузке из LOD0 модели — `SceneObject::bake` → `Renderer::bake` после `Scene::initialize`,
до прогрева пайплайнов, вариантом `mainBake` материала секции: цвет, нормаль, глубина поверхности; сторона кадра — по слою
(`ScatterLayers.impostor_frame_size`, у травы 64, по умолчанию 128); крона затеняет себя по своим кадрам со стороны солнца
(`impostor_self_shadow`, `Surface::selfShadow` — минимум с каскадами, дальше 200 м — одна); после правки материала растения или настроек запекания
в окне слоя — заново, `Scatterer::rebakeChangedImpostors` из `Scene::applyEditorChanges`; кэш —
`cache\impostors\<debug|release>\<модель>\` по ключу из мешей, материалов, текстур, настроек и шейдеров); глубина пикселя —
запечённой поверхности (смещение глубины): prepass и тени пишут её, проход цвета — `opaqueDepthRead` (видимость по
глубине сцены); в каскадах теней импостер — уже дальше `ScatterLayers.shadow_impostor_distance` (у леса 65 м). Подробно —
`docs/scatter.md`.

**Подсистемы сцены** (`src/Engine/Graphics/Scene/`): `Terrain` (`TerrainPipeline` — данные, `CDLODTerrain` — отрисовка), `Scatterer` (расстановка, см. выше),
`Water` (`WaterSimulation`),
`Particle` (`ParticleSystem`), `Sky` (`SkySphere`, `SkyAtmosphere`, `VolumetricCloud`), `Light` (`DMLightDriver`, свет в structured buffer), `Camera`,
`Texture` (`DMTexture` — текстура GPU и вид, `DMTextureStorage`, чтение файлов — `ImageFile::load`), `Model`/`Mesh`
(`ModelInstances`; общие вершинный и индексный буферы в `VertexPool`), `Materials` (`Material` и его классы,
`MaterialStorage`). Не объекты сцены, а общее для проходов — в `src/Engine/Graphics/`:
`ShaderProgram`, `FullscreenShader`, `ComputeShader`, `ConstantBuffers`, `VolumetricFog`.
