# Террейн: CDLOD и материал слоёв

Рельеф уровня — `CDLODTerrain` (`Scene/Terrain/`): сетка по карте высот с плавными уровнями детализации. Поверхность
— `TerrainMaterial` и `Shaders/terrain.ps`: до восьми слоёв из фото-текстур (сейчас пять: две травы, осыпи, скала, снег),
смешанных по splat-карте и по высоте, с проекцией triplanar на склонах и вторым масштабом вдали.

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
| 3 — снег | `crusted_snow2` | 4 | FreePBR, `DownloadResources/Crusted_snow2-bl.zip` |
| 4 — вторая трава | `wispy_grass_meadow` | 4 | FreePBR, `DownloadResources/whispy-grass-meadow-bl.zip` |

Две травы лежат вперемешку крупными пятнами (см. «Splat-карта и маски»), чтобы луг издалека не был одного тона.
Прежний снег `snow_02` (следы на снегу) заменён настом, его файлы остались в `Textures\terrain\layers\`. Лицензия
FreePBR запрещает распространять сами файлы — архивы и собранные DDS в git не попадают (`DownloadResources/` и
`Textures/` вне git).

Слой — строка `TerrainLayers` (`terrain`, `layer` 0…7 — канал `layer % 4` среза `layer / 4` splat-карты, `name`,
`albedo`, `normal` — файлы от `Textures\`, `tiling` — метров на повтор). Файлы слоя:

- `<имя>_albedo.dds` — RGB альбедо в sRGB (`R8G8B8A8_UNORM_SRGB`), A — высота для смешивания слоёв, 0…1;
- `<имя>_normal.dds` — RGB нормаль в соглашении DirectX (G — вниз по картинке), A — шероховатость.

При загрузке слои собираются в два `Texture2DArray` одного размера (по первому слою), по срезу на слой до наибольшего
описанного `layer` (пропуск в номерах — заглушка): альбедо — sRGB, выборка отдаёт
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
B --name crusted_snow2 --albedo D/Crusted_snow2-bl.zip:Crusted_snow2-bl/Crusted_snow2_Base_Color.png
  --normal D/Crusted_snow2-bl.zip:Crusted_snow2-bl/Crusted_snow2_Normal-ogl.png
  --roughness D/Crusted_snow2-bl.zip:Crusted_snow2-bl/Crusted_snow2_Roughness.png
  --height D/Crusted_snow2-bl.zip:Crusted_snow2-bl/Crusted_snow2_Height.png --layer 3 --tiling 4
B --name wispy_grass_meadow --albedo D/whispy-grass-meadow-bl.zip:whispy-grass-meadow-bl/wispy-grass-meadow_albedo.png
  --normal D/whispy-grass-meadow-bl.zip:whispy-grass-meadow-bl/wispy-grass-meadow_normal-ogl.png
  --roughness D/whispy-grass-meadow-bl.zip:whispy-grass-meadow-bl/wispy-grass-meadow_roughness.png
  --height D/whispy-grass-meadow-bl.zip:whispy-grass-meadow-bl/wispy-grass-meadow_height.png --layer 4 --tiling 4
```

У ARM Poly Haven шероховатость — канал G (`:g`), у отдельной карты шероховатости — R (по умолчанию). Размер —
`--size` (2048). Нормаль DirectX — `--normal-convention dx`. Настоящий размер текстуры Poly Haven отдаёт API
(`https://api.polyhaven.com/info/<имя>`, поле `dimensions`, мм) — от него повтор.

Процедурные тестовые слои (без фото) по-прежнему пишет `python Tools/gen_terrain_textures.py` — вместе со splat-картой
и масками расстановки (альбедо — в sRGB, как у фото).

## Рельеф: долина и эрозия

Карту высот строит `python Tools/gen_heightmap.py` (около двух минут на 1024 × 1024, numpy): горная долина, размытая
водой, — как рельеф Valley Benchmark из World Machine. Размер мира и высота берутся из строки `Terrain`
(`width_multiplier` — метров на тексель, `height_multiplier` — метров на 1,0 карты), все размеры рельефа — в метрах,
поэтому для мира в 4–8 км сценарий просто запускается заново. Результат детерминирован (`--seed`).

1. **Исходная долина.**
   - Дно петляет с юга на север через место тестовых моделей (x ≈ 470–545, z ≈ 240–280) и понижается к северу на
     ~16 % высоты карты, чтобы вода уходила с карты, а не стояла озером.
   - Поперечный профиль U-образный, как у ледниковой долины: плоское дно ±45 м, склон — S-кривая до плеча долины.
   - Выше и дальше от оси — хребты: ridged multifractal (Musgrave, «Texturing and Modeling») на градиентном шуме
     Перлина (quintic fade) с искажением координат (domain warp), чтобы гребни и край дна не шли по прямым.
   - Мелкие бугры (морена) — fBm с амплитудой 2 % высоты.
2. **Гидравлическая эрозия** — `erosion.hydraulic_erosion`, капли по H. T. Beyer (2015), как у S. Lague:
   - миллион капель пакетами по 65 536 (внутри шага капли друг друга не видят, как на GPU; сложение в карту —
     `np.bincount`);
   - капля идёт по билинейному градиенту с инерцией 0,3 до 80 шагов по текселю, ёмкость переноса —
     max(уклон, 0,01) · скорость · вода;
   - недостающий грунт размывается кистью радиуса 5, лишний откладывается в четыре угла ячейки;
   - размыв за шаг не глубже перепада, и ёмкость · скорость размыва должна быть ≪ 1 — иначе каждая капля срезает свой
     путь до ровного, и гребни становятся плато (первая проба);
   - капли начинаются по карте дождя (шум ~250 м, от 0,25 до 1), поэтому промоины глубже в одних местах и мельче
     в других, а не одинаковая «гребёнка» по всем склонам;
   - карта на время расчёта продолжена за край на кисть: капли уходят с карты, без нетронутой рамки.
3. **Осыпание** — `erosion.thermal_erosion` (Musgrave, Kolb, Mace 1989): где уклон к соседу круче 38° (угол
   естественного откоса), четверть излишка сползает вниз; 40 проходов. Так у подножий крутых склонов копится осыпь.
4. **Водосбор** — `erosion.flow_accumulation`: низины заполняются Priority-Flood с малым уклоном (Barnes, Lehman,
   Mulla 2014), затем вода стекает в соседа с наибольшим уклоном (D8, O'Callaghan и Mark 1984); площадь водосбора
   накапливается от высоких клеток к низким.
5. **Запись.** Карта высот — `Textures\terrain\heightmap.dds` (R16_UNORM, 0…1: итог приводится к размаху карты). Рядом —
   карты эрозии R32_FLOAT, имена как у выходов World Machine и Gaea:

   | Файл | Что | Единицы |
   |---|---|---|
   | `flow.dds` | площадь водосбора | м² |
   | `wear.dds` | понижение водой (итоговое, не сумма за шаги) | м |
   | `deposition.dds` | повышение отложениями воды | м |
   | `talus.dds` | осыпь | м |

   Движок их не читает: по ним строятся splat-карта и маски (ниже), позже — русла ручьёв и маски леса.
6. **Модели на новую землю.** Перед записью сценарий читает прежнюю карту и сдвигает по высоте экземпляры
   `LevelModels` уровней с этим террейном на разницу самых высоких точек земли под моделью (круг по масштабу
   экземпляра, не меньше метра). Не по центру: шар, лежавший на склоне, на ровном месте иначе повис бы. `--keep-models`
   — не трогать (тогда в следующий раз «прежней» будет уже новая карта).

Параметры: `--size`, `--droplets` (1 000 000), `--no-erosion`, `--keep-models`, `--preview файл.png` — отмывка
рельефа с руслами (водосбор > 2000 м²), чтобы подобрать рельеф без запуска движка (PNG пишет `Tools/preview.py`).

## Splat-карта и маски

Splat-карта — массив из двух RGBA, как weightmap в UE Landscape (там веса тоже пакуются по четыре слоя в текстуру):
срез 0 — веса слоёв 0…3, срез 1 — 4…7. `Tools/dds.py` пишет его DDS-массивом (`write_rgba8` с массивом срезов).
Прежний файл из одной картинки движок читает как раньше — второй срез нулевой.

`Tools/gen_terrain_textures.py` (после `gen_heightmap.py`) строит `Textures\terrain\splatmap.dds` по карте высот и картам
эрозии (без них — только по высоте и уклону):
- **скала** — крутые склоны (27–37° с шумом) и коренная порода в промоинах: понижение водой больше 1–3 м на склонах
  круче 14–24°;
- **снег** — выше 0,80–0,88 высоты карты, на северных склонах ниже (по направлению нормали), на скале меньше;
- **осыпи** — осыпь `talus`, склоны 18–25°, галька в руслах на склонах (водосбор больше ~2000 м² при уклоне от
  6–12°; на дне долины — луг), пятна на пологих склонах;
- **трава** — остальное, двух видов по влажности: сочная `wispy_grass_meadow` (слой 4) — на влажном, суше
  `patchy_meadow1` (слой 0); граница — пятнами по шуму (~30–130 м), мягкий край дорабатывает смешивание по высоте.
  Влажность — топографический индекс TWI = ln(a / tan β) (Beven, Kirkby 1979; a — водосбор на метр ширины склона):
  велик там, куда стекает много воды и где ровно, — дно долины, низины, русла; плюс конусы выноса (отложения).

Там же маски плотности расстановки: `mask_grass` (обе травы), `mask_camomile` (пятна цветов в траве, гуще на влажном),
`mask_pebbles` (осыпи). `--preview файл.png` — слои цветом поверх отмывки рельефа.

## Шейдер

1. **Нормаль рельефа** — по карте высот, мип под размер пикселя.
2. **Четыре самых весомых слоя пикселя** — из восьми весов двух срезов (слои сверх `g_layerCount` — описанных в
   `TerrainLayers` — веса не получают). Текстуры читаются только у них: в одной точке больше трёх-четырёх слоёв не бывает,
   а массив из восьми выборок занял бы регистры и снизил занятость GPU.
3. **Проекция** — сверху, на крутых склонах ещё вдоль X и Z (triplanar; нормали карты — по UDN), «Triplanar sharpness».
4. **Второй масштаб вдали** (distance resampling, как в материалах UE Landscape) — мелкий повтор (скала 2,7 м, снег)
   издалека складывается в сетку, поэтому дальше «Far blend start» (40 м) та же текстура читается ещё и в масштабе
   «Far texture scale» (×8) и к «Far blend end» (120 м) заменяет ближний. Обе выборки — только в полосе перехода.
5. **Смешивание по высоте** (Mishkinis): к весу слоя прибавляется его высота, виден слой с наибольшей суммой, переход —
   «Height blend»; так камни осыпей проступают сквозь траву.
6. Крупные пятна яркости по шуму (40 и 13 м), затем `evaluateLighting` ([lighting.md](lighting.md)).

Всё перечисленное в кавычках — ползунки окна террейна в GUI (Scene Objects → CDLOD terrain).

## Производительность

Release, 1920 × 1080, время GPU террейна (оба прохода цвета и prepass — отдельно, [passes.md](passes.md)). Таблица —
по прежнему рельефу из холмов; с долиной камеры там же оказываются на других высотах. Долина с эрозией: стартовая
камера (`512,90,150,20,0`) — кадр 1,80 мс, террейн 0,72; у шаров (`500,20,215,4,5`) — кадр 4,83 мс, террейн 0,48,
трава 1,08, глубина теней 2,0 (дно долины всё в траве, см. пункт о тени расстановки в TODO).

| Камера | Процедурные слои 512² | Фото 2048² | Фото + второй масштаб |
|---|---|---|---|
| перед шарами (`512,96,212,12,0`) | 0,43 мс | 0,44 мс | 0,55 мс |
| обзор (`512,160,150,30,0`) | — | 0,79 мс | 0,95 мс |
| с холма вдаль (`300,140,100,12,40`) | — | 0,53 мс | 0,62 мс |

С пятью слоями (две травы, splat-массив, выбор четырёх слоёв) обзор — 1,05 мс вместо 0,96 при четырёх, у земли
(`567,58.1,212,8,35`) — 0,62 вместо 0,60.

Второй масштаб стоит +0,1–0,15 мс не столько из-за двойной выборки в полосе (узкая полоса 60–90 м экономит только
0,05 мс), сколько из-за более детальных мипов вдали: хуже работает кэш текстур. Массивы 2048² × 5 слоёв × 2 с мипами —
~210 МБ видеопамяти (слой — ~42 МБ). Мипы лежат в файлах слоёв: с ними запуск дольше прежнего на ~0,2 с (чтение 8 × 21 МБ), без них —
на ~0,9 с в Release и ~4 с в Debug (мипы на CPU).

## Если что-то не так

1. **Шахматка** — не найден файл слоя (строка «Terrain material: can`t load» в логе).
2. **Выпуклости выглядят вмятинами** — нормаль в другом соглашении: пересоберите слой с `--normal-convention dx` или gl.
3. **Слой бледный или тёмный** — альбедо записано не в sRGB (старые файлы генератора до перехода); пересоберите.
4. **Слои не смешиваются по высоте** — высота слоя плоская (нет disp-карты): проверьте альфу `_albedo.dds`.
5. **Слой 4…7 не виден** — splat-карта старая, из одной картинки (второй срез — нули): перегенерируйте её
   `python Tools/gen_terrain_textures.py`.
6. **Сетка повтора** — увеличьте повтор слоя (`tiling`) или «Far texture scale»; вблизи повтор виден, только если
   `tiling` мал для материала.

## Файлы

| Файл | Что там |
|---|---|
| `src/Engine/Graphics/Scene/Terrain/CDLODTerrain.h/.cpp` | квадродерево, выбор узлов, константы шейдера, свойства GUI |
| `src/Engine/Graphics/Scene/Terrain/TerrainMaterial.h/.cpp` | слои из `TerrainLayers`, массивы текстур, splat-карта |
| `Shaders/cdlod.sh`, `cdlod.vs`, `terrain.ps` | константы, вершинный шейдер с морфингом, материал |
| `Tools/pack_terrain_layer.py` | слой из фото-текстур (Blender) |
| `Tools/gen_heightmap.py` | долина с эрозией: карта высот, карты эрозии, модели уровня на новую землю |
| `Tools/erosion.py` | гидравлическая эрозия каплями, осыпание, водосбор D8 с заполнением низин |
| `Tools/gen_terrain_textures.py` | процедурные слои, splat-карта и маски расстановки по картам эрозии |
| `Tools/dds.py`, `Tools/preview.py` | запись и чтение DDS (RGBA8, R16, R32_FLOAT), превью PNG и отмывка рельефа |

## Откуда подход

- F. Strugar, «Continuous Distance-Dependent Level of Detail for Rendering Heightmaps» (2009).
- A. Mishkinis, «Advanced Terrain Texture Splatting» (2013) — смешивание по высоте.
- Unreal Engine: материалы Landscape — слои по весам, triplanar на склонах, distance blend (второй масштаб вдали).
- H. T. Beyer, «Implementation of a method for hydraulic erosion» (2015); S. Lague, «Coding Adventure: Hydraulic
  Erosion» — капли.
- F. K. Musgrave, C. E. Kolb, R. S. Mace, «The synthesis and rendering of eroded fractal terrains» (SIGGRAPH 1989) —
  осыпание; F. K. Musgrave, «Texturing and Modeling», гл. 16 — ridged multifractal.
- R. Barnes, C. Lehman, D. Mulla, «Priority-flood: An optimal depression-filling and watershed-labeling algorithm»
  (2014); J. O'Callaghan, D. Mark (1984) — сток D8; K. Beven, M. Kirkby (1979) — индекс влажности TWI.
- World Machine, Gaea — имена выходов эрозии (Flow, Wear, Deposition, Talus).
