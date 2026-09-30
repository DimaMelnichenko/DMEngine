# Переезд DMEngine на D3D12: что менять и в каком порядке

План переезда движка с D3D11 на D3D12 без деградации: сначала подготовка прямо на D3D11, затем бэкенд D3D12 рядом
с D3D11 под общим слоем, и только потом то, ради чего всё затевалось. Составлен 2026-09-30 по коду ветки `v2`
(коммит `1747309`, «Ветер дерева по схеме SpeedTree») и по разбору кадра The Witcher 3 (проект `X:\Witcher_research`:
`D3D12_MIGRATION.md` — общие соображения, `REPORT.md` §4 — как W3 устроил D3D12). Это документ-план: пункты по мере
выполнения уходят в `TODO.md` → «Сделано», а этот файл становится описанием слоя RHI.

## 1. Зачем это именно DMEngine

D3D12 сам по себе кадр не ускоряет: шейдеры, растеризация и полоса памяти те же, а порт «в лоб» часто медленнее D3D11.
Кадр движка — 1,4–4,8 мс GPU в 1920 × 1080 (`docs/passes.md`), CPU на сбор мешей и раскладку — доли миллисекунды,
поэтому многопоточная запись команд и дешёвые вызовы, главные CPU-плюсы D3D12, в FPS сейчас не превратятся.

Выигрыш — в возможностях, которых в D3D11 нет, и они нужны ближайшим этапам `TODO.md`:

| Этап | Что нужно | Что даёт D3D12 |
|---|---|---|
| 6, резервы | Тени 0,4–2,0 мс упираются в растеризацию, compute расстановки, таблицы неба и гистограмма экспозиции идут в той же очереди | **Async compute**: compute-проходы (`Scatterer::compute`, `SkyAtmosphere::compute`, экспозиция) на второй очереди поверх каскадов теней и prepass |
| 6, трава | 2,7 млн треугольников, растеризуются дважды (prepass + цвет), упирается в растеризацию | **Mesh-шейдеры** (SM 6.5): amplification-шейдер отсекает тайлы и выбирает LOD, mesh-шейдер генерирует травинки — без буфера инстансов и без вызова на каждый LOD |
| 10, лес | Кластеры деревьев: отсечение и LOD на GPU, compute + indirect, импостеры вдали | **`ExecuteIndirect` со счётчиком**: один вызов на все кластеры, LOD и материалы, список строит GPU; **bindless** — материалы кластеров в одном вызове |
| 14, деревня | Много источников света (Forward+) | Отсечение света по плиткам в compute на второй очереди, глубина как SRV |
| 15, масштаб | Выбор узлов CDLOD на GPU | Тот же `ExecuteIndirect` со счётчиком |

Сегодня расстановка уже упирается в модель D3D11: у слоя до 64 списков («вариант × LOD», обычные и перехода) × 4
секции — **до 256 indirect-вызовов на слой на вид**, видов в кадре пять, а `copySectionCounts` — отдельный dispatch
только потому, что в D3D11 каждый indirect-вызов читает свою запись аргументов. В D3D12 это один `ExecuteIndirect`
на слой на вид с root-константой номера секции в каждой команде.

## 2. Что в движке уже «по-D3D12»

Это почему подготовка стоит меньше, чем кажется:

- **Проходами владеет `Renderer`**, объекты себя не рисуют (`MeshBatch` / `CustomBatch`, `RenderContext`,
  `docs/passes.md`). Порядок кадра — явный список в `Renderer::render`: тени → prepass → opaque → sky → transparent
  → постобработка. Render graph здесь — не переписывание, а объявление входов и выходов у того, что уже есть.
- **Слоты зафиксированы в `Shaders/slots.h`**: b0 кадр, b1 объект, b2 материал / проход, b3 тени, b4+ буферы прохода;
  t0…t15 материал, t16 инстансы, t100…t106 ресурсы сцены; сэмплеры s0…s7 общие и s8 сравнения. Это готовая
  root signature (§4.3).
- **Состояния — перечисления**: `RasterState` (7), `DepthState` (5), `BlendState` (3), фазы шейдеров (`PBR` — 14,
  террейн — 3, остальные по одной). Набор PSO конечен и невелик — порядка сотни-двух реально используемых сочетаний.
- **Все обновления константных буферов идут через два шаблона** `Device::updateResource` / `updateResourceData`
  (`D3D/DMD3D.h`, 28 мест вызова) — одно место, где `Map(DISCARD)` меняется на кольцевой upload-буфер.
- Ничего экзотического: HS / DS нет, GS один (`particle.gs`), stream output и `DrawAuto` не используются, MSAA по
  умолчанию 1. Reversed-Z, D32 у теней, R16F у сцены — в D3D12 то же самое.
- **Шейдеры SM 5.0 через fxc годятся D3D12 как есть**: рантайм принимает DXBC. Переход на DXC и SM 6.x нужен только
  для wave-интринсиков, bindless (`ResourceDescriptorHeap`, SM 6.6) и mesh-шейдеров (SM 6.5) — то есть в фазе C, не
  раньше.
- `unbindTransientResources` и `unbindShadowMap` — уже осознанная работа с зависимостями между проходами. В D3D12
  она становится барьерами в тех же местах (после A5 — в `DMD3D::beginPass` и `makeReadable`).

## 3. Инвентарь: что придётся менять

Прямые обращения к D3D11 за пределами `src/Engine/Graphics/D3D/` на момент плана: типы `ID3D11*` / `D3D11_*` — в 47
файлах движка (плюс `3rdParty/ScreenGrab`), `DMD3D::instance()` — в 32 файлах, `GetDeviceContext()->…` — 88 вызовов,
`GetDevice()` — 36, около 50 разных методов контекста. После A2 (2026-09-30) вне `D3D/` типов D3D11 нет, `GetDevice()` —
только у привязки ImGui, `DMD3D::instance()` — в 23 файлах (создание и привязка ресурсов). Вот что за этим стояло и чем
это стало:

| Что сейчас | Где | В D3D12 | Когда |
|---|---|---|---|
| `D3D11CreateDeviceAndSwapChain`, `DXGI_SWAP_EFFECT_DISCARD`, `BufferCount = 1` | `DMD3D::createDeviceSwapChain` | Только flip model: `CreateSwapChainForHwnd`, `FLIP_DISCARD`, 2–3 буфера, `ALLOW_TEARING` без vsync | **A1** — сделано 2026-09-30 |
| `com_unique_ptr<ID3D11ShaderResourceView>&` в публичных сигнатурах | `DMD3D::setSRV`, `DMTexture::srv`, `RenderTarget`, `CubeTarget`, `ScatterPass::instances`, `DMStructuredBuffer`, `ShadowCascades`, `SkyLight`, `PostProcess::renderBloom`, `CDLODTerrain::m_heightMap`, `TerrainMaterial`, `HDRIBackdrop` | Дескриптор в куче (`D3D12_CPU_DESCRIPTOR_HANDLE` + индекс) | **A2** — сделано: `ShaderView` и др. в `D3D/GpuResources.h` |
| `GetDeviceContext()->IASet*`, `OMSetRenderTargets( 0, … )`, `*SetShaderResources`, `CSSetShader`, `Dispatch`, `ClearDepthStencilView` | `CDLODTerrain`, `DMParticleSystem`, `ScattererPass`, `SkyAtmosphere`, `SkyLight`, `PostProcess`, `ShadowCascades`, `GUI`, `DMComputeShader`, `DMShader`, `ConstantBuffers` | Методы командного списка за интерфейсом контекста | **A2** — сделано: методы `DMD3D` |
| `Map( WRITE_DISCARD )` на каждый draw: b1 в `setPerObjectBuffer`, материал в `PBRMaterial::setParams`, `DMStructuredBuffer::updateData` (инстансы, патчи террейна), параметры террейна и расстановки | 28 мест через `Device::updateResource*` | Кольцевой upload-буфер кадра, root CBV / SRV со смещением; выравнивание 256 байт | **A3** — сделано для констант (`D3D/ConstantRing.h`); участки структурных буферов — с дескрипторами B3 |
| `D3DCompileFromFile` при старте, `DMShader::Phase` (набор VS/PS/GS/HS/DS), `IASetInputLayout` + `setState` порознь; `Layout` — псевдошейдер ради input layout | `DMShader`, `Layout`, все материалы | **PSO** = фаза + `RenderState` + input layout + топология + форматы целей; кэш `ID3D12PipelineLibrary`; input layout не требует байткода VS | **A4** — сделано (`D3D/GpuPipeline.h`, прогрев 826 пайплайнов), **B4** |
| Цели и зависимости неявные: `setSceneTarget` / `setRenderTarget`, `unbindTransientResources`, `unbindShadowMap`, `OMSetRenderTargets( 0 )` в трёх местах | `Renderer::executePass`, `renderShadows`, `PostProcess::render`, `SkyAtmosphere`, `SkyLight` | Объявление прохода (цели, что читает, что пишет) → барьеры считаются из объявлений | **A5** — сделано (`D3D/GpuPass.h`, `beginPass`, команда `passes`) |
| `GenerateMips` | `SkyAtmosphere::renderSkyCube`, `HDRIBackdrop` | В D3D12 нет: compute-проход уменьшения по мипам | **A6** |
| `UpdateSubresource` для сброса indirect-аргументов | `ScatterPass::resetArgs` | `CopyBufferRegion` из буфера начальных аргументов в default-куче + барьер `INDIRECT_ARGUMENT` | **A6** |
| Чтение с GPU: `CopyResource` + `Map( READ, DO_NOT_WAIT )` по кольцу из N копий | `PostProcess::readBackExposure` | Readback-куча + значение fence у каждой копии | **A6** |
| `CaptureTexture` (DirectXTex, D3D11) при загрузке карты высот | `CDLODTerrain::initialize` | `CaptureTexture( ID3D12CommandQueue*, … )` из DirectXTex | **B2** |
| Текстуры через DirectXTex D3D11: `CreateShaderResourceView`, `CreateTexture` | `DMTextureStorage`, `CustomTexture`, `TerrainMaterial`, `CDLODTerrain`, `HDRIBackdrop` | DirectXTex D3D12 (`BUILD_DX12 ON` в CMake): `CreateTexture` + `PrepareUpload` + копирование через upload-буфер | **B2** |
| `3rdParty/ScreenGrab` (D3D11) | `DMD3D::saveScreenshot` | `ScreenGrab12` из DirectXTex | **B2** |
| `D3D11_QUERY_TIMESTAMP` + `DISJOINT`, `ID3DUserDefinedAnnotation` | `GpuProfiler` | `ID3D12QueryHeap` TIMESTAMP + `ResolveQueryData` в readback-буфер, `GetTimestampFrequency`; метки — `PIXBeginEvent` (WinPixEventRuntime) | **B6** |
| `imgui_impl_dx11` | `GUI.cpp` | `imgui_impl_dx12` (нужна куча SRV под шрифт) | **B7** |
| `ID3D11InfoQueue` → `log.txt` | `DMD3D::logDebugMessages` | `ID3D12InfoQueue1` с callback + GPU-based validation в Debug, DRED при потере устройства | **B1** |
| `ResolveSubresource` при MSAA | `DMD3D::sceneColor` | Есть в D3D12 как есть | B5 |

Уровень устройства: с A3 — 11_1 без отката (`ID3D11DeviceContext1`, константные буферы со смещением и
`Map( NO_OVERWRITE )` у них); на Windows 10 / 11 это ничего не стоит.

## 4. Целевое устройство слоя

### 4.1. Интерфейс

Один слой над API, устроенный по модели D3D12, с двумя бэкендами. Имена условные — важно разделение ролей:

- **`Device`** — создание ресурсов и видов, PSO, кучи дескрипторов, swap chain, fence. Сейчас это половина `DMD3D`
  плюс `GetDevice()` в 36 местах.
- **`CommandContext`** — то, что сейчас `GetDeviceContext()`: `beginPass( PassDesc )`, `setPipeline`,
  `setConstants( slot, data, size )` (из кольца), `setSRV( slot, view )`, `setUAV`, `setVertexBuffers` / `setIndexBuffer`,
  `draw*`, `drawIndirect`, `dispatch`, `copy`, `barrier` (в D3D11 — пусто). Один на кадр, позже — по одному на поток.
- **Ресурсы** — непрозрачные объекты `Texture`, `Buffer` и виды `ShaderView` (SRV), `TargetView` (RTV / DSV),
  `StorageView` (UAV). Объекты сцены и материалы держат только их. `com_unique_ptr<ID3D11*>` остаётся внутри бэкенда.
  Сделано в A2: `D3D/GpuResources.h` — эти классы плюс `ShaderStage`, `InputLayout` и описания `BufferDesc` /
  `BufferViewDesc` / `TextureDesc` / `TextureData` / `TextureViewDesc` / `VertexElement`; `D3D/TextureImages.h` — мост
  к DirectXTex. Пока их создаёт и привязывает `DMD3D` (роли `Device` и `CommandContext` в одном классе).
- **`FrameResources[N]`**, N = 2–3 кадра в полёте: кольцо upload-буфера (8–16 МБ), кольцо дескрипторов (десятки
  тысяч), аллокатор команд, значение fence. В D3D11 — те же кольца без fence (драйвер сам ждёт).

### 4.2. Пайплайны

`PipelineDesc { DMShader*, фаза, RenderState, топология, input layout, форматы RT / DS }` → PSO из кэша по хэшу.
`ScopedRenderState` остаётся как удобство для кода объектов, но состояние теперь не ставится в контекст, а входит в
ключ PSO при `setPipeline`. Набор нужных PSO известен заранее: рендерер знает, в каких проходах и с какими состояниями
рисуется каждый материал (prepass / opaque / shadow / transparent × twoSided × mirrored × wireframe), поэтому PSO
собираются при загрузке уровня, а собранный «лениво» PSO пишется в лог как предупреждение — иначе фризы в первом кадре.
Кэш на диске — `ID3D12PipelineLibrary`.

### 4.3. Привязка ресурсов: root signature из `slots.h`

Одна root signature для графики, одна для compute (RTX 4070 Ti — Resource Binding Tier 3, лимит 64 DWORD):

| Параметр | Что | Размер |
|---|---|---|
| root CBV b0 | кадр (352 байта) | 2 DWORD |
| root CBV b1 | объект (144 байта) | 2 DWORD |
| root CBV b2 | материал / проход | 2 DWORD |
| root CBV b3 | тени | 2 DWORD |
| таблица T0 | t0…t16 (ресурсы материала и прохода, инстансы) + b4…b7 как CBV в таблице | 1 DWORD |
| таблица T1 | t100…t106 — ресурсы сцены, ставится раз за кадр | 1 DWORD |
| статические сэмплеры | s0…s7 и s8 (сравнение `GREATER_EQUAL`) | 0 |

Compute: root CBV b0, b2, b4, b7; таблица SRV t0…t15; таблица UAV u0…u7.

На каждый draw копируется до 17 дескрипторов в кольцо shader-visible кучи — та же схема, что у W3 (у него кольцо на
~750 тыс. дескрипторов); при сотнях draw в кадре это незаметно. Bindless (§5, C3) убирает и это.

### 4.4. Проходы и барьеры

Сделано в A5 (`D3D/GpuPass.h`). `PassDesc { имя, цели цвета, цель глубины, область вывода, что читает, что пишет (UAV) }`.
`beginPass` в D3D11 — `OMSetRenderTargets` + viewport, транзитные слоты t0…t49 снимаются со входов всех стадий, виды
ресурсов, которые проход пишет, — и из слотов сцены (DMD3D помнит ресурс за каждым слотом SRV / UAV, виды помнят свой
ресурс); ресурсы прохода привязываются после `beginPass`. Второе правило — `setSRV` вида ресурса, который пишет текущий
проход, означает конец прохода над ним: цели снимаются, UAV отвязывается (`makeReadable`) — так ресурсы сцены (t101…t106)
привязываются сразу после проходов, которые их посчитали. В D3D12 на тех же местах — барьеры переходов: в `beginPass` по
целям и `writes` (плюс UAV-барьеры между compute-проходами), в `makeReadable` — в состояние чтения; `reads` — для списка
проходов, проверки и объединения барьеров. Список проходов кадра — команда консоли `passes` (`DMD3D::logPasses`, кадр в
`log.txt`: имя, цели с размером, `reads` / `writes`). Зависимости, которые в D3D12 станут барьерами:

- аргументы расстановки: UAV в `scatter.cs` → `INDIRECT_ARGUMENT`; инстансы: UAV → SRV вершинного шейдера;
- экспозиция: UAV `exposure_adapt.cs` → SRV пиксельных шейдеров (t105) и `COPY_SOURCE` для чтения на CPU;
- таблицы неба и объём воздушной перспективы: RTV / UAV → SRV (t106);
- карта теней: DSV каскадов → SRV (t104); буфер сцены: RTV → SRV постобработки; уровни bloom по цепочке;
- cubemap неба: RTV граней → SRV → уменьшение мипов в compute → SRV.

Все compute-проходы движка идут на границах проходов (`compute()` до отрисовки, экспозиция внутри постобработки), а
не внутри `renderCustom`, поэтому объявлений на уровне прохода достаточно.

## 5. План по шагам

Правило каждого шага: картинка с контрольных камер (§7) совпадает до пикселя (`engine.py screenshot` с
`--nogui --nowind`), «GPU average» не хуже, debug-слой молчит.

### Фаза A. Подготовка на D3D11 — движок всё время рабочий

- **A1. Flip model — сделано 2026-09-30** (итог и замеры — «Сделано» в `TODO.md`). `DMD3D::createDeviceSwapChain`:
  `CreateSwapChainForHwnd`, `DXGI_SWAP_EFFECT_FLIP_DISCARD`,
  2–3 буфера, `DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING` + `DXGI_PRESENT_ALLOW_TEARING` без vsync (проверить
  `DXGI_FEATURE_PRESENT_ALLOW_TEARING`), waitable object для задержки. Заодно — пересоздание целей по `WM_SIZE`
  (пункт «Размер окна» в `TODO.md`): в D3D12 `ResizeBuffers` требует, чтобы GPU закончил с задними буферами.
- **A2. Ноль `ID3D11` за пределами `Graphics/D3D/` — сделано 2026-09-30** (итог — «Сделано» в `TODO.md`; `DMD3D::instance()`
  в объектах остался до `CommandContext` A4 / A5). Ввести `ShaderView` / `TargetView` / `StorageView` / `Buffer` /
  `Texture` и заменить ими сигнатуры из §3. Дописать в `DMD3D` недостающие методы, чтобы убрать 88 `GetDeviceContext()`
  и 36 `GetDevice()`: `setVertexBuffer`, `setIndexBuffer`, `setTopology`, `dispatch`, `setUAV`, `clearDepth`,
  `resetTargets`, `copyBuffer`, создание текстур 2D / 3D / массивов / cubemap с видами. Метрика — `grep ID3D11 src`
  находит только `Graphics/D3D/` и `3rdParty/`. Попутно уходит `DMD3D::instance()` из объектов: контекст приходит через
  `RenderContext` (пункт про синглтоны в `TODO.md`).
- **A3. Кольцо констант — сделано 2026-09-30** (итог — «Сделано» в `TODO.md`; кольцо структурных буферов отложено
  до дескрипторов B3). `ConstantRing::alloc( size )` → указатель CPU + привязка; на D3D11 — один большой
  `DYNAMIC`-буфер, `MAP_WRITE_NO_OVERWRITE` (в начале кадра — `DISCARD`), привязка через
  `ID3D11DeviceContext1::*SetConstantBuffers1` со смещением в единицах по 16 констант (256 байт — то же выравнивание,
  что у CBV D3D12). Переписать два шаблона `Device::updateResource*` на кольцо — 28 мест вызова не меняются.
  `DMStructuredBuffer::updateData` (инстансы моделей, патчи террейна) — так же, но кольцо SRV-буфера.
- **A4. Объекты пайплайна — сделано 2026-09-30** (итог — «Сделано» в `TODO.md`). `PipelineDesc` и кэш; `DMShader::setPass`
  + `DMD3D::setState` → `context.setPipeline`.
  Места, где состояние ставится вручную: `Renderer::drawMesh` / `drawMeshInstanced` (11 `ScopedRenderState` /
  `setState` в `Renderer.cpp`), `Scatterer::renderCustom` (3), `CDLODTerrain::renderCustom` (2), `FullscreenShader::draw`,
  `SkySphere`. Input layout — в `PipelineDesc` из `m_layoutDesc`; класс `Layout` с псевдошейдером (подключён только в
  `DMGraphics.cpp`) — проверить, нужен ли ещё, и убрать.
- **A5. Объявления проходов — сделано 2026-09-30** (итог — «Сделано» в `TODO.md`, устройство — §4.4). `PassDesc` в
  `Renderer::executePass`, `renderShadows` (цель — срез каскада), `PostProcess::render` (экспозиция, уровни bloom,
  тонмаппинг), `SkyAtmosphere::compute` / `renderSkyCube`, `SkyLight`, `HDRIBackdrop`, `Scatterer::compute`, частицы, GUI.
  Разбросанные `OMSetRenderTargets( 0, … )`, `unbindShadowMap`, `unbindTransientResources` убраны — их делает
  `beginPass`. Список проходов кадра с ресурсами — команда `passes`.
- **A6. Три операции, которых нет в D3D12.** `GenerateMips` → compute-уменьшение cubemap (`Shaders/cube_downsample.cs`,
  2 места); `resetArgs` → копирование из GPU-буфера начальных аргументов; `readBackExposure` → `ReadbackRing<T>`
  (на D3D11 — то же кольцо копий, на D3D12 — с fence). Тот же `ReadbackRing` потом нужен `GpuProfiler`.
- **A7. Компиляция шейдеров.** Кэш DXBC на диске по хэшу исходника и defines (сейчас все материалы компилируются при
  каждом запуске — «Load material 517 мс» в `log.txt`); горячая перезагрузка остаётся в Debug. Подготовка к DXC: путь
  через `IDxcCompiler3` с тем же интерфейсом, пока выключен.

Каждый шаг A — отдельный коммит с проверкой по правилу выше. Порядок A1 → A2 → A3 → A4 → A5 важен: A4 и A5
опираются на интерфейс из A2. A1–A5 сделаны; остались A6 и A7.

### Фаза B. Бэкенд D3D12 рядом с D3D11

Выбор API — `settings.ini`, `[General] Api = D3D11 | D3D12`; `Tools/run.ps1` и `Tools/engine.py` получают параметр,
чтобы снимать одни и те же кадры обоими бэкендами.

- **B1. Устройство, очередь, swap chain, fence, `FrameResources`.** Debug-слой и GPU-based validation в Debug с
  выводом в `log.txt` через `ID3D12InfoQueue1`; DRED. Feature level 12_0, проверка Resource Binding Tier 3.
- **B2. Ресурсы.** Сначала committed-ресурсы; загрузка через upload-буфер и копирующую очередь (`UploadContext`).
  Текстуры — DirectXTex с `BUILD_DX12 ON`; `ScreenGrab12`; `CaptureTexture` D3D12 для карты высот.
  D3D12 Memory Allocator — позже, когда появится алиасинг (C5).
- **B3. Дескрипторы и root signature** из §4.3: постоянная shader-visible куча под текстуры и ресурсы сцены, кольцо на
  кадр под таблицы draw; сэмплеры статические.
- **B4. PSO** из `PipelineDesc` (A4) на тех же DXBC; `ID3D12PipelineLibrary` на диске.
- **B5. `CommandContext`**: барьеры из `PassDesc` (A5); `renderInstancedIndirect` → `ExecuteIndirect` с сигнатурой из
  одной команды `DRAW_INDEXED` — ровно то, что делает D3D11; `Dispatch`, копирования, очистки.
- **B6. `GpuProfiler`**: куча запросов TIMESTAMP, `ResolveQueryData` в readback раз за кадр, `PIXBeginEvent` /
  `PIXEndEvent` с теми же именами областей — чтобы PIX показывал их как сейчас RenderDoc.
- **B7. ImGui** — `imgui_impl_dx12`.
- **B8. Паритет.** Контрольные камеры (§7) совпадают между бэкендами до пикселя (допуск ±1 из 255 на округление),
  GPU-время в пределах ±5 %, в захвате PIX нет лишних барьеров и простоев между проходами. До паритета фаза C не
  начинается.

### Фаза C. То, ради чего всё это — по одному шагу с замером

- **C1. `ExecuteIndirect` со счётчиком для расстановки.** Сигнатура {root-константа (список, секция), `DRAW_INDEXED`};
  `scatter.cs` пишет команды и счётчик; один вызов на слой на вид вместо до 256; `copySectionCounts` удаляется. Замер:
  число вызовов и CPU в «Statistic», GPU в траве.
- **C2. Async compute.** `Scatterer::compute`, `SkyAtmosphere::compute`, гистограмма и адаптация экспозиции — на
  compute-очереди поверх каскадов теней; fence между очередями, ресурсы compute-проходов — по два экземпляра. Замер:
  кадр в траве (тени 0,5–2,0 мс есть чем перекрыть).
- **C3. DXC и bindless.** SM 6.6, `ResourceDescriptorHeap`: текстуры материала по индексам из буфера материалов,
  таблица T0 на draw исчезает. Нужно лесу (этап 10): кластеры с разными материалами в одном `ExecuteIndirect`.
- **C4. Mesh-шейдеры для травы.** SM 6.5, `DispatchMesh`: AS — поток на тайл (отсечение, LOD, число травинок), MS —
  травинки из хэша позиции с ветром. Запасной путь через VS остаётся. Сравнивать с нынешними 2,7 млн треугольников;
  подробности — `X:\Witcher_research\analysis\scene1\grass\GRASS.md`.
- **C5. Placed-ресурсы и алиасинг** через D3D12 Memory Allocator: уровни bloom, таблицы неба и промежуточные цели в
  одной памяти. Низкий приоритет — видеопамяти пока хватает.

## 6. Риски именно здесь

- **Взрыв PSO.** `ScopedRenderState` позволяет любое сочетание, но реальный набор — фазы × проходы × twoSided ×
  mirrored × wireframe. Сделано в A4: прогрев по списку рендерера (`Renderer::warmPipelines` + свои списки объектов) —
  826 пайплайнов, «ленивый» — строка в лог и счётчик в «Statistic», за контрольный прогон их ноль. Время старта уже
  4,3 с (`log.txt`), без кэша шейдеров и PSO оно вырастет: в D3D12 эти 826 — список для `ID3D12PipelineLibrary`.
- **Барьеры.** Пропущенный — артефакты или падение только под D3D12, лишний — простой. Защита: объявления A5 + список
  проходов `passes` + GPU-based validation в Debug + захват PIX на паритете. Уже в D3D11 A5 ловит пропуски: debug-слой
  ругается «still bound on output», когда ресурс привязан на вход, пока он цель, — так нашёлся `SkyLight::bind` сразу после
  своих проходов, отсюда правило `makeReadable` в `setSRV`.
- **Кольцо констант.** Переполнение кольца в кадре с большим числом draw — падение или перезапись данных, которые GPU
  ещё читает. Сделано в A3: 4 МБ с запасом, переполнение начинает участок заново с `DISCARD` (безопасно, но считается),
  счётчики «Constant ring = KB / writes / wraps» в «Statistic»; кольцевой буфер без записи в кадре не привязывается.
- **`WM_SIZE` и swap chain.** В D3D12 перед `ResizeBuffers` нужно дождаться GPU и отпустить все ссылки на задние
  буферы. Сделано в A1: `DMD3D::resize` (`unbindTransientResources` → `waitForGpu` → `releaseSizedTargets` →
  `ResizeBuffers` → цели заново), проверено уменьшением окна извне и сворачиванием.
- **Debug-слой строже.** Состояния ресурсов, размеры кучи, срок жизни upload-буферов до завершения копирования — то,
  что D3D11 прощал. Проверять каждый шаг B в Debug.
- **Кадр без vsync.** Flip model без `ALLOW_TEARING` ограничит частоту кадров частотой монитора; «GPU average» на это не
  влияет, но сравнения «на глаз» — да. На RTX 4070 Ti tearing поддерживается (лог: «tearing allowed»).

## 7. Как проверять

- Контрольные камеры — у земли нынешней долины: обзор со стартовой камеры `512,90,150,20,0`, у шаров `500,20,215,4,5`, в траве `530,12.8,250,8,210`, у рощи `480,12.6,320,3,0`.
  Камеры таблицы `docs/passes.md` (`512,96,212,12,0` и три другие) сняты до перегенерации рельефа (этап 8.1) и висят
  в 45–85 м над травой — в кадре только террейн и небо, кольцо расстановки до них не достаёт. Снимки — задний
  буфер 1920 × 1080 через `python Tools/engine.py screenshot` при `start --nogui --nowind` (размер окна и монитор
  не важны, полноэкранный режим не нужен), сравнение по пикселям — `Tools/compare_frames.py` (два файла или две
  папки, `--tolerance`, `--diff` — карта расхождений). Между запусками одной сборки ±1 в 15–350 пикселях — двоякое
  округление экспозиции (пункт в «Решить или проверить» `TODO.md`), поэтому допуск ±1, как у B8.
- «GPU average» (`stat gpu 3`) по тем же камерам до и после каждого шага; на нынешней долине с подменной высокой
  травой: у шаров 2,2 мс, в траве 2,9 мс, у рощи 3,7 мс (Release, RTX 4070 Ti, 2026-09-30).
- `log.txt` без сообщений debug-слоя; в D3D12 — ещё и GPU-based validation в Debug.
- Захваты: RenderDoc для D3D11, PIX для D3D12 (в `X:\Witcher_research\tools\` — скрипты разбора захватов PIX,
  `CAPTURE_ANALYSIS.md` — приёмы).

## 8. Куда это в `TODO.md`

- Фазы A и B — этап 9 `TODO.md`, перед лесом (этап 10): лес строится на отсечении и indirect на GPU, и делать его
  стоит уже на интерфейсе A2 / A5, а не на `GetDeviceContext()`. Шаги A1 и A7 независимы и годятся в любой момент;
  фаза B — после A, может идти параллельно деревьям и маскам этапа 10.
- C1, C2 и C3 — пункты этапа 10 вместе с лесом (расстановка деревьев кластерами — первый потребитель `ExecuteIndirect`
  со счётчиком); C4 — резерв травы в этапе 6, когда упрётся; C5 — в «Условные и на потом».

## 9. Что читать

- Общие соображения и пять рисков переезда: `X:\Witcher_research\D3D12_MIGRATION.md`; как W3 устроил D3D12 (привязка,
  дескрипторы, память, очереди, барьеры): `X:\Witcher_research\REPORT.md` §4.
- Frostbite, «FrameGraph: Extensible Rendering Architecture in Frostbite» (GDC 2017) — объявление проходов и барьеры.
- Microsoft: Resource Barriers, Enhanced Barriers, `ExecuteIndirect`, Pipeline State Objects и `ID3D12PipelineLibrary`,
  Descriptor Heaps, Mesh Shaders; DirectXTex (`DirectXTex.h` с D3D12, `ScreenGrab12.h`); WinPixEventRuntime.
- GPUOpen: D3D12 Memory Allocator; meshoptimizer — meshlet'ы для C4.
