# Генерирует процедурные тестовые слои материала террейна (без фото): трава, камни, скала, снег.
# Каждый слой — две бесшовные текстуры 512×512 RGBA8 без сжатия (Textures\terrain\layers):
#   <слой>_albedo.dds — RGB альбедо в sRGB (R8G8B8A8_UNORM_SRGB), A высота (по ней слои смешиваются: камни проступают
#   сквозь траву);
#   <слой>_normal.dds — RGB нормаль (соглашение DirectX: G смотрит вдоль +v, вниз по картинке), A шероховатость.
# Splat-карту и маски расстановки и леса считает движок при загрузке по итоговому рельефу и руслам (TerrainMasks,
# docs/terrain.md, «Слои и маски по рельефу»); здесь их больше нет. Результат детерминирован. Нужен numpy. Запускать из
# корня проекта:
#   python Tools/gen_terrain_textures.py
import os

import numpy as np

import dds

LAYER_SIZE = 512
LAYERS_DIR = os.path.join('Textures', 'terrain', 'layers')


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


rng = np.random.default_rng(11)
grass(rng)
boulders(rng)
rock(rng)
snow(rng)
