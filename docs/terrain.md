# Террейн: CDLOD и материал слоёв

Рельеф уровня — `CDLODTerrain` (`Scene/Terrain/`): сетка по карте высот с плавными уровнями детализации. Поверхность
— `TerrainMaterial` и `Shaders/terrain.ps`: до четырёх слоёв (трава, осыпи, скала, снег) из фото-текстур, смешанных
по splat-карте и по высоте, с проекцией triplanar на склонах и вторым масштабом вдали.

## Геометрия (CDLOD)

Квадродерево над картой высот (Strugar 2009): корень покрывает весь террейн, лист — 32 текселя, диапазон каждого
уровня вдвое больше предыдущего. Узлы выбираются на CPU на каждый вид кадра (главная камера и каскады теней) по
расстоянию до AABB и по frustum; рисуются одним `DrawIndexedInstanced` патча 16 × 16. Вершина уровня L читает мип L
карты высот, к концу диапазона морфинг (`Shaders/cdlod.vs`) сдвигает её на сетку уровня L + 1 — уровни стыкуются без
трещин. Настройки — строка `Terrain` уровня (`heightmap` — имя в `Textures`, `splatmap` — файл, `height_multiplier`,
`height_offset`, `width_multiplier`). Карта высот и splat-карта лежат перевёрнутыми по Z: `uv.y = 1 − z / worldSize`,
то есть строка r картинки — z ≈ worldSize − r − 0,5, столбец — x (важно, когда место ищут по маске или карте высот).

## Слои

| Слой (`layer`) | Сейчас | Повтор, м | Источник |
|---|---|---|---|
| 0 — трава | `patchy_meadow1` | 4 | freepbr, `DownloadResources/patchy-meadow1-bl.zip` |
| 1 — осыпи | `rocky_terrain_03` | 20 (снято 90 × 90 м, сжато) | Poly Haven, `rocky_terrain_03_2k.gltf.zip` + disp |
| 2 — скала | `rock_face_03` | 2,7 | Poly Haven, `DownloadResources/polyhaven/rock_face_03_*` |
| 3 — снег | `snow_02` | 5 (снято 2 × 2 м) | Poly Haven, `snow_02_2k.gltf.zip` + disp |

Слой — строка `TerrainLayers` (`terrain`, `layer` 0…3 — канал splat-карты, `name`, `albedo`, `normal` — файлы от
`Textures\`, `tiling` — метров на повтор). Файлы слоя:

- `<имя>_albedo.dds` — RGB альбедо в sRGB (`R8G8B8A8_UNORM_SRGB`), A — высота для смешивания слоёв, 0…1;
- `<имя>_normal.dds` — RGB нормаль в соглашении DirectX (G — вниз по картинке), A — шероховатость.

При загрузке слои собираются в два `Texture2DArray` одного размера (по первому слою): альбедо — sRGB, выборка отдаёт
линейный цвет, мипы фильтруются в линейном, высота в альфе остаётся линейной; нормаль — UNORM. Цветовое пространство
задаёт назначение, а не метка формата файла (как `Textures.sRGB`). Мипы строятся без WIC: WIC масштабирует с
премультипликацией альфы, а в альфе — данные; если у всех файлов слоёв один размер и полная цепочка мипов (их пишут
`Tools/pack_terrain_layer.py` и генератор, альбедо — с фильтрацией в линейном), массив собирается из готовых мипов и
запуск их не строит. Нет файла — шахматка (альбедо) или плоская нормаль и строка в логе.

### Слой из фото-текстур

`Tools/pack_terrain_layer.py` — сценарий Blender (он читает JPG, PNG и EXR): альбедо как есть, нормаль из OpenGL в
DirectX, шероховатость из канала, высота из disp-карты, растянутая на 0…1 по 1-му и 99-му процентилю. С `--layer`
обновляет строку `TerrainLayers`. Файл внутри архива — `архив.zip:путь`. Нынешние слои, из корня проекта (`B` —
`"C:\Program Files\Blender Foundation\Blender 5.0\blender.exe" -b --factory-startup --python Tools/pack_terrain_layer.py --`,
`D` — `DownloadResources`):

```
B --name patchy_meadow1 --albedo D/patchy-meadow1-bl.zip:patchy-meadow1-bl/patchy-meadow1_albedo.png
  --normal D/patchy-meadow1-bl.zip:patchy-meadow1-bl/patchy-meadow1_normal-ogl.png
  --roughness D/patchy-meadow1-bl.zip:patchy-meadow1-bl/patchy-meadow1_roughness.png
  --height D/patchy-meadow1-bl.zip:patchy-meadow1-bl/patchy-meadow1_height.png --layer 0 --tiling 4
B --name rocky_terrain_03 --albedo D/rocky_terrain_03_2k.gltf.zip:textures/rocky_terrain_03_diff_2k.jpg
  --normal D/rocky_terrain_03_2k.gltf.zip:textures/rocky_terrain_03_nor_gl_2k.jpg
  --roughness D/rocky_terrain_03_2k.gltf.zip:textures/rocky_terrain_03_arm_2k.jpg:g
  --height D/polyhaven/rocky_terrain_03_disp_2k.exr --layer 1 --tiling 20
B --name rock_face_03 --albedo D/polyhaven/rock_face_03_diff_2k.jpg --normal D/polyhaven/rock_face_03_nor_gl_2k.jpg
  --roughness D/polyhaven/rock_face_03_arm_2k.jpg:g --height D/polyhaven/rock_face_03_disp_2k.exr --layer 2 --tiling 2.7
B --name snow_02 --albedo D/snow_02_2k.gltf.zip:textures/snow_02_diff_2k.jpg
  --normal D/snow_02_2k.gltf.zip:textures/snow_02_nor_gl_2k.jpg --roughness D/snow_02_2k.gltf.zip:textures/snow_02_rough_2k.jpg
  --height D/polyhaven/snow_02_disp_2k.exr --layer 3 --tiling 5
```

У ARM Poly Haven шероховатость — канал G (`:g`), у отдельной карты шероховатости — R (по умолчанию). Размер —
`--size` (2048). Нормаль DirectX — `--normal-convention dx`. Настоящий размер текстуры Poly Haven отдаёт API
(`https://api.polyhaven.com/info/<имя>`, поле `dimensions`, мм) — от него повтор.

Процедурные тестовые слои (без фото) по-прежнему пишет `python Tools/gen_terrain_textures.py` — вместе со splat-картой
и масками расстановки (альбедо — в sRGB, как у фото).

## Splat-карта и маски

`Tools/gen_terrain_textures.py` строит `Textures\terrain\splatmap.dds` по карте высот: снег на вершинах, скала на
крутых склонах, осыпи пятнами и у подножия скал, остальное трава. Там же маски плотности расстановки: `mask_grass`,
`mask_camomile` (пятна внутри травы), `mask_pebbles` (осыпи).

## Шейдер

1. **Нормаль рельефа** — по карте высот, мип под размер пикселя.
2. **Проекция** — сверху, на крутых склонах ещё вдоль X и Z (triplanar; нормали карты — по UDN), «Triplanar sharpness».
3. **Второй масштаб вдали** (distance resampling, как в материалах UE Landscape) — мелкий повтор (скала 2,7 м, снег)
   издалека складывается в сетку, поэтому дальше «Far blend start» (40 м) та же текстура читается ещё и в масштабе
   «Far texture scale» (×8) и к «Far blend end» (120 м) заменяет ближний. Обе выборки — только в полосе перехода.
4. **Смешивание по высоте** (Mishkinis): к весу слоя прибавляется его высота, виден слой с наибольшей суммой, переход —
   «Height blend»; так камни осыпей проступают сквозь траву.
5. Крупные пятна яркости по шуму (40 и 13 м), затем `evaluateLighting` ([lighting.md](lighting.md)).

Всё перечисленное в кавычках — ползунки окна террейна в GUI (Scene Objects → CDLOD terrain).

## Производительность

Release, 2880 × 1620, время GPU террейна (оба прохода цвета и prepass — отдельно, [passes.md](passes.md)):

| Камера | Процедурные слои 512² | Фото 2048² | Фото + второй масштаб |
|---|---|---|---|
| перед шарами (`512,96,212,12,0`) | 0,43 мс | 0,44 мс | 0,55 мс |
| обзор (`512,160,150,30,0`) | — | 0,79 мс | 0,95 мс |
| с холма вдаль (`300,140,100,12,40`) | — | 0,53 мс | 0,62 мс |

Второй масштаб стоит +0,1–0,15 мс не столько из-за двойной выборки в полосе (узкая полоса 60–90 м экономит только
0,05 мс), сколько из-за более детальных мипов вдали: хуже работает кэш текстур. Массивы 2048² × 4 слоя × 2 с мипами —
~170 МБ видеопамяти. Мипы лежат в файлах слоёв: с ними запуск дольше прежнего на ~0,2 с (чтение 8 × 21 МБ), без них —
на ~0,9 с в Release и ~4 с в Debug (мипы на CPU).

## Если что-то не так

1. **Шахматка** — не найден файл слоя (строка «Terrain material: can`t load» в логе).
2. **Выпуклости выглядят вмятинами** — нормаль в другом соглашении: пересоберите слой с `--normal-convention dx` или gl.
3. **Слой бледный или тёмный** — альбедо записано не в sRGB (старые файлы генератора до перехода); пересоберите.
4. **Слои не смешиваются по высоте** — высота слоя плоская (нет disp-карты): проверьте альфу `_albedo.dds`.
5. **Сетка повтора** — увеличьте повтор слоя (`tiling`) или «Far texture scale»; вблизи повтор виден, только если
   `tiling` мал для материала.

## Файлы

| Файл | Что там |
|---|---|
| `src/Engine/Graphics/Scene/Terrain/CDLODTerrain.h/.cpp` | квадродерево, выбор узлов, константы шейдера, свойства GUI |
| `src/Engine/Graphics/Scene/Terrain/TerrainMaterial.h/.cpp` | слои из `TerrainLayers`, массивы текстур, splat-карта |
| `Shaders/cdlod.sh`, `cdlod.vs`, `terrain.ps` | константы, вершинный шейдер с морфингом, материал |
| `Tools/pack_terrain_layer.py` | слой из фото-текстур (Blender) |
| `Tools/gen_heightmap.py`, `Tools/gen_terrain_textures.py`, `Tools/dds.py` | тестовая карта высот, процедурные слои, splat-карта и маски, запись DDS |

## Откуда подход

- F. Strugar, «Continuous Distance-Dependent Level of Detail for Rendering Heightmaps» (2009).
- A. Mishkinis, «Advanced Terrain Texture Splatting» (2013) — смешивание по высоте.
- Unreal Engine: материалы Landscape — слои по весам, triplanar на склонах, distance blend (второй масштаб вдали).
