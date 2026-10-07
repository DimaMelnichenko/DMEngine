# Проходы кадра и depth prepass

Как `Renderer` рисует кадр сцены: какие проходы и в каком порядке, как меши и свои вызовы объектов попадают в
проходы и что делает depth prepass — глубина всех непрозрачных до прохода цвета, как EarlyZPass в UE.

## Порядок кадра

1. `compute()` всех объектов (расстановка, частицы, небо, воздушная перспектива).
2. Сбор с главного вида: каждый видимый объект отдаёт в `MeshCollector` меши (`MeshBatch`) и свои вызовы
   (`CustomBatch` с маской проходов), `Renderer::buildCommands` раскладывает их по проходам с ключом сортировки.
3. Глубина четырёх каскадов теней — у каждого каскада свой сбор ([shadows.md](shadows.md)).
4. **`depthPrepass`** — только глубина непрозрачных и Masked, без цели цвета.
5. **`opaque`** — непрозрачные и Masked с освещением. После prepass глубина проверяется на равенство без записи.
6. **`opaqueDepthRead`** (только после prepass и если есть кому) — непрозрачные, которым нужна глубина сцены в шейдере
   (`Material::readsSceneDepth`; сейчас импостеры, [scatter.md](scatter.md)): глубина в проходе только для чтения и
   видна шейдерам (`SLOT_SCENE_DEPTH`, t107), проверка «ближе или равно» без записи. Без prepass такие материалы рисует
   `opaque` и пишут глубину сами.
7. **`sky`** — фон на дальней плоскости (глубина 0), только там, где сцена ничего не нарисовала ([sky.md](sky.md)).
8. **`Scene color copy`** (только если в `transparent` кто-то читает цвет сцены) — копия HDR-буфера для шейдеров, см.
   «Цвет сцены для шейдеров».
9. **`transparent`** — полупрозрачные от дальних к ближним, глубина только читается.
10. Постобработка в задний буфер ([postprocess.md](postprocess.md)), затем GUI.

Задний буфер — swap chain flip model (`DMD3D::createDeviceSwapChain`, как требует D3D12): два буфера `R8G8B8A8_UNORM`
с sRGB-видом, без vsync — с разрывом кадра (tearing), кадр начинается по waitable object (`DMD3D::waitForNextFrame`).
При `WM_SIZE` `DMGraphics::resize` пересоздаёт задний буфер, HDR-буфер сцены, глубину, уровни bloom и проекцию камеры
по правилу D3D12: дождаться GPU, отпустить ссылки на задние буферы, `ResizeBuffers`, цели заново (`DMD3D::resize`).

Проход объекта выбирает рендерер, а не объект: меши — по режиму материала (`passFor`: Translucent — в
`transparent`, остальные — в `opaque` и prepass, читающие глубину сцены после prepass — в `opaqueDepthRead`), свои
вызовы — по битам маски (`passBit( MeshPass::… )`). Глубина обратная (Reversed-Z): 1 у ближней плоскости, 0 у дальней,
«ближе» — `GREATER`.

## Глубина сцены для шейдеров

Как SceneDepthTexture в UE. Буфер глубины сцены (`SceneTargets`) — текстура `R32_TYPELESS`: цель `D32_FLOAT`
(`depthTarget`), цель только для чтения (`depthReadTarget`, DSV с `READ_ONLY_DEPTH`) и вид `R32_FLOAT` (`depthView`).
Проход, которому глубина нужна в шейдере, объявляет цель только для чтения и `depthView` в `reads`: слой ставит layout
`DIRECT_QUEUE_GENERIC_READ` — проверка глубины и чтение шейдерами одной текстуры ([d3d12.md](d3d12.md), §3.7). Рендерер
так рисует `opaqueDepthRead` и привязывает `depthView` в слот сцены `SLOT_SCENE_DEPTH` (`Shaders/slots.h`; в шейдере —
`DM_SRV( Texture2D<float>, g_sceneDepth, SLOT_SCENE_DEPTH )`, выборка `g_sceneDepth[uint2( position.xy )]`). Экранным
эффектам между prepass и проходом цвета (SSAO, контактные тени) достаточно обычного чтения: `depthView` в `reads`
compute-прохода. Цели только для чтения очистка не нужна (`clearDepth` её пропускает).

## Цвет сцены для шейдеров

Как SceneColor в UE (преломление, Single Layer Water): полупрозрачному шейдеру нужен цвет того, что за ним, а рисует он
в тот же HDR-буфер, из которого читать нельзя. Поэтому перед проходом `transparent` рендерер копирует буфер целиком
(`SceneTargets::copyColor` → `DMD3D::copyTexture`, `CopyResource`) — то, что нарисовали непрозрачные и небо, без других
полупрозрачных (как в UE). Копия — `R16G16B16A16_FLOAT` в тех же единицах, что буфер сцены: яркость × pre-exposure
(`preExposure()` из `Shaders/exposure.sh`), а не «цвет на экране».

Копия делается, только если её кто-то читает: меш с материалом `Material::readsSceneColor()` или свой вызов
`MeshCollector::addCustom( маска, расстояние, true )` (`CustomBatch::readsSceneColor`) в проходе `transparent`;
`Renderer::buildCommands` ставит `m_sceneColorRead`. Текстура копии создаётся при первой копии (16 МБ при 1920 × 1080)
и заново при смене размера окна. Тогда проход `transparent`:
- объявляет копию в `reads` и привязывает её в слот сцены `SLOT_SCENE_COLOR` (t108; в шейдере —
  `DM_SRV( Texture2D<float4>, g_sceneColor, SLOT_SCENE_COLOR )`, выборка `g_sceneColor[uint2( position.xy )]` или со
  смещением преломления — `SampleLevel` с `g_SamplerLinearClamp`);
- рисует с глубиной только для чтения (`depthReadTarget`) и привязывает её в `SLOT_SCENE_DEPTH`, как `opaqueDepthRead`:
  воде нужны оба — цвет дна и толщина слоя до него.

Без читающих копии нет, и проход `transparent` такой же, как прежде. Если в нём нужна только глубина (мягкие частицы,
`CustomBatch::readsSceneDepth`, [particles.md](particles.md)), — глубина только для чтения в `SLOT_SCENE_DEPTH` без копии цвета. Читает её поверхность воды
([water.md](water.md), «Поверхность»). Копия 1920 × 1080 — 0,028 мс GPU (Release,
RTX 4070 Ti, строка `Scene color copy` в «GPU average»). В списке проходов (`passes`) — строка `Scene color copy: copy`.

## Depth prepass

Без него непрозрачные рисуются с записью глубины в порядке объектов сцены (небо, террейн, модели, расстановка), и
пиксельный шейдер освещает всё, что проходит проверку глубины в момент рисования: террейн под травой и за моделями,
дальние склоны за ближними, камешки под травой. Потом эти пиксели закрывает то, что нарисовано позже.

С prepass (как в UE при полном EarlyZPass — `DDM_AllOpaque`):
- **prepass** рисует глубину всех непрозрачных и Masked вариантом материала «только глубина» (`depthPhaseFor`):
  у `PBR` — вершинный шейдер `DEPTH_ONLY` (позиция и UV), непрозрачные без пиксельного шейдера, Masked — с `mainDepth`
  (только `clip` по альфе) ([materials.md](materials.md)). Цель — только буфер глубины сцены
  (объявление прохода без целей цвета в `Renderer::executePass`);
- **проход цвета** рисует те же меши с `DepthState::readOnlyEqual` — проверка `EQUAL` без записи (CF_Equal
  базового прохода UE): пиксель проходит, только если его глубина совпадает с записанной, то есть это ближайшая
  поверхность. Каждый видимый пиксель освещается один раз. Позиция в обоих вариантах вершинного шейдера считается
  одной функцией и помечена `precise`, поэтому глубины совпадают до бита и `EQUAL` не мерцает;
- **Masked в проходе цвета — без `clip`** (как r.EarlyZPassOnlyMaterialMasking в UE): вырезанные по альфе пиксели уже
  не попали в глубину prepass, `EQUAL` их отбрасывает, а шейдер без `clip` сохраняет раннюю проверку глубины.
  Так же и смена LOD дизерингом ([materials.md](materials.md)): в prepass экземпляр в полосе перехода рисует свою долю
  пикселей (`mainDepth` с `LOD_DITHER`), в проходе цвета — без `clip`. Вариант выбирает `phaseFor( params, options )`
  по `ShaderPhaseOptions::depthFromPrepass`.

Меш, у материала которого нет варианта глубины (`depthPhaseFor` = −1: классы `Color`, `Texture`, — например Box
уровня `Test`), и свой вызов без бита `depthPrepass` в prepass не рисуются. В проходе цвета они проверяют и пишут
глубину сами (`DepthState::enabled`), поэтому закрывают и закрываются правильно, только без выигрыша от prepass.

Каркас (Q и «Wireframe» террейна) рисуется каркасом и в prepass: сплошная глубина закрыла бы то, что видно сквозь
каркас. Где сходится много линий одной глубины (полюса шаров), с prepass сверху может оказаться другая из них.

## Переключатель

- `DepthPrepass` в секции `[General]` `settings.ini` (`true` / `false`; нет строки — включён), как r.EarlyZPass в UE;
- флажок «Depth prepass» в окне GUI «Renderer» — переключение на лету, чтобы сравнить время в «Statistic».

Время prepass — строка `Pass depth prepass` в «Statistic» и «GPU average» одной областью, как тени; строки объектов
(`CDLOD terrain`, `Meadow` …) — их время в проходе цвета.

## Свой объект в prepass

- **Меши** (`MeshBatch`) попадают в prepass сами, если у материала есть `depthPhaseFor` и он не Translucent.
- **Свой вызов:** добавьте `passBit( MeshPass::depthPrepass )` в маску `addCustom` и в `renderCustom` при
  `isDepthOnlyPass( context.pass )` рисуйте только глубину: вариант материала «только глубина», без ресурсов
  пиксельного шейдера. Вершинный шейдер должен давать ту же глубину, что в проходе цвета: тот же шейдер (как у
  террейна) или вариант с `precise`-позицией, посчитанной тем же кодом. Если часть объекта в prepass не рисуется
  (у расстановки — слой без варианта глубины), в проходе цвета рисуйте её с `DepthState::enabled`
  (`Scatterer::renderCustom`).
- В проходе цвета `context.depthFromPrepass` говорит, что глубина уже есть: Masked берёт вариант без `clip`.

## Если что-то не так

1. **Чёрные точки или дыры на объекте** — глубина prepass не совпала с глубиной прохода цвета, `EQUAL` отбросил
   пиксель. Проверьте, что вершинный шейдер варианта «только глубина» считает позицию тем же кодом и с `precise`, и что
   в обоих проходах один и тот же выбор геометрии (LOD, инстансы).
2. **Объект пропал целиком** — он есть в проходе цвета, но не в prepass (нет бита или варианта глубины), а запасной
   путь с `DepthState::enabled` не сработал.
3. **Лепестки Masked стали сплошными** — в prepass рисуется вариант без `clip` (не `mainDepth`).
4. Сравнить с кадром без prepass — флажок «Depth prepass» или `DepthPrepass=false`.

## Производительность

Release, 1920 × 1080, время GPU, мс (частицы в уровнях выключены):

| Камера | Кадр без prepass | Кадр с prepass | Prepass | Проход цвета | Террейн в цвете | Трава в цвете |
|---|---|---|---|---|---|---|
| перед шарами (`512,96,212,12,0`) | 1,53 | 1,41 | 0,08 | 0,71 → 0,52 | 0,62 → 0,43 | — |
| над травой (`567,58.5,212,35,35`) | 3,28 | 3,11 | 0,36 | 1,60 → 1,10 | 0,53 → 0,35 | 0,93 → 0,74 |
| у ромашек (`567,59.0,205.8,14,180`) | 3,60 | 2,87 | 0,41 | 2,00 → 0,94 | 0,88 → 0,25 | 0,96 → 0,69 |
| у земли (`560,57,215,5,40`) | 3,37 | 3,05 | 0,48 | 1,57 → 0,93 | 0,38 → 0,18 | 1,07 → 0,74 |

Выигрывает в основном террейн: его тяжёлый шейдер больше не освещает закрытое травой, моделями и ближними
склонами; камешки под травой почти перестали стоить (0,13–0,16 → 0,01 мс). Трава сама дороже на ~0,15 мс: в prepass
её мелкие треугольники растеризуются второй раз, а упирается она именно в растеризацию. Без расстановки в prepass
кадр в траве медленнее (3,24, 3,06 и 3,38 мс вместо 3,11, 2,87 и 3,05): террейн под травой снова освещается целиком.
Время объектов на границах неточно (метки времени не ждут окончания работы), достоверен итог кадра и проходов.

## Ограничения

- Prepass только у главного вида; виды теней рисуют одну глубину и так.
- Полупрозрачные в prepass не рисуются и освещают всё, что проходит проверку глубины.

## Файлы

| Файл | Что там |
|---|---|
| `src/Engine/Graphics/Renderer.cpp` | порядок проходов (`render`), раскладка (`buildCommands`), запасной путь, `drawMesh` |
| `src/Engine/Graphics/Scene/MeshBatch.h` | `MeshPass::depthPrepass`, `opaqueDepthRead`, `isDepthOnlyPass`, `passFor` |
| `src/Engine/Graphics/SceneTargets.h/.cpp` | буфер сцены: глубина с видом для шейдеров и целью только для чтения |
| `src/Engine/Graphics/Scene/SceneObject.h` | `RenderContext::depthFromPrepass` |
| `src/Engine/Graphics/D3D/DMD3DPipelines.cpp`, `DMD3DPasses.cpp` | `DepthState::readOnlyEqual`, `beginPass` — цели прохода по `PassDesc` (`D3D/GpuPass.h`) |
| `src/Engine/Graphics/Scene/Materials/PBRMaterial.cpp` | `phaseFor( params, options )`, фазы глубины |
| `src/Engine/Graphics/Scene/Terrain/CDLODTerrain.cpp`, `Scatterer/Scatterer.cpp` | свои вызовы в prepass |
| `Shaders/LightShader.vs`, `Shaders/depth_only.sh` | вариант «только глубина» с `precise`-позицией |
| `settings.ini`, `src/Config/Config.cpp` | `DepthPrepass` |

## Откуда подход

- Unreal Engine: EarlyZPass (`r.EarlyZPass`, полный prepass `DDM_AllOpaque`), базовый проход с `CF_Equal` без записи,
  `r.EarlyZPassOnlyMaterialMasking`, FDepthOnlyVS, `INVARIANT_OUTPUT` у позиции.
- DOOM (2016, id Tech 6): depth prepass перед кластерным forward-освещением — та же основа, что нужна Forward+ здесь.
