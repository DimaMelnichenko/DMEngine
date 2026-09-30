# Переезд DMEngine на D3D12: прямое переписывание бэкенда

План перехода движка с D3D11 на D3D12 **без второго бэкенда**: слой `Graphics/D3D/` переписывается на D3D12 целиком,
сразу в современном виде (Agility SDK, DXC и SM 6.6, bindless, enhanced barriers, D3D12 Memory Allocator, PIX), а D3D11
остаётся только в истории git как эталон для сравнения кадров. Составлен 2026-09-30 по коду ветки `v2` после коммита
`6a8cd3d` («Этап 9: A1–A5 переезда на D3D12»); первая редакция плана (подготовка A на D3D11, затем бэкенд D3D12 рядом с
D3D11 и паритет двух бэкендов) — в истории этого файла. Это документ-план: пункты по мере выполнения уходят в
`TODO.md` → «Сделано», а этот файл становится описанием слоя.

## 1. Зачем это именно DMEngine

D3D12 сам по себе кадр не ускоряет: шейдеры, растеризация и полоса памяти те же, а порт «в лоб» часто медленнее D3D11.
Кадр движка — 1,4–3,4 мс GPU в 1920 × 1080 на контрольных камерах (§7), CPU на сбор мешей и раскладку — доли
миллисекунды, поэтому многопоточная запись команд и дешёвые вызовы, главные CPU-плюсы D3D12, в FPS сейчас не
превратятся. Выигрыш — в возможностях, которых в D3D11 нет, и они нужны ближайшим этапам `TODO.md`:

| Этап | Что нужно | Что даёт D3D12 |
|---|---|---|
| 6, резервы | Тени 0,4–2,0 мс упираются в растеризацию, compute расстановки, таблицы неба и гистограмма экспозиции идут в той же очереди | **Async compute**: compute-проходы на второй очереди поверх каскадов теней и prepass |
| 6, трава | 2,7 млн треугольников, растеризуются дважды (prepass + цвет) | **Mesh-шейдеры** (SM 6.5): отсечение тайлов и LOD в amplification-шейдере, травинки из mesh-шейдера без буфера инстансов |
| 10, лес | Кластеры деревьев: отсечение и LOD на GPU, импостеры вдали, разные материалы | **`ExecuteIndirect` со счётчиком** — один вызов на все кластеры; **bindless** — материалы кластеров в одном вызове |
| 14, деревня | Много источников света (Forward+) | Отсечение света по плиткам в compute на второй очереди |
| 15, масштаб | Выбор узлов CDLOD на GPU | Тот же `ExecuteIndirect` со счётчиком |

Уже сегодня расстановка упирается в модель D3D11: до 256 indirect-вызовов на слой на вид и лишний dispatch
`copySectionCounts` только потому, что каждый вызов D3D11 читает свою запись аргументов.

## 2. Почему прямое переписывание, а не два бэкенда

Первая редакция плана предполагала бэкенд D3D12 рядом с D3D11 под общим слоем и паритет двух бэкендов внутри одного
exe. Для этого движка, одного разработчика и одной видеокарты это лишняя работа:

- **Подготовка уже сделана.** Шаги A1–A5 (2026-09-30) убрали D3D11 из движка: все `ID3D11*` живут в `Graphics/D3D/`
  (`DMD3D.cpp` 1,8 тыс. строк плюс `ConstantRing`, `TextureImages`, `CubeTarget`, `RenderTarget`, `DMStructuredBuffer`,
  `DMSamplerState`, `GpuProfiler` — всего 3,3 тыс. строк), остальной код говорит с `DMD3D` через непрозрачные ресурсы
  (`GpuResources.h`), пайплайны (`GpuPipeline.h`) и объявления проходов (`GpuPass.h`). Переписать нужно один каталог.
- **Эталон есть в git.** Кадры контрольных камер и «GPU average» снимаются со сборки коммита `6a8cd3d` в отдельном
  worktree (§7); переключатель API в одном exe для этого не нужен.
- **D3D11 после переезда — мёртвый код.** Фаза возможностей (§5, «дальше») только для D3D12, движок принципиально
  вычищает невызываемый код (`CLAUDE.md`), значит поддерживать второй бэкенд было бы некому и незачем.
- **Что теряем** — возможность в одном запуске переключиться на D3D11 и сравнить проход за проходом. Замена: команда
  `passes` (список проходов с целями и барьерами), GPU-based validation, захват PIX и контрольные точки вехи M4 (§5),
  каждая из которых сравнивается с эталонным кадром.

Цена прямого пути — между вехами M1 и M4 вершина `v2` не рисует кадр целиком. Это принято (2026-09-30): на время
переезда репозиторий больше никем не используется, поэтому отдельная ветка не заводится, вехи идут в `v2`, а рабочий
D3D11 — это коммит `6a8cd3d`.

## 3. Целевой стек: «современный D3D12» на 2026-09-30

Проверено на этой машине: Windows 11 22H2 (сборка 22621), RTX 4070 Ti, драйвер NVIDIA 595.97, Windows SDK 10.0.26100,
PIX 2603.25, компонент «Средства графики» (debug-слои) установлен.

| Компонент | Версия | Откуда | Зачем |
|---|---|---|---|
| **DirectX 12 Agility SDK** | 1.619.6 (2026-09-14) | NuGet `Microsoft.Direct3D.D3D12`: zip по URL `https://www.nuget.org/api/v2/package/Microsoft.Direct3D.D3D12/1.619.6` через `FetchContent`; redist `D3D12Core.dll` и `d3d12SDKLayers.dll` копируются в `D3D12\` рядом с exe; в exe экспорт `D3D12SDKVersion = 619` и `D3D12SDKPath = ".\\D3D12\\"` | Новый рантайм независимо от ОС: enhanced barriers, SM 6.6+, debug-слой и GPU-based validation из того же пакета. Системный `D3D12Core.dll` здесь — 10.0.22621, старый |
| **DirectX-Headers** | тег под ту же 1.619 | `FetchContent` с GitHub `microsoft/DirectX-Headers` | `d3d12.h` в версии Agility SDK, `d3dx12.h` (helpers: `CD3DX12_*`, `UpdateSubresources`, pipeline state stream) |
| **DXC** | v1.9.2609 (2026-09) | zip релиза GitHub `microsoft/DirectXShaderCompiler` через `FetchContent`; `dxcompiler.dll` и `dxil.dll` рядом с exe | Компиляция HLSL в DXIL в процессе (`IDxcCompiler3`), SM 6.6 ради `ResourceDescriptorHeap`. `dxil.dll` подписывает DXIL — без него рантайм байткод не примет. `dxc` из Windows SDK 26100 — 1.7.2308 (2023), старый |
| **D3D12 Memory Allocator** | 3.2.0 (2026-06) | `FetchContent` с GitHub `GPUOpen-LibrariesAndSDKs/D3D12MemoryAllocator` | Память под ресурсы: committed / placed без своего аллокатора куч, потом алиасинг |
| **WinPixEventRuntime** | 1.0.240308001 | NuGet zip через `FetchContent`; `WinPixEventRuntime.dll` рядом с exe | `PIXBeginEvent` / `PIXEndEvent` в командном списке — области профайлера видны в PIX |
| **DirectXTex** | тег `may2026` (уже в проекте), `BUILD_DX12 ON`, `BUILD_DX11 OFF` | уже `FetchContent` | `CreateTexture` / `PrepareUpload` / `CaptureTexture( ID3D12CommandQueue* … )` для карты высот, `ScreenGrab12` вместо `3rdParty/ScreenGrab` |
| **Dear ImGui** | обновить с 1.66 WIP (2018) до текущей 1.92.x (решено 2026-09-30) | ядро + `imgui_impl_win32` + `imgui_impl_dx12` | Бэкенд DX12 для версии 1.66 не сочетается с нынешним ядром. GUI движка использует ~35 функций (`Begin`/`End`, `TreeNode`, `DragFloat*`, `SliderFloat*`, `ColorEdit*`, `BeginCombo`, `Image`…) — все есть в 1.92; `ImGui::Image` принимает `ImTextureRef` с GPU-дескриптором |

Что требуется от устройства, без запасных путей (проверка при старте, при отсутствии — строка в лог и выход):
feature level 12_0, Resource Binding Tier 3, `D3D12_FEATURE_SHADER_MODEL` ≥ 6.6, `D3D12_FEATURE_D3D12_OPTIONS12::EnhancedBarriersSupported`.
На RTX 4070 Ti всё это есть. Необязательное: `D3D12_FEATURE_D3D12_OPTIONS16::GPUUploadHeapSupported` (куча
`GPU_UPLOAD`, запись CPU прямо в видеопамять через ReBAR) — требует Windows 11 сборки 26080+, на этой машине (22621)
недоступно; кольцо констант делается на `UPLOAD`-куче, а переход на `GPU_UPLOAD` — одна проверка флага на потом.

Все 33 шейдера движка (7 `.vs`, 19 `.ps`, 1 `.gs`, 7 `.cs`; `common.vs` — include без точки входа) уже компилируются DXC
с `-T *_6_6 -HV 2021` без ошибок и предупреждений с defines по умолчанию (проверено 2026-09-30). Варианты с defines
(`ALPHA_MASK`, `INST_MATRIX`, `LOD_DITHER`, `DEPTH_ONLY`, `WIND_TREE`, точка входа `mainDepth`) проверяются на вехе M3.

## 4. Устройство бэкенда

### 4.1. Интерфейс остаётся

Публичный интерфейс `DMD3D` после A5 — уже интерфейс D3D12: `beginFrame` / `BeginScene` / `beginPass( PassDesc )` /
`setPipeline( pipeline( PipelineDesc ) )` / создание ресурсов и видов по описаниям / `setConstantBuffer`, `setSRV`,
`setUAV`, буферы вершин и индексов / `draw*`, `drawIndexedInstancedIndirect`, `dispatch` / `copyBuffer`, `readBuffer` /
`EndScene`. Объекты сцены, материалы и `Renderer` не меняются, кроме перечисленного в §4.10. Внутри — `ID3D12*`, только в
`Graphics/D3D/`; `GetDevice()` остаётся для привязки ImGui (`ID3D12Device*` и куча дескрипторов).

### 4.2. Кадр

`FrameResources[2]` (два кадра в полёте, как сейчас `SetMaximumFrameLatency( 2 )`): аллокатор команд, участок кольца
upload, значение fence, очередь отложенного удаления (ресурс отпускается, когда fence его кадра пройден — размер окна,
перезагрузка). Один графический командный список на кадр (запись в один поток, как сейчас); объекты очередей compute и
copy создаются сразу, но до async compute (§5, «дальше») используется только direct. Swap chain остаётся тем же кодом
A1 (flip model, tearing, waitable object): `beginFrame` ждёт waitable object и fence кадра N − 2, `EndScene` закрывает
список, `ExecuteCommandLists`, `Signal`, `Present`. Барьер заднего буфера `PRESENT` ↔ `RENDER_TARGET` — в `BeginScene` /
`EndScene`.

### 4.3. Память и загрузка

- **D3D12MA** владеет всей памятью: `Buffer` и `Texture` держат `D3D12MA::Allocation`; сначала committed / placed на
  усмотрение аллокатора, алиасинг — отдельным шагом потом.
- **Кольцо констант** (`ConstantRing`, 4 МБ, участки по 256 байт) переезжает на буфер в `UPLOAD`-куче, отображённый
  один раз навсегда; привязка — `SetGraphicsRootConstantBufferView( GPU-адрес + смещение )`. Логика A3 не меняется.
  Туда же — данные, которые сейчас пишет `DMStructuredBuffer::updateData` каждый кадр (матрицы инстансов, патчи
  террейна, источники света): участок кольца плюс SRV со смещением (кольцо структурных буферов, отложенное в A3).
- **Загрузка** статических ресурсов (меши, текстуры, начальные данные буферов): один staging-буфер, `CopyBufferRegion` /
  `CopyTextureRegion` (`UpdateSubresources` из `d3dx12.h`) на direct-очереди во время инициализации, барьер в состояние
  чтения, fence в конце загрузки. Копирующая очередь — когда появится подгрузка во время игры. Текстуры — DirectXTex
  D3D12 (`CreateTexture` + `PrepareUpload`), карта высот на CPU — `CaptureTexture( queue, … )`.
- **Редкие обновления** (`updateBuffer`: варианты расстановки, начальные indirect-аргументы) — участок upload + копия
  в командном списке кадра + барьер; **чтение на CPU** (`BufferUsage::readback`) — `READBACK`-куча, `copyBuffer`
  запоминает в `Buffer` значение fence кадра, `readBuffer` отдаёт данные, только если fence пройден. `ReadbackRing<T>`
  (веха M0) над этим одинаков для экспозиции и профайлера.

### 4.4. Дескрипторы: bindless с первого дня

Одна shader-visible куча `CBV_SRV_UAV` (Tier 3 — до миллиона дескрипторов; стартовый размер 64 К) со списком свободных.
Каждый `ShaderView` и `StorageView` получает **постоянный индекс** при создании и живёт с ним до удаления; UAV ещё держит
копию в CPU-куче для `ClearUnorderedAccessView*`. Никакого копирования дескрипторов на draw. RTV и DSV — свои
не shader-visible кучи, по дескриптору на `TargetView`. Сэмплеры — **статические в root signature** (s0…s7 из
`samplers.sh` и s8 сравнения), `SamplerDescriptorHeap` не нужен.

**Root signature 1.1**, одна на графику и compute (флаг `CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED`, для графики ещё
`ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT`):

| Параметр | Что | DWORD |
|---|---|---|
| root-константы «привязки вызова» | индексы дескрипторов по слотам: t0…t16 (17), u0…u7 (8), t100…t106 (7) — ровно 32 | 32 |
| root CBV b0…b7 | кадр, объект, материал / проход, тени, буферы проходов b4…b7 — адреса в кольце констант или буферов | 16 |
| статические сэмплеры s0…s8 | | 0 |

Итого 48 из 64. `setSRV( stage, slot, view )` и `setUAV( slot, view )` **не меняют сигнатуру**: они пишут индекс вида в
массив привязок текущего вызова (стадия не важна — таблица одна на вызов), `draw*` / `dispatch` перед вызовом ставит
массив одним `SetGraphicsRoot32BitConstants`, если он менялся. Слоты сцены t100…t106 — та же таблица, ставятся раз за
кадр и переживают `beginPass` (как сейчас). Индекс 0 в куче — вечная заглушка (шахматка `placeholderId` 0 хранилищ
текстур), поэтому непривязанный слот читает шахматку, а не мусор.

В HLSL — `Shaders/bindless.sh`: root-константы как `cbuffer DrawBindings : register( b8 ) { uint4 g_bindings[8]; }`
(`uint4`, а не `uint[32]`: элементы массива в cbuffer выравниваются по 16 байт) и макросы `DM_SRV_INDEX( slot )` →
`g_bindings[slot / 4][slot % 4]`, `DM_TEXTURE2D( name, slot )` и т. п. → `Texture2D<float4> name =
ResourceDescriptorHeap[DM_SRV_INDEX( slot )]`. Слоты `slots.h` остаются номерами в таблице привязок, так что C++ и HLSL
продолжают говорить `SLOT_SHADOW_MAP`. Объявлений ресурсов в шейдерах 54 (46 SRV, 8 UAV) в ~25 файлах — замена
механическая; форма макроса (глобальная `static` или локальная в начале функции) уточняется компиляцией на M3.
Альтернатива — классические таблицы дескрипторов по `register( tN )` с копированием ~17 дескрипторов на draw в кольцо
(«как в W3», первая редакция плана): шейдеры не трогать, но появляется раскладка таблиц, кольцо дескрипторов и отдельный
шаг bindless потом. Выбор — bindless сразу: лес (кластеры с разными материалами в одном `ExecuteIndirect`) без него
невозможен, а объём правки шейдеров — час-два.

### 4.5. Пайплайны

`PipelineDesc` (A4) → `D3D12_PIPELINE_STATE_STREAM_DESC`: стадии DXIL, растеризатор / глубина / блендинг из
`RenderState` (перечисления `RenderState.h` — в описания D3D12, объектов состояний больше нет), input layout из
`VertexElement` (байткод VS не нужен: `createInputLayout` теряет параметры байткода), тип топологии, **форматы целей**
— `beginPass` записывает форматы своих целей в контекст, и `pipeline( desc )` дополняет ключ ими: одна фаза материала
в проходе сцены (R16G16B16A16_FLOAT + D32), в каскаде теней (только D32) и в тонмаппинге (R8G8B8A8_UNORM_SRGB как вид
заднего буфера) — три PSO. Списки прогрева (`Renderer::warmPipelines` и свои у объектов) получают форматы проходов.
Кэш на диске — `ID3D12PipelineLibrary1` (`cache/pipelines.bin`), ключ — хэш описания и DXIL стадий; «ленивый» PSO —
строка в лог и счётчик, как сейчас.

### 4.6. Шейдеры

Класс `ShaderCompiler` (`D3D/ShaderCompiler.h`) на `IDxcCompiler3` + `IDxcUtils` с обработчиком `#include` для
`Shaders\` заменяет `D3DCompileFromFile` в трёх местах (`DMShader::addShaderPassFromFile`, `FullscreenShader::load`,
`DMComputeShader::Initialize`). Цель `*_6_6`, `-HV 2021`; Debug — `-Zi -Qembed_debug -Od` (исходник виден в PIX), Release
— `-O3`. **Кэш DXIL на диске** по хэшу исходника (с include) и defines — `cache/shaders/<hash>.dxil`: сейчас все материалы
компилируются при каждом запуске («Load material 517 мс»), с кэшем старт короче; горячая перезагрузка в Debug — та же
компиляция. Это бывший шаг A7 первой редакции.

### 4.7. Проходы и барьеры

**Enhanced barriers** (`ID3D12GraphicsCommandList7::Barrier`, Agility SDK), а не legacy resource barriers. Бэкенд ведёт
состояние (layout, sync, access) каждого подресурса — мипы куба неба и срезы массива каскадов теней переходят по
отдельности. Барьеры считаются ровно там, где A5 расставил правила:

- `beginPass`: цели → `RENDER_TARGET` / `DEPTH_STENCIL_WRITE`, `reads` → `SHADER_RESOURCE`, `writes` →
  `UNORDERED_ACCESS`; между двумя compute-проходами, пишущими один ресурс, — UAV-барьер. Все барьеры прохода — одним
  вызовом `Barrier` (пачкой);
- `makeReadable` (вид ресурса, который пишет текущий проход, привязан на вход) → барьер в `SHADER_RESOURCE`;
- `drawIndexedInstancedIndirect` → аргументы в `INDIRECT_ARGUMENT`; `copyBuffer` / `updateBuffer` → `COPY_DEST` /
  `COPY_SOURCE`; `readBuffer` — источник копии; задний буфер — §4.2;
- очистки (`ClearRenderTargetView`, `ClearDepthStencilView`) требуют layout цели — порядок «beginPass, затем clear» в
  коде уже такой.

`passes` печатает и барьеры каждого прохода — по нему видно лишние переходы (простой) и пропущенные (их ловит GPU-based
validation). `reads` объявлений — для этого списка и для объединения барьеров, не единственный источник истины:
`makeReadable` страхует то, что объявить забыли.

### 4.8. Профайлер и отладка

- `GpuProfiler`: `ID3D12QueryHeap` TIMESTAMP (по 2 × 64 запросов на кадр в полёте), `ResolveQueryData` в
  `ReadbackRing`, частота — `GetTimestampFrequency` очереди; области — те же имена, плюс `PIXBeginEvent` /
  `PIXEndEvent` на командном списке и `SetName` у ресурсов (имена из `PassDesc` и хранилищ) — так захват PIX читается
  как сейчас RenderDoc.
- Debug-сборка: `D3D12GetDebugInterface` → `EnableDebugLayer` + `SetEnableGPUBasedValidation` (ключ `GpuValidation` в
  `settings.ini`, чтобы выключать, когда медленно), `ID3D12InfoQueue1::RegisterMessageCallback` → `log.txt` с тем же
  подавлением повторов, что у `logDebugMessages`; DRED (`ID3D12DeviceRemovedExtendedDataSettings1`: auto-breadcrumbs и
  page fault) — при `DXGI_ERROR_DEVICE_REMOVED` в лог уходит последний выполненный проход.
- Статистика: к «Statistic» добавляются занятость кучи дескрипторов, кольца upload, число барьеров за кадр и бюджет
  видеопамяти (`IDXGIAdapter3::QueryVideoMemoryInfo`).

### 4.9. GUI

`imgui_impl_dx12` версии 1.92: инициализация через `ImGui_ImplDX12_InitInfo` с нашей кучей `CBV_SRV_UAV` и
callback'ами выделения / освобождения дескрипторов (список свободных §4.4), формат заднего буфера, очередь. Превью
текстур в GUI — GPU-дескриптор вида по его индексу.

### 4.10. Что уходит и что меняется снаружи

Уходят: библиотеки `d3d11` и `d3dcompiler`, `3rdParty/ScreenGrab`, `imgui_impl_dx11`, `TextureUsage::generateMips` и
`DMD3D::generateMips` (мипы — compute, веха M0), `drawAuto` (не используется), `unbindSRV` / `unbindUAVs` /
`unbindShaders` / `setShaderStage` / `setInputLayout` / `setTopology` (всё это — часть PSO или не имеет смысла при
bindless), путь MSAA (`MSAACount`, `ResolveSubresource` в `sceneColor`: по умолчанию 1, постобработка читает буфер сцены
напрямую; MSAA при желании возвращается отдельным пунктом), `Device::updateResource*` через `Map` (все обновления кадра —
кольцо). Снаружи `Graphics/D3D/` меняются: `DMShader` / `FullscreenShader` / `DMComputeShader` (компилятор),
`GUI.cpp` (бэкенд ImGui), `CubeTarget` (виды мипов для compute), шейдеры (bindless-макросы), CMake.

## 5. Вехи

Вехи идут прямо в `v2` (§2). Веха — коммит с проверкой (§7): что должно совпасть, сказано в каждой. Правило коммитов проекта —
короткий поясняющий коммит после каждого пункта, push не делать.

- **M0. Операции, которых нет в D3D12 — на D3D11, пока он есть** (бывший A6) — сделано 2026-09-30 (итог — «Сделано» в `TODO.md`). `Shaders/cube_downsample.cs` строит мипы
  куба неба и HDRI (`CubeTarget` получает UAV и SRV на каждый мип, `SkyLight::capture` строит мипы перед захватом —
  проверка против `GenerateMips` до пикселя возможна только сейчас); `ScatterPass::resetArgs` — копия из GPU-буфера
  начальных аргументов (`copyBuffer`, буфер обновляется только когда меняются секции); `ReadbackRing<T>`
  (`D3D/ReadbackRing.h`) для экспозиции. Проверка: контрольные камеры ±1, GPU не хуже, debug-слой D3D11 молчит.
- **M1. Зависимости и каркас.** CMake: `FetchContent` для Agility SDK, DirectX-Headers, DXC, D3D12MA,
  WinPixEventRuntime; DirectXTex `BUILD_DX12`; копирование DLL рядом с exe после сборки; экспорт `D3D12SDKVersion` /
  `D3D12SDKPath`; ImGui 1.92 с `imgui_impl_dx12`. Устройство на выбранном адаптере (код A1), очереди, swap chain, fence,
  `FrameResources`, кучи RTV / DSV / `CBV_SRV_UAV`, debug-слой + GPU-based validation + `InfoQueue1` + DRED. Остальные
  методы `DMD3D` — заглушки с одной строкой в лог, чтобы движок собирался и запускался целиком. Проверка: окно, очистка
  цветом, GUI ImGui поверх, `passes` печатает проходы, лог без сообщений слоя, `engine.py` работает.
- **M2. Ресурсы.** D3D12MA, `createBuffer` / `createTexture` / виды с индексами в куче, staging-загрузка, DirectXTex D3D12,
  карта высот через `CaptureTexture`, readback с fence, отложенное удаление, `resize`. Проверка: уровень `Test` грузится,
  в логе те же 20 заглушек, что на D3D11 (`Missing resources replaced by placeholders: 20`), бюджет видеопамяти в
  «Statistic».
- **M3. Шейдеры и пайплайны.** `ShaderCompiler` на DXC с кэшем DXIL; `Shaders/bindless.sh` и замена 54 объявлений
  ресурсов; root signature §4.4; PSO из `PipelineDesc` с форматами целей, `ID3D12PipelineLibrary1`; прогрев. Проверка:
  все варианты шейдеров (включая defines и `mainDepth`) компилируются, 826 пайплайнов собираются, время старта с кэшем
  и без — в лог.
- **M4. Кадр.** Запись команд: кольцо констант и структурных данных на upload, привязки root-константами, барьеры из
  `PassDesc` и `makeReadable`, `draw*` / `ExecuteIndirect` (сигнатура из одной `DRAW_INDEXED` — ровно то, что делает
  D3D11) / `Dispatch`, очистки, копии, мипы куба из M0. Контрольные точки — снимки против эталона D3D11 (§7), в порядке
  появления в кадре: (a) небо и его таблицы — полноэкранные проходы; (b) террейн и модели — prepass, opaque,
  инстансинг; (c) каскады теней; (d) расстановка — indirect; (e) постобработка — экспозиция в compute с чтением на CPU,
  bloom, тонмаппинг; (f) частицы (`Levels.particles = 1` на время проверки). Итог вехи: четыре камеры ±1 (или
  задокументированные расхождения от DXC, §6), GPU-based validation в Debug молчит.
- **M5. Профайлер, снимки, чистка.** Query heap и PIX-события, `ScreenGrab12` (`screenshot`, клавиша P),
  «GPU average» в пределах ±5 % от эталона по четырём камерам, захват PIX без лишних барьеров и простоев между
  проходами. Удаление всего из §4.10, `CLAUDE.md` и `docs/` (этот файл — описание слоя, `passes.md`, `postprocess.md`,
  `remote.md`).

**Дальше — то, ради чего всё это** (замер каждого шага; места в `TODO.md` — §8):

- **ExecuteIndirect со счётчиком** для расстановки — первым, сразу после M5: путь indirect в `ScatterPass` только что
  переписан; сигнатура {root-константа (список, секция), `DRAW_INDEXED`}, `scatter.cs` пишет команды и счётчик, один
  вызов на слой на вид вместо до 256, `copySectionCounts` удаляется.
- **Async compute**: compute-проходы на второй очереди поверх каскадов теней и prepass, fence между очередями,
  ресурсы compute — по два экземпляра.
- **Mesh-шейдеры травы** (SM 6.5, `DispatchMesh`) — резерв этапа 6.
- **Placed-ресурсы и алиасинг** через D3D12MA — когда упрётся видеопамять.
- DXC, SM 6.6 и bindless из первой редакции (C3) сделаны внутри M3 — отдельного шага нет.

Оценка объёма: новый бэкенд — 3–4 тыс. строк против 3,3 тыс. нынешних (D3D12 явно делает то, что D3D11 делал сам),
правка шейдеров и трёх компиляторов — механическая. По сессиям: M0 и M1 — по одной, M2 и M3 — одна-две, M4 — две-три,
M5 — одна.

## 6. Риски прямого пути

- **Чёрный кадр, в котором ново всё.** Защита: контрольные точки M4 по одному проходу, GPU-based validation (ловит
  барьеры, вышедшие за кучу индексы, неинициализированные дескрипторы), `passes` с барьерами, PIX (в
  `X:\Witcher_research\tools\` — скрипты разбора захватов), DRED при потере устройства.
- **fxc → DXC меняет числа.** Другой компилятор — другая свёртка FMA, другие приближения `pow` / `exp`; после тонмаппинга
  это ±1 в отдельных пикселях, но в местах, чувствительных к точности (глубина prepass с проверкой `EQUAL` — позиция уже
  `precise`, экспозиция), может быть больше. Отличить ошибку бэкенда от разницы компиляторов внутри одной сборки нельзя
  (DXC не даёт байткода для D3D11); поэтому расхождения больше ±1 разбираются по проходам в PIX, и допуск
  фиксируется в `TODO.md` только после того, как причина названа.
- **Загрузчик Agility SDK.** Без экспорта `D3D12SDKVersion` / `D3D12SDKPath` или без `D3D12\D3D12Core.dll` рядом с exe
  берётся системный рантайм 22621 без enhanced barriers — проверка `EnhancedBarriersSupported` при старте скажет об
  этом явно. Debug-слой берётся из того же redist (`d3d12SDKLayers.dll`).
- **DXIL без подписи.** `dxil.dll` должна лежать рядом с `dxcompiler.dll`, иначе рантайм отвергает шейдеры — проверка
  при старте с понятной строкой в лог.
- **Bindless: неверный индекс читает чужой ресурс** и не падает. Заглушка под индексом 0, GPU-based validation, в
  Debug — проверка индекса в `setSRV` (вид без дескриптора — ошибка в лог).
- **Обновление ImGui** 1.66 → 1.92: поверхность API у GUI мала (§3), но проверить каждое окно глазами.
- **Срок жизни ресурсов.** Удаление ресурса, который GPU ещё читает (размер окна, перезагрузка уровня) — только через
  очередь отложенного удаления по fence; это ловит debug-слой.
- **Простой между проходами** от лишних или по одному выставленных барьеров — смотреть в PIX на M5, барьеры прохода
  пачкой.
- **Кадр без vsync**: как в A1, tearing поддерживается («tearing allowed» в логе).

## 7. Как проверять

- **Эталон D3D11** — сборка коммита `6a8cd3d` в отдельном worktree: `git worktree add ..\DMEngine-d3d11 6a8cd3d`, там
  `Tools\build.cmd release`, `python Tools/engine.py start --config Release --nogui --nowind` и снимки четырёх камер в
  `DownloadResources\reference\d3d11\` (не в git). Ресурсы (`Textures\`, `Meshes\`, `base.db3`) у обеих сборок должны быть
  одни и те же — копировать или ссылаться, иначе кадры несравнимы.
- **Контрольные камеры** у земли нынешней долины: обзор `512,90,150,20,0`, у шаров `500,20,215,4,5`, в траве
  `530,12.8,250,8,210`, у рощи `480,12.6,320,3,0`. Снимки — задний буфер 1920 × 1080 (`engine.py screenshot` при
  `start --nogui --nowind`), сравнение — `Tools/compare_frames.py` (два файла или две папки, `--tolerance 1`, `--diff`).
  Между запусками одной сборки ±1 в 15–350 пикселях — двоякое округление экспозиции (пункт в «Решить или проверить»
  `TODO.md`), поэтому допуск ±1. Камера обзора шумит сильнее (каскад 0 — 0,54…0,83 мс между запусками), для GPU
  опираться на три остальные.
- **«GPU average»** (`stat gpu 3`) по тем же камерам; эталон D3D11 после A5 (Release, RTX 4070 Ti, 2026-09-30): обзор
  1,44, шары 1,95, трава 2,63, роща 3,40 мс. Паритет — ±5 %.
- `log.txt` без сообщений debug-слоя; в D3D12 — с GPU-based validation в Debug. Захваты: PIX
  (`X:\Witcher_research\CAPTURE_ANALYSIS.md` — приёмы), RenderDoc остаётся для эталона D3D11.

## 8. Куда это в `TODO.md`

- Вехи M0–M5 — этап 9 `TODO.md`, перед лесом (этап 10): лес строится на `ExecuteIndirect` со счётчиком и bindless.
- `ExecuteIndirect` со счётчиком (C1) и async compute (C2) — пункты этапа 10; mesh-шейдеры травы — резерв этапа 6;
  алиасинг — «Условные и на потом». Пункт C3 (DXC и bindless) снят: делается в M3. Кэш шейдеров (A7) — тоже в M3;
  пункт «Cook ресурсов» ссылается на него.

## 9. Что читать

- Microsoft: [DirectX 12 Agility SDK](https://devblogs.microsoft.com/directx/directx12agility/) и пакет
  [Microsoft.Direct3D.D3D12](https://www.nuget.org/packages/Microsoft.Direct3D.D3D12);
  [DirectX-Headers](https://github.com/microsoft/DirectX-Headers) (`d3dx12.h`);
  [Enhanced Barriers](https://microsoft.github.io/DirectX-Specs/d3d/D3D12EnhancedBarriers.html); спецификация
  SM 6.6 Dynamic Resources (`ResourceDescriptorHeap`) в DirectX-Specs; `ExecuteIndirect`; Pipeline State Objects и
  `ID3D12PipelineLibrary1`; Mesh Shaders.
- [DirectXShaderCompiler](https://github.com/microsoft/DirectXShaderCompiler/releases) — релизы, wiki «Using dxc.exe and
  dxcompiler.dll».
- [D3D12 Memory Allocator](https://gpuopen-librariesandsdks.github.io/D3D12MemoryAllocator/html/) — документация и
  «Optimal resource allocation».
- [WinPixEventRuntime](https://devblogs.microsoft.com/pix/winpixeventruntime/) и
  [пакет](https://www.nuget.org/packages/WinPixEventRuntime).
- DirectXTex: `DirectXTex.h` с `ID3D12Device`, `ScreenGrab12.h`; Dear ImGui:
  [imgui_impl_dx12](https://github.com/ocornut/imgui/blob/master/backends/imgui_impl_dx12.cpp).
- Общие соображения и пять рисков переезда — `X:\Witcher_research\D3D12_MIGRATION.md`; как W3 устроил D3D12 —
  `X:\Witcher_research\REPORT.md` §4; Frostbite, «FrameGraph: Extensible Rendering Architecture in Frostbite» (GDC 2017).
