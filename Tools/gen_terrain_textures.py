# Генерирует тестовые текстуры материала террейна: четыре слоя (трава, камни, скала, снег) и splat-карту.
# Каждый слой — две бесшовные текстуры 512×512 RGBA8 без сжатия (Textures\terrain\layers):
#   <слой>_albedo.dds — RGB альбедо в sRGB (R8G8B8A8_UNORM_SRGB), A высота (по ней слои смешиваются: камни проступают
#   сквозь траву);
#   <слой>_normal.dds — RGB нормаль (соглашение DirectX: G смотрит вдоль +v, вниз по картинке), A шероховатость.
# Splat-карта Textures\terrain\splatmap.dds (массив из двух RGBA: срез 0 — веса слоёв 0…3, срез 1 — 4…7; слой 4 —
# вторая трава) строится по рельефу после эрозии и картам эрозии (Textures\terrain\eroded: height, flow, wear,
# deposition, talus — их пишет движок при загрузке, TerrainErosion) и строке Terrain из base.db3: снег на вершинах и ниже на северных
# склонах, скала на крутых склонах и в промоинах, осыпи у подножий скал и в руслах, остальное трава — сочная на
# влажном, суше выше. Там же маски плотности для расстановки (Textures\terrain, значение в RGB): mask_grass — вес
# травы, mask_camomile — пятна цветов в траве, mask_pebbles — вес осыпей.
# И маски леса по типам (forest_masks, как в Valley Benchmark): mask_forest_spruce — ель выше и на северных склонах,
# mask_forest_pine — сосна на средних сухих склонах, mask_forest_birch — берёза во влажных низинах, mask_shrubs_riparian —
# кусты вдоль ручьёв, mask_shrubs_slope — стланик над границей леса и кусты на сухих прогалинах; лес — не на скале,
# снегу, осыпях и в руслах, ниже границы леса, на дне долины — луга с редкими рощами.
# Результат детерминирован (у масок леса свой генератор: остальное от них не меняется). Нужен numpy.
# Splat-карта и маски — такие же входные файлы, как любая текстура: их можно поправить в любой программе. Запускать из
# корня проекта после первого запуска движка с новой картой высот (он пишет каталог eroded):
#   python Tools/gen_terrain_textures.py [--preview файл.png] [--forest-preview файл.png]
import os
import sqlite3
import sys

import numpy as np

import dds
import preview

LAYER_SIZE = 512
LAYERS_DIR = os.path.join('Textures', 'terrain', 'layers')
ERODED = os.path.join('Textures', 'terrain', 'eroded')
SPLATMAP = os.path.join('Textures', 'terrain', 'splatmap.dds')


def smoothstep(a, b, x):
    t = np.clip((x - a) / (b - a), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def lattice_coords(size, period):
    # Для каждого пикселя: номер ячейки решётки period×period и положение внутри неё (0…1)
    coord = np.arange(size) * period / size
    cell = np.floor(coord).astype(int)
    return cell, coord - cell


def value_noise(size, period, rng):
    # Бесшовный value noise: случайные значения в узлах решётки, сглаженная интерполяция
    lattice = rng.random((period, period))
    cell, t = lattice_coords(size, period)
    t = t * t * (3.0 - 2.0 * t)
    k0, k1 = cell % period, (cell + 1) % period
    tx, ty = t[None, :], t[:, None]
    top = lattice[k0][:, k0] + (lattice[k0][:, k1] - lattice[k0][:, k0]) * tx
    bottom = lattice[k1][:, k0] + (lattice[k1][:, k1] - lattice[k1][:, k0]) * tx
    return top + (bottom - top) * ty


def fbm(size, octaves, rng):
    # Сумма октав value noise: octaves — пары (период решётки, вес); результат нормирован в [0, 1]
    total = sum(value_noise(size, period, rng) * weight for period, weight in octaves)
    return (total - total.min()) / (total.max() - total.min())


def stones(size, period, radius, rng):
    # Бесшовные камни: в каждой ячейке решётки period×period один купол со случайным центром и радиусом.
    # Возвращает высоту (0 — между камнями, 1 — вершина) и оттенок камня под пикселем
    cx, cy = rng.random((period, period)), rng.random((period, period))
    r = rng.uniform(radius[0], radius[1], (period, period))
    tone = rng.uniform(0.75, 1.1, (period, period))
    cell, local = lattice_coords(size, period)
    height = np.zeros((size, size))
    shade = np.ones((size, size))
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            iy = (cell[:, None] + dy) % period
            ix = (cell[None, :] + dx) % period
            ddx = dx + cx[iy, ix] - local[None, :]
            ddy = dy + cy[iy, ix] - local[:, None]
            h = np.sqrt(np.clip(1.0 - (ddx * ddx + ddy * ddy) / r[iy, ix] ** 2, 0.0, None))
            higher = h > height
            height = np.where(higher, h, height)
            shade = np.where(higher, tone[iy, ix], shade)
    return height, shade


def normals(height, depth):
    # Нормаль по бесшовной карте высот; depth — перепад высоты 0…1 в текселях
    k = depth * 0.5
    nx = -(np.roll(height, -1, axis=1) - np.roll(height, 1, axis=1)) * k
    ny = -(np.roll(height, -1, axis=0) - np.roll(height, 1, axis=0)) * k
    n = np.stack([nx, ny, np.ones_like(height)], axis=-1)
    return n / np.linalg.norm(n, axis=-1, keepdims=True)


def blur(values, radius, passes=3):
    # Три прохода box-фильтра ≈ гауссово размытие; за краем карты повторяется крайний тексель
    size = values.shape[0]
    for _ in range(passes):
        padded = np.pad(values, radius, mode='edge')
        values = sum(padded[radius:radius + size, shift:shift + size] for shift in range(2 * radius + 1)) / (2 * radius + 1)
        padded = np.pad(values, radius, mode='edge')
        values = sum(padded[shift:shift + size, radius:radius + size] for shift in range(2 * radius + 1)) / (2 * radius + 1)
    return values


def dilate(values, radius):
    # Максимум по квадрату (2·radius + 1)²: тонкие линии (русла по карте стока) расширяются, а не гаснут, как при размытии
    size = values.shape[0]
    padded = np.pad(values, radius, mode='edge')
    values = np.maximum.reduce([padded[radius:radius + size, shift:shift + size] for shift in range(2 * radius + 1)])
    padded = np.pad(values, radius, mode='edge')
    return np.maximum.reduce([padded[shift:shift + size, radius:radius + size] for shift in range(2 * radius + 1)])


def mix(a, b, t):
    return np.asarray(a) + (np.asarray(b) - np.asarray(a)) * t[..., None]


def write_layer(name, albedo, height, roughness, depth):
    # Цвет задан линейным, в файле — sRGB, как у фото (слои из фото — Tools/pack_terrain_layer.py)
    albedo_height = np.concatenate([dds.linear_to_srgb(albedo), height[..., None]], axis=-1)
    normal_roughness = np.concatenate([normals(height, depth) * 0.5 + 0.5, roughness[..., None]], axis=-1)
    dds.write_rgba8(os.path.join(LAYERS_DIR, name + '_albedo.dds'), albedo_height, srgb=True, mips=True)
    dds.write_rgba8(os.path.join(LAYERS_DIR, name + '_normal.dds'), normal_roughness, mips=True)


def grass(rng):
    patches = fbm(LAYER_SIZE, [(4, 0.5), (8, 0.3), (16, 0.2)], rng)
    blades = fbm(LAYER_SIZE, [(64, 0.3), (128, 0.4), (256, 0.3)], rng)
    height = 0.15 + 0.6 * blades + 0.25 * patches
    albedo = mix((0.10, 0.20, 0.05), (0.32, 0.42, 0.12), patches) * (0.7 + 0.5 * blades)[..., None]
    write_layer('grass', albedo, height, 0.85 + 0.1 * blades, depth=3.0)


def boulders(rng):
    dirt = fbm(LAYER_SIZE, [(16, 0.4), (32, 0.3), (64, 0.2), (128, 0.1)], rng)
    detail = fbm(LAYER_SIZE, [(64, 0.5), (128, 0.5)], rng)
    big, big_shade = stones(LAYER_SIZE, 8, (0.3, 0.5), rng)
    small, small_shade = stones(LAYER_SIZE, 32, (0.25, 0.45), rng)

    dirt_height = dirt * 0.3
    big_height = np.where(big > 0.0, big * 0.85 + detail * 0.15, 0.0)
    small_height = np.where(small > 0.0, small * 0.45 + detail * 0.1, 0.0)
    height = np.maximum.reduce([dirt_height, big_height, small_height])

    is_dirt = height == dirt_height
    shade = np.where(height == big_height, big_shade, small_shade) * (0.85 + 0.3 * detail)
    stone_color = np.asarray((0.42, 0.40, 0.37)) * shade[..., None]
    dirt_color = mix((0.20, 0.15, 0.10), (0.36, 0.28, 0.19), dirt)
    albedo = np.where(is_dirt[..., None], dirt_color, stone_color)
    write_layer('boulders', albedo, height, np.where(is_dirt, 0.95, 0.65), depth=14.0)


def rock(rng):
    warp = fbm(LAYER_SIZE, [(4, 0.6), (8, 0.4)], rng)
    ridges = fbm(LAYER_SIZE, [(8, 0.4), (16, 0.3), (32, 0.2), (64, 0.1)], rng)
    fine = fbm(LAYER_SIZE, [(128, 0.6), (256, 0.4)], rng)
    # Слои породы вдоль строк картинки: на склонах triplanar кладёт их горизонтально
    v = (np.arange(LAYER_SIZE) / LAYER_SIZE)[:, None]
    band = 0.5 + 0.5 * np.sin(2.0 * np.pi * (v * 12.0 + warp * 1.5))
    ridged = 1.0 - np.abs(2.0 * ridges - 1.0)
    height = 0.5 * ridged + 0.3 * band + 0.2 * fine

    tint = 0.9 + 0.2 * band
    crack = 0.45 + 0.55 * smoothstep(0.05, 0.3, ridged)
    albedo = mix((0.26, 0.25, 0.23), (0.52, 0.48, 0.43), height)
    albedo *= np.stack([tint * crack, crack, crack / tint], axis=-1)
    write_layer('rock', albedo, height, 0.7 + 0.15 * fine, depth=16.0)


def snow(rng):
    dunes = fbm(LAYER_SIZE, [(4, 0.5), (8, 0.3), (16, 0.15), (32, 0.05)], rng)
    grain = fbm(LAYER_SIZE, [(128, 0.5), (256, 0.5)], rng)
    height = 0.3 + 0.6 * dunes + 0.1 * grain
    albedo = np.asarray((0.82, 0.86, 0.92)) * (0.92 + 0.06 * dunes + 0.02 * grain)[..., None]
    write_layer('snow', albedo, height, 0.45 + 0.2 * grain, depth=4.0)


def splatmap(rng, preview_path=None, forest_preview_path=None):
    db = sqlite3.connect('base.db3')
    height_multiplier, height_offset, texel_size = db.execute(
        "select height_multiplier, height_offset, width_multiplier from Terrain where name = 'Terrain'").fetchone()
    db.close()
    # Рельеф после эрозии движка, нормированный (0…1 — как исходная карта: правила ниже — в долях высоты)
    height_path = os.path.join(ERODED, 'height.dds')
    if not os.path.exists(height_path):
        sys.exit('no %s - run the engine once with the terrain heightmap (it writes the eroded terrain)' % height_path)
    height = (dds.read_r32f(height_path) - height_offset) / height_multiplier
    size = height.shape[0]
    # Карты эрозии движка; без них — нули: правила ниже работают по высоте и уклону
    maps = {}
    for name in ('flow', 'wear', 'deposition', 'talus'):
        path = os.path.join(ERODED, name + '.dds')
        maps[name] = dds.read_r32f(path) if os.path.exists(path) else np.zeros_like(height)
        if not os.path.exists(path):
            print('no', path, '- run the engine once (it writes the erosion maps)')
    cell_area = texel_size * texel_size
    log_flow = np.log10(np.maximum(maps['flow'], cell_area))

    # Уклон в градусах по центральным разностям (за краем карты повторяется крайний тексель), сглаженный: у мелких
    # октав рельефа уклон меняется через несколько текселей, и без сглаживания скала и камни рассыпаются штрихами
    padded = np.pad(height, 1, mode='edge')
    scale = height_multiplier / (2.0 * texel_size)
    dx = (padded[1:-1, 2:] - padded[1:-1, :-2]) * scale
    dz = (padded[2:, 1:-1] - padded[:-2, 1:-1]) * scale   # вниз по картинке — на юг
    slope = blur(np.degrees(np.arctan(np.hypot(dx, dz))), radius=3)
    # Склон смотрит на север, если к югу (вниз по картинке) он выше: там снег лежит ниже
    north = blur(np.clip(dz / np.maximum(np.hypot(dx, dz), 1e-6), 0.0, 1.0) * smoothstep(5.0, 15.0, slope), radius=3)

    noise = fbm(size, [(8, 0.35), (16, 0.3), (32, 0.2), (64, 0.1), (128, 0.05)], rng) - 0.5
    patches = fbm(size, [(16, 0.5), (32, 0.3), (64, 0.2)], rng)

    # Скала: крутые склоны и коренная порода там, где вода сняла грунт (промоины на средних уклонах)
    bedrock = smoothstep(1.0, 3.0, blur(maps['wear'], radius=2)) * smoothstep(14.0, 24.0, slope)
    rock_w = np.maximum(smoothstep(27.0, 37.0, slope + noise * 10.0), 0.8 * bedrock)
    snow_w = smoothstep(0.80, 0.88, height + noise * 0.08 + 0.06 * north) * (1.0 - 0.7 * rock_w)
    # Осыпи: осыпь у подножий скал, крутоватые склоны, галька в руслах на склонах (на дне долины — луг)
    talus = smoothstep(0.15, 0.8, blur(maps['talus'], radius=2))
    channel = smoothstep(3.3, 4.0, blur(log_flow, radius=1)) * smoothstep(6.0, 12.0, slope)
    boulders_w = np.maximum.reduce([talus, 0.7 * smoothstep(18.0, 25.0, slope + noise * 6.0), 0.8 * channel,
                                    0.6 * smoothstep(0.62, 0.72, patches) * smoothstep(8.0, 16.0, slope)])
    boulders_w *= (1.0 - rock_w) * (1.0 - snow_w)
    grass_w = np.maximum(1.0 - rock_w - snow_w - boulders_w, 0.0)

    weights = np.stack([grass_w, boulders_w, rock_w, snow_w], axis=-1)
    weights /= weights.sum(axis=-1, keepdims=True)

    # Влажность — топографический индекс влажности TWI = ln(a / tan β) (Beven, Kirkby 1979; a — водосбор на метр
    # ширины склона): велик там, куда стекает много воды и где ровно — низины, дно долины, русла; плюс конусы выноса
    twi = np.log(np.maximum(maps['flow'], cell_area) / texel_size / np.maximum(np.hypot(dx, dz), 1e-3))
    moisture = blur(np.maximum(smoothstep(4.0, 8.0, twi), smoothstep(0.1, 0.5, maps['deposition'])), radius=6)

    # Маски плотности расстановки в тех же координатах, что карта высот: трава — на обеих травах, одуванчики —
    # пятнами в траве, гуще на влажном, камни — на осыпях
    flowers = smoothstep(0.62, 0.72, fbm(size, [(32, 0.5), (64, 0.3), (128, 0.2)], rng) + 0.15 * moisture)
    write_mask('mask_grass', weights[..., 0])
    write_mask('mask_camomile', weights[..., 0] * flowers)
    write_mask('mask_pebbles', weights[..., 1])

    # Две травы: сочная wispy_grass_meadow (слой 4) — на влажном, суше patchy_meadow1 (слой 0), граница — пятнами
    # по шуму (~30–130 м); мягкий край дорабатывает смешивание по высоте в шейдере
    meadow = smoothstep(0.4, 0.6, 0.6 * moisture + 0.4 * fbm(size, [(8, 0.5), (16, 0.3), (32, 0.2)], rng))
    grass = weights[..., 0].copy()
    slice0 = weights.copy()
    slice0[..., 0] = grass * (1.0 - meadow)
    slice1 = np.zeros_like(weights)
    slice1[..., 0] = grass * meadow
    dds.write_rgba8(SPLATMAP, np.stack([slice0, slice1]))

    forest_masks(height, height_multiplier, texel_size, slope, north, moisture, log_flow, rock_w, snow_w, boulders_w,
                 forest_preview_path)

    if preview_path:
        # Слои цветом поверх отмывки: травы — два зелёных, осыпи — охра, скала — серая, снег — белый
        colors = np.array([(0.45, 0.50, 0.20), (0.62, 0.52, 0.36), (0.45, 0.44, 0.43), (0.95, 0.96, 0.98),
                           (0.22, 0.52, 0.18)])
        layer_weights = np.concatenate([slice0, slice1[..., :1]], axis=-1)
        rgb = layer_weights @ colors
        shade = preview.hillshade(height * height_multiplier, texel_size)[..., None]
        preview.write_png(preview_path, rgb * (0.3 + 0.7 * shade))


def forest_masks(height, height_multiplier, texel_size, slope, north, moisture, log_flow, rock_w, snow_w, boulders_w,
                 preview_path=None):
    # Маски леса по типам (как в Valley Benchmark: ель, сосна, берёза, кусты) — плотность 0…1 в тех же координатах,
    # что карта высот. Свой генератор случайных чисел: splat-карта и маски травы от них не меняются
    rng = np.random.default_rng(23)
    size = height.shape[0]
    noise = fbm(size, [(8, 0.4), (16, 0.3), (32, 0.2), (64, 0.1)], rng) - 0.5

    # Где лес растёт вообще: не на скале, снегу и осыпях, не круче ~35°, ниже границы леса (на северных склонах она
    # ниже), не в руслах — там кусты. Граница и опушки рваные — шум в несколько десятков метров
    treeline = 1.0 - smoothstep(0.72, 0.79, height + 0.08 * noise + 0.04 * north)
    # На осыпях лес реже, но не исчезает: осыпь включает и просто крутоватые склоны (boulders_w в splatmap)
    ground = (1.0 - rock_w) * (1.0 - snow_w) * (1.0 - 0.8 * smoothstep(0.45, 0.85, boulders_w))
    steep = 1.0 - smoothstep(30.0, 38.0, slope + 6.0 * noise)
    # Русло — сток больше ~4000 м² (log10 3,6), шириной в несколько метров
    stream = smoothstep(3.6, 4.2, blur(dilate(log_flow, 1), radius=1))
    # Дно долины — луга: ровное и низкое почти без леса, рощи только пятнами
    floor = smoothstep(0.32, 0.22, height) * smoothstep(9.0, 4.0, slope)
    clearings = smoothstep(0.30, 0.46, fbm(size, [(16, 0.5), (32, 0.3), (64, 0.2)], rng) + 0.13 - 0.42 * floor)
    forest = ground * steep * treeline * (1.0 - stream) * clearings

    # Типы — по месту: ель выше и на северных склонах, сосна — на средних сухих склонах, обращённых к солнцу, берёза —
    # во влажных низинах. У каждого типа свой шум древостоев (~30–130 м): пояса по высоте не лентами, а мозаикой —
    # соседний тип заходит пятнами. Степень 3 даёт чистые древостои с узкой смешанной полосой между ними
    # Предпочтения мягкие: на средних высотах ель и сосна конкурируют, у подножий — сосна и берёза, решает шум
    spruce = (0.35 + 0.65 * smoothstep(0.25, 0.55, height + 0.10 * noise)) * (0.7 + 0.3 * north)
    pine = smoothstep(8.0, 20.0, slope) * (1.1 - moisture) * (1.0 - 0.6 * north) * smoothstep(0.66, 0.45, height)
    birch = smoothstep(0.2, 0.7, moisture) * smoothstep(0.45, 0.25, height) * smoothstep(20.0, 8.0, slope)
    stands = [0.4 + 1.2 * fbm(size, [(8, 0.5), (16, 0.3), (32, 0.2)], rng) for _ in range(3)]
    types = np.stack([spruce * stands[0], pine * stands[1], birch * stands[2]], axis=-1) + 1e-4
    types = types ** 3
    types /= types.sum(axis=-1, keepdims=True)
    spruce_m, pine_m, birch_m = (forest * types[..., i] for i in range(3))

    # Кусты: у ручьёв (ольха, ива) — полоса вдоль русел, шире самого русла; на склонах (можжевельник) — над границей
    # леса до скал и снега и на прогалинах сухих склонов
    # Берега ручьёв с водосбором от ~5000 м² (log10 3,7): полоса в несколько метров по обе стороны
    banks = smoothstep(3.5, 3.9, blur(dilate(log_flow, 3), radius=2)) * ground * smoothstep(0.78, 0.70, height)
    shrubs_riparian = banks * (1.0 - 0.7 * stream) * smoothstep(0.30, 0.50, fbm(size, [(32, 0.5), (64, 0.5)], rng) + 0.2 * moisture)
    # Стланик — узкий пояс над границей леса пятнами
    krummholz = smoothstep(0.73, 0.78, height + 0.08 * noise + 0.04 * north) * smoothstep(0.84, 0.79, height)
    patches = smoothstep(0.40, 0.60, fbm(size, [(32, 0.5), (64, 0.3), (128, 0.2)], rng))
    dry_gaps = (1.0 - clearings) * smoothstep(12.0, 20.0, slope) * (1.0 - moisture) * (1.0 - floor)
    shrubs_slope = np.maximum(krummholz * patches, 0.5 * dry_gaps) * ground * steep * (1.0 - stream)

    masks = [('mask_forest_spruce', spruce_m), ('mask_forest_pine', pine_m), ('mask_forest_birch', birch_m),
             ('mask_shrubs_riparian', shrubs_riparian), ('mask_shrubs_slope', shrubs_slope)]
    for name, values in masks:
        write_mask(name, values)
        print('  %s: %.1f%% of the map covered (mean density %.2f)' % (name, 100.0 * np.mean(values > 0.5), np.mean(values)))

    if preview_path:
        # Ель — тёмно-зелёная, сосна — рыжеватая, берёза — светло-зелёная, кусты у ручьёв — бирюзовые, на склонах —
        # лиловые поверх отмывки рельефа
        colors = np.array([(0.05, 0.25, 0.10), (0.60, 0.42, 0.15), (0.65, 0.85, 0.30), (0.15, 0.65, 0.65), (0.55, 0.35, 0.60)])
        weights = np.stack([m for _, m in masks], axis=-1)
        cover = np.clip(weights.sum(axis=-1, keepdims=True), 0.0, 1.0)
        rgb = (weights @ colors) / np.maximum(weights.sum(axis=-1, keepdims=True), 1e-6)
        base = np.array([0.82, 0.80, 0.74]) * (1.0 - snow_w[..., None]) + snow_w[..., None]
        rgb = rgb * cover + base * (1.0 - cover)
        shade = preview.hillshade(height * height_multiplier, texel_size)[..., None]
        preview.write_png(preview_path, rgb * (0.35 + 0.65 * shade))


def write_mask(name, values):
    rgba = np.concatenate([np.repeat(values[..., None], 3, axis=-1), np.ones(values.shape + (1,))], axis=-1)
    dds.write_rgba8(os.path.join('Textures', 'terrain', name + '.dds'), rgba)


def option(name):
    return sys.argv[sys.argv.index(name) + 1] if name in sys.argv else None


rng = np.random.default_rng(11)
grass(rng)
boulders(rng)
rock(rng)
snow(rng)
splatmap(rng, option('--preview'), option('--forest-preview'))
