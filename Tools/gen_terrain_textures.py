# Генерирует тестовые текстуры материала террейна: четыре слоя (трава, камни, скала, снег) и splat-карту.
# Каждый слой — две бесшовные текстуры 512×512 RGBA8 без сжатия (Textures\terrain\layers):
#   <слой>_albedo.dds — RGB альбедо в sRGB (R8G8B8A8_UNORM_SRGB), A высота (по ней слои смешиваются: камни проступают
#   сквозь траву);
#   <слой>_normal.dds — RGB нормаль (соглашение DirectX: G смотрит вдоль +v, вниз по картинке), A шероховатость.
# Splat-карта Textures\terrain\splatmap.dds (массив из двух RGBA: срез 0 — веса слоёв 0…3, срез 1 — 4…7; слой 4 —
# вторая трава) строится по карте высот Textures\terrain\heightmap.dds, картам эрозии рядом с ней (flow, wear,
# deposition, talus — их пишет Tools/gen_heightmap.py) и строке Terrain из base.db3: снег на вершинах и ниже на северных
# склонах, скала на крутых склонах и в промоинах, осыпи у подножий скал и в руслах, остальное трава — сочная на
# влажном, суше выше. Там же маски плотности для расстановки (Textures\terrain, значение в RGB): mask_grass — вес
# травы, mask_camomile — пятна цветов в траве, mask_pebbles — вес осыпей.
# Результат детерминирован. Нужен numpy.
# Запускать из корня проекта после Tools/gen_heightmap.py:
#   python Tools/gen_terrain_textures.py [--preview файл.png]
import os
import sqlite3
import sys

import numpy as np

import dds
import preview

LAYER_SIZE = 512
LAYERS_DIR = os.path.join('Textures', 'terrain', 'layers')
HEIGHTMAP = os.path.join('Textures', 'terrain', 'heightmap.dds')
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


def splatmap(rng, preview_path=None):
    height = dds.read_r16(HEIGHTMAP)
    size = height.shape[0]
    db = sqlite3.connect('base.db3')
    height_multiplier, texel_size = db.execute(
        "select height_multiplier, width_multiplier from Terrain where name = 'Terrain'").fetchone()
    db.close()
    # Карты эрозии (Tools/gen_heightmap.py); без них — нули: правила ниже работают по высоте и уклону
    maps = {}
    for name in ('flow', 'wear', 'deposition', 'talus'):
        path = os.path.join('Textures', 'terrain', name + '.dds')
        maps[name] = dds.read_r32f(path) if os.path.exists(path) else np.zeros_like(height)
        if not os.path.exists(path):
            print('no', path, '- run Tools/gen_heightmap.py first')
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

    if preview_path:
        # Слои цветом поверх отмывки: травы — два зелёных, осыпи — охра, скала — серая, снег — белый
        colors = np.array([(0.45, 0.50, 0.20), (0.62, 0.52, 0.36), (0.45, 0.44, 0.43), (0.95, 0.96, 0.98),
                           (0.22, 0.52, 0.18)])
        layer_weights = np.concatenate([slice0, slice1[..., :1]], axis=-1)
        rgb = layer_weights @ colors
        shade = preview.hillshade(height * height_multiplier, texel_size)[..., None]
        preview.write_png(preview_path, rgb * (0.3 + 0.7 * shade))


def write_mask(name, values):
    rgba = np.concatenate([np.repeat(values[..., None], 3, axis=-1), np.ones(values.shape + (1,))], axis=-1)
    dds.write_rgba8(os.path.join('Textures', 'terrain', name + '.dds'), rgba)


rng = np.random.default_rng(11)
grass(rng)
boulders(rng)
rock(rng)
snow(rng)
splatmap(rng, sys.argv[sys.argv.index('--preview') + 1] if '--preview' in sys.argv else None)
