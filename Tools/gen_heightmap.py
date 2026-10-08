# Исходная карта высот террейна: горная долина без эрозии (docs/terrain.md, «Рельеф: долина и эрозия») — один из
# источников карты, как World Machine или Gaea. Эрозию (капли и осыпание), водосбор, карты эрозии и пересадку моделей на
# новую землю делает движок при загрузке — первая ступень конвейера рельефа и воды (TerrainErosion, строка
# TerrainErosion у террейна). Размер мира и высота — из строки Terrain в base.db3 (width_multiplier — метров на тексель,
# height_multiplier — метров на 1,0 карты), все размеры рельефа — в метрах: дно петляет с юга на север и понижается к
# северу, U-образные склоны поднимаются к хребтам (ridged multifractal на шуме Перлина с искажением координат).
# Запись: Textures\terrain\heightmap.dds (R16_UNORM, 0…1). Результат детерминирован (seed), секунды. Нужен numpy.
# Запускать из корня проекта:
#   python Tools/gen_heightmap.py [--size 1024] [--seed 7] [--preview файл.png]
import argparse
import math
import os
import sqlite3
import time

import numpy as np

import dds
import erosion
import preview

TERRAIN_DIR = os.path.join('Textures', 'terrain')
HEIGHTMAP = os.path.join(TERRAIN_DIR, 'heightmap.dds')

# Долина, метры (мир 1024 м, высота 100 м): ось петляет вокруг x = W/2 и проходит через тестовые модели (x ≈ 470–545,
# z ≈ 240–280), вдоль взгляда стартовой камеры (512, 160, 150, на север)
AXIS_POINT = (505.0, 260.0)              # (x, z), через который проходит ось
AXIS_WAVES = [(110.0, 1300.0), (30.0, 420.0)]   # (амплитуда, длина волны) петель
FLOOR_HALF_WIDTH = 45.0                  # плоское дно ±, м
FLOOR_SOUTH, FLOOR_DROP = 0.24, 0.16     # высота дна на юге и понижение к северу — доли высоты карты
WALL_WIDTH, WALL_HEIGHT = 330.0, 0.5     # склон от края дна до плеча долины, м; высота плеча — доля высоты карты
RIDGE_PERIOD, RIDGE_HEIGHT = 380.0, 0.45  # хребты: период крупной октавы, м; высота — доля высоты карты
WARP = 60.0                               # искажение координат, м


class Perlin:
    """Градиентный шум Перлина (quintic fade, Perlin 2002), значения ≈ −1…1, векторно по массивам координат"""

    def __init__(self, rng, period=256):
        angle = rng.uniform(0.0, 2.0 * math.pi, (period, period))
        self.gx, self.gy, self.period = np.cos(angle), np.sin(angle), period

    def __call__(self, x, y):
        ix, iy = np.floor(x).astype(np.int64), np.floor(y).astype(np.int64)
        fx, fy = x - ix, y - iy
        p = self.period

        def corner(ox, oy):
            gx = self.gx[(iy + oy) % p, (ix + ox) % p]
            gy = self.gy[(iy + oy) % p, (ix + ox) % p]
            return gx * (fx - ox) + gy * (fy - oy)

        u = fx * fx * fx * (fx * (fx * 6 - 15) + 10)
        v = fy * fy * fy * (fy * (fy * 6 - 15) + 10)
        top = corner(0, 0) + (corner(1, 0) - corner(0, 0)) * u
        bottom = corner(0, 1) + (corner(1, 1) - corner(0, 1)) * u
        return (top + (bottom - top) * v) * math.sqrt(2.0)


def fbm(noise, x, y, octaves, lacunarity=2.0, gain=0.5):
    total, amplitude, norm = 0.0, 1.0, 0.0
    for _ in range(octaves):
        total = total + noise(x, y) * amplitude
        norm += amplitude
        x, y = x * lacunarity, y * lacunarity
        amplitude *= gain
    return total / norm


def ridged(noise, x, y, octaves, lacunarity=2.0, gain=2.0, offset=1.0, h=1.0):
    """Ridged multifractal (F. K. Musgrave, «Texturing and Modeling», гл. 16): острые гребни из |шума|, каждая
    следующая октава сильнее на гребнях. Результат нормирован примерно в 0…1"""
    signal = (offset - np.abs(noise(x, y))) ** 2
    total, norm, weight = signal, 1.0, signal
    for octave in range(1, octaves):
        x, y = x * lacunarity, y * lacunarity
        weight = np.clip(signal * gain, 0.0, 1.0)
        signal = (offset - np.abs(noise(x, y))) ** 2 * weight
        amplitude = lacunarity ** (-octave * h)
        total = total + signal * amplitude
        norm += amplitude
    return total / norm


def smoothstep(a, b, x):
    t = np.clip((x - a) / (b - a), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def valley(size, cell, height_range, rng):
    """Исходный рельеф в метрах: строка r — z = W − (r + 0,5)·cell, столбец c — x = (c + 0,5)·cell (docs/terrain.md)"""
    world = size * cell
    x = (np.arange(size) + 0.5) * cell
    z = world - (np.arange(size) + 0.5) * cell
    X, Z = np.meshgrid(x, z)
    noise = Perlin(rng)

    # Искажение координат: хребты и край дна не идут по прямым
    wx = X + WARP * fbm(noise, X / 300.0 + 11.3, Z / 300.0 + 7.1, 3)
    wz = Z + WARP * fbm(noise, X / 300.0 + 31.7, Z / 300.0 + 3.9, 3)

    # Ось долины: сумма волн, сдвинутая так, чтобы пройти через AXIS_POINT
    def waves(zz):
        return sum(a * np.sin(2.0 * math.pi * zz / length + k) for k, (a, length) in enumerate(AXIS_WAVES))
    axis = world / 2.0 + waves(wz) - (world / 2.0 + waves(AXIS_POINT[1]) - AXIS_POINT[0])
    distance = np.abs(wx - axis)

    half_width = FLOOR_HALF_WIDTH * (1.0 + 0.35 * fbm(noise, X / 200.0 + 5.5, Z / 200.0, 2))
    t = np.clip((distance - half_width) / WALL_WIDTH, 0.0, 1.0)
    wall = t * t * (3.0 - 2.0 * t)
    floor = FLOOR_SOUTH - FLOOR_DROP * Z / world
    mountains = ridged(noise, wx / RIDGE_PERIOD + 101.0, wz / RIDGE_PERIOD + 57.0, 6)
    mountains = (mountains - mountains.min()) / (mountains.max() - mountains.min())
    mountain_weight = smoothstep(half_width + 30.0, half_width + 320.0, distance)
    detail = fbm(noise, X / 70.0 + 3.3, Z / 70.0 + 9.9, 4)   # бугры, морена на дне

    h = floor + WALL_HEIGHT * wall + RIDGE_HEIGHT * mountains * mountain_weight + 0.02 * detail
    h = (h - h.min()) / (h.max() - h.min())
    return h * height_range


def write_preview(path, height, flow, cell):
    """Отмывка рельефа, цвет по высоте, русла (водосбор больше 2000 м²) — синим"""
    t = (height - height.min()) / (height.max() - height.min())
    low, mid, high = np.array([0.36, 0.45, 0.25]), np.array([0.52, 0.45, 0.36]), np.array([0.95, 0.95, 0.97])
    color = np.where(t[..., None] < 0.55, low + (mid - low) * (t[..., None] / 0.55),
                     mid + (high - mid) * np.clip((t[..., None] - 0.55) / 0.45, 0.0, 1.0))
    shade = preview.hillshade(height, cell)[..., None]
    rgb = color * (0.25 + 0.75 * shade)
    channel = np.clip((np.log10(np.maximum(flow, 1.0)) - 3.3) / 1.5, 0.0, 1.0)[..., None]
    rgb = rgb + (np.array([0.15, 0.35, 0.85]) - rgb) * channel * 0.85
    preview.write_png(path, rgb)


def main():
    parser = argparse.ArgumentParser(description='Source terrain heightmap: mountain valley without erosion')
    parser.add_argument('--size', type=int, default=1024)
    parser.add_argument('--preview', help='PNG with a hillshade and water channels')
    parser.add_argument('--seed', type=int, default=7)
    args = parser.parse_args()

    db = sqlite3.connect('base.db3')
    height_range, cell = db.execute(
        "SELECT height_multiplier, width_multiplier FROM Terrain WHERE name = 'Terrain'").fetchone()
    db.close()
    rng = np.random.default_rng(args.seed)
    start = time.time()

    height = valley(args.size, cell, height_range, rng)
    normalized = (height - height.min()) / (height.max() - height.min())
    dds.write_r16(HEIGHTMAP, normalized)
    if args.preview:
        flow, _ = erosion.flow_accumulation(normalized * height_range, cell)
        write_preview(args.preview, normalized * height_range, flow, cell)
    print('source valley written in %.1f s; erosion - the engine at load (TerrainErosion)' % (time.time() - start))


if __name__ == '__main__':
    main()
