# Дальние горы вокруг карты уровня (docs/terrain.md, «Дальние горы»; TODO.md, этап 16): карта высот кольца 64 × 64 км
# с шагом 32 м (2048², = мип 5 карты 1 м) с центром у карты — альпийские пики, как на картинке-ориентире
# (docs/reference/alpine_valley_sunset.png). Карта — в котловине: дно её долины продолжается за выходы, на юге его
# замыкает стена кара (исток ручья), на востоке и западе — горы от краёв карты, на севере — просвет: долина расширяется
# воронкой и опускается к будущему берегу моря (дно уходит ниже 0 м дальше COAST). У краёв карты рельеф в первые
# километры переходит из высот её края (стык с картой точно сводит движок: FarTerrain); горы — ridged multifractal
# Musgrave с искажением координат над подъёмом массива. Ледниковой эрозии пока нет — следующий пункт TODO (кары, троги,
# «рога»). Метры, координаты мира: x — восток, z — север; строка r — z = origin + extent − (r + 0,5)·cell, столбец c —
# x = origin + (c + 0,5)·cell (как у карты, docs/terrain.md). Внутри карты — её высоты (рельеф после эрозии движка,
# Textures\terrain\eroded\height.dds), средним по клетке 32 м.
# Запись: Textures\terrain\far_heightmap.dds (R16_UNORM), в базу — строка FarTerrain уровня с множителем и сдвигом высоты
# (метры = значение · множитель + сдвиг), углом и стороной (Tools/far_terrain.py; --no-db — без базы). Детерминирован
# (seed), ~30 с. Нужен numpy. Запускать из корня проекта:
#   python Tools/gen_far_terrain.py [--level Test] [--seed 11] [--preview файл.png] [--no-db]
import argparse
import math
import os
import sqlite3
import sys
import time

import numpy as np

import dds
import far_terrain
import preview
import terrain
from noise import Perlin, fbm, ridged, smoothstep

OUTPUT = os.path.join('Textures', 'terrain', 'far_heightmap.dds')
SIZE = 2048                 # текселей в стороне
CELL = 32.0                 # м на тексель — мип 5 карты 1 м
ORIGIN = -32768.0           # угол кольца (x и z), кратен узлу CDLOD 1024 м

# Котловина: дно долины карты за её выходами (x выходов на южном и северном краю, высота дна у них)
SOUTH_EXIT_X, NORTH_EXIT_X = 420.0, 280.0
FLOOR_AT_EXITS = (15.0, 2.0)        # высота дна у южного и северного выхода карты, м
BASIN_SOUTH = 4000.0                # дно котловины за южным краем карты до стены кара, м
FLOOR_SOUTH_RISE = 0.012            # дно к стене кара поднимается, м на м
FLOOR_HALF_WIDTH = (150.0, 600.0, 4500.0)   # полуширина дна: у карты, в котловине, в конце просвета, м
WALL = (2000.0, 400.0)              # стенка трога: ширина (от края дна до плеча) и высота плеча, м
VALLEY_MOUNTAINS = (300.0, 3500.0)  # от края дна: горы начинаются и выходят на полную высоту, м
VALLEY_SMOOTHING = 500.0            # профиль долины сглажен вдоль неё (без изломов на краях карты), м

# Просвет на север: расширяется воронкой, изгибы не закрывают вид; дно опускается до 0 м к COAST — будущий берег моря
OPENING_GROWTH = (2000.0, 25000.0)  # за северным краем карты: просвет начинает расширяться и выходит на полную ширину, м
OPENING_MEANDERS = [(700.0, 16000.0), (200.0, 5000.0)]    # (амплитуда, длина волны) изгибов просвета, м
COAST = 15000.0                     # за северным краем карты здесь дно опускается до 0 м, м
SEABED_DROP = 0.003                 # дальше берега дно уходит вниз, м на м

# Горы: период крупной октавы, высота пиков над массивом, подъём массива (седловины и долины между пиками)
RIDGE_PERIOD = 7000.0
PEAK_HEIGHT = 2400.0
MASSIF_HEIGHT = 700.0
MOUNTAINS_FROM_MAP = (300.0, 5000.0)       # от края карты: предгорья → горы, м
FOOTHILLS = 0.1             # доля высоты гор у самой карты
WARP = 2500.0               # искажение координат, м
NEAR_MAP_BLEND = 2000.0     # от края карты: высоты края карты → рельеф кольца, м
EDGE_BLUR = 0.35            # край карты, продолженный наружу, размыт с сигмой — эта доля расстояния от карты
OUTER_TAPER = (25000.0, 32768.0)   # к внешнему краю горы ниже (от центра карты, по наибольшей из |x|, |z|), м


def octaves(period, shortest=3.0 * CELL):
    """Октав шума с крупной волной period, м, пока волна не короче shortest: октава мельче двух текселей не передаётся
    сеткой карты и складывается в решётку бугорков (алиасинг)"""
    return int(math.floor(math.log2(period / shortest))) + 1


def ramp(x, width):
    """max(x, 0), сглаженный в полосе ±width (без излома)"""
    return np.where(x < -width, 0.0, np.where(x > width, x, (x + width) ** 2 / (4.0 * width)))


def gaussian_1d(values, sigma_cells):
    radius = int(math.ceil(3.0 * sigma_cells))
    kernel = np.exp(-0.5 * (np.arange(-radius, radius + 1) / sigma_cells) ** 2)
    return np.convolve(np.pad(values, radius, mode='edge'), kernel / kernel.sum(), mode='valid')


def box_blur(field, radius):
    """Три прохода прямоугольного фильтра радиуса radius клеток по обеим осям — гаусс с сигмой ≈ radius + 0,5; края —
    повтор"""
    width = 2 * radius + 1
    for _ in range(3):
        for axis in (0, 1):
            pad = [(0, 0), (0, 0)]
            pad[axis] = (radius + 1, radius)
            total = np.cumsum(np.pad(field, pad, mode='edge'), axis=axis)
            n = field.shape[axis]
            field = (np.take(total, np.arange(width, width + n), axis=axis) - np.take(total, np.arange(n), axis=axis)) / width
    return field


def valley_axis(z, map_size):
    """x оси долины для z мира: внутри карты — прямая между выходами, за ними — прямо, в просвете — с изгибами"""
    north = np.maximum(z - map_size, 0.0)
    exit_x = np.where(z >= map_size, NORTH_EXIT_X, np.where(z <= 0.0, SOUTH_EXIT_X,
                                                            SOUTH_EXIT_X + (NORTH_EXIT_X - SOUTH_EXIT_X) * z / map_size))
    growth = smoothstep(OPENING_GROWTH[0], OPENING_GROWTH[0] + 6000.0, north)
    meander = sum(a * (np.sin(2.0 * math.pi * north / length + k) - np.sin(k)) for k, (a, length) in enumerate(OPENING_MEANDERS))
    return exit_x + growth * meander


def distance_to_axis(x, z, axis, reach=10000.0, step=4):
    """Расстояние от узлов сетки (x — столбцы, z — строки) до ломаной оси (axis — x оси в строке): перебор строк оси
    в пределах reach через step строк; дальше — reach"""
    rows = len(z)
    X = np.broadcast_to(x[None, :], (rows, len(x)))
    best = np.full((rows, len(x)), reach * reach)
    span = int(reach / CELL)
    for offset in range(-span, span + 1, step):
        source = np.clip(np.arange(rows) + offset, 0, rows - 1)
        along = (z[source] - z)[:, None]
        across = X - axis[source][:, None]
        np.minimum(best, across * across + along * along, out=best)
    return np.sqrt(best)


def coarse_map(map_heights, map_cell):
    """Высоты карты средним по клетке кольца (32 м)"""
    block = int(round(CELL / map_cell))
    n = map_heights.shape[0] // block
    return map_heights[:n * block, :n * block].reshape(n, block, n, block).mean(axis=(1, 3))


def sample_coarse(coarse, map_size, x, z):
    """Билинейно по карте средних (клетка CELL, строка 0 — север), координаты зажаты в квадрат карты: снаружи — её край,
    продолженный наружу"""
    n = coarse.shape[0]
    c = np.clip(np.clip(x, 0.0, map_size) / CELL - 0.5, 0.0, n - 1.001)
    r = np.clip((map_size - np.clip(z, 0.0, map_size)) / CELL - 0.5, 0.0, n - 1.001)
    c0, r0 = c.astype(np.int64), r.astype(np.int64)
    fc, fr = c - c0, r - r0
    top = coarse[r0, c0] + (coarse[r0, c0 + 1] - coarse[r0, c0]) * fc
    bottom = coarse[r0 + 1, c0] + (coarse[r0 + 1, c0 + 1] - coarse[r0 + 1, c0]) * fc
    return top + (bottom - top) * fr


def blur_with_distance(field, from_map):
    """Поле, размытое тем сильнее, чем дальше от карты (сигма — EDGE_BLUR · расстояние): у карты — как есть, дальше без
    полос, которые тянет продолженный край, — смесь двух ближайших по сигме размытий"""
    radii = [0, 1, 2, 4, 8, 16, 32, 64]
    sigmas = [0.0] + [r + 0.5 for r in radii[1:]]
    level = np.interp(EDGE_BLUR * from_map / CELL, sigmas, np.arange(len(radii)))
    result = np.zeros_like(field)
    for k, radius in enumerate(radii):
        weight = np.maximum(1.0 - np.abs(level - k), 0.0)
        if weight.any():
            result += weight * (box_blur(field, radius) if radius > 0 else field)
    return result


def far_relief(map_heights, map_cell, rng):
    """Рельеф кольца в метрах (SIZE²): горы, котловина с просветом на север, у карты — переход из её краёв, внутри
    карты — её высоты"""
    map_size = map_heights.shape[0] * map_cell
    centers = ORIGIN + ( np.arange(SIZE) + 0.5 ) * CELL
    x = centers
    z = centers[::-1]       # строка 0 — север
    X, Z = np.meshgrid(x, z)
    noise = Perlin(rng)

    # Расстояние от квадрата карты и от оси долины
    dx = np.maximum(np.maximum(-X, X - map_size), 0.0)
    dz = np.maximum(np.maximum(-Z, Z - map_size), 0.0)
    from_map = np.hypot(dx, dz)
    from_valley = distance_to_axis(x, z, valley_axis(z, map_size))

    # Профиль долины вдоль z: дно (к стене кара вверх, в просвете — к берегу и ниже), его полуширина, замыкание на юге
    south = np.maximum(-z, 0.0)
    north = np.maximum(z - map_size, 0.0)
    floor = np.where(z < 0.0, FLOOR_AT_EXITS[0] + FLOOR_SOUTH_RISE * south,
                     np.where(z > map_size, FLOOR_AT_EXITS[1] * (1.0 - north / COAST) - SEABED_DROP * ramp(north - COAST, 3000.0),
                              FLOOR_AT_EXITS[0] + (FLOOR_AT_EXITS[1] - FLOOR_AT_EXITS[0]) * z / map_size))
    half_width = (FLOOR_HALF_WIDTH[0] + (FLOOR_HALF_WIDTH[1] - FLOOR_HALF_WIDTH[0]) * smoothstep(0.0, 2000.0, np.maximum(south, north))
                  + (FLOOR_HALF_WIDTH[2] - FLOOR_HALF_WIDTH[1]) * smoothstep(OPENING_GROWTH[0], OPENING_GROWTH[1], north))
    closed = smoothstep(BASIN_SOUTH - 1500.0, BASIN_SOUTH + 500.0, south)
    floor, half_width, closed = (gaussian_1d(v, VALLEY_SMOOTHING / CELL)[:, None] for v in (floor, half_width, closed))
    half_width = half_width * (1.0 + 0.25 * fbm(noise, X / 3000.0 + 17.1, Z / 3000.0 + 4.3, 2))

    # Трог — U-образные стенки до плеча; горы ниже у долины; на юге за котловиной долины нет — стена кара
    wall = smoothstep(half_width, half_width + WALL[0], from_valley)
    wall = wall + (1.0 - wall) * closed
    beside_valley = smoothstep(half_width + VALLEY_MOUNTAINS[0], half_width + VALLEY_MOUNTAINS[1], from_valley)
    beside_valley = beside_valley + (1.0 - beside_valley) * closed

    # Горы: подъём массива и ridged multifractal по искажённым координатам, острее возведением в степень; высота — от
    # долины и от карты, крупные пятна выше и ниже (не все пики одной высоты), к внешнему краю ниже
    envelope = beside_valley * (FOOTHILLS + (1.0 - FOOTHILLS) * smoothstep(MOUNTAINS_FROM_MAP[0], MOUNTAINS_FROM_MAP[1], from_map))
    outer = np.maximum(np.abs(X - map_size / 2.0), np.abs(Z - map_size / 2.0))
    envelope = envelope * (1.0 - 0.45 * smoothstep(OUTER_TAPER[0], OUTER_TAPER[1], outer))
    wx = X + WARP * fbm(noise, X / 9000.0 + 41.3, Z / 9000.0 + 13.7, 3)
    wz = Z + WARP * fbm(noise, X / 9000.0 + 71.9, Z / 9000.0 + 23.1, 3)
    peaks = ridged(noise, wx / RIDGE_PERIOD + 211.0, wz / RIDGE_PERIOD + 97.0, octaves(RIDGE_PERIOD))
    peaks = (peaks - peaks.min()) / (peaks.max() - peaks.min())
    peaks = peaks ** 1.4
    amplitude = PEAK_HEIGHT * (0.75 + 0.35 * fbm(noise, X / 22000.0 + 5.9, Z / 22000.0 + 2.2, 2)) * envelope
    detail = fbm(noise, X / 600.0 + 3.1, Z / 600.0 + 8.8, octaves(600.0))   # бугры на склонах ±25 м, на дне — вверх до 8 м:
    bumps = 25.0 * detail * wall + 4.0 * (detail + 1.0) * (1.0 - wall)   # дно у карты не уходит ниже уровня моря
    relief = floor + WALL[1] * wall + MASSIF_HEIGHT * envelope + peaks * amplitude + bumps

    # У карты — её край, продолженный наружу и размытый с расстоянием; за NEAR_MAP_BLEND — рельеф кольца; внутри карты —
    # её высоты средним по клетке кольца (их заменит движок мипом 5 итоговой карты)
    edge = blur_with_distance(sample_coarse(coarse_map(map_heights, map_cell), map_size, X, Z), from_map)
    relief = edge + (relief - edge) * smoothstep(0.0, NEAR_MAP_BLEND, from_map)
    return relief


def write_preview(path, height):
    """Отмывка рельефа и цвет по высоте: ниже 0 м — синее (будущее море), долина зелёная, скалы серые, выше 2000 м —
    снег"""
    t = np.clip(height / 3500.0, 0.0, 1.0)[..., None]
    low, rock, snow = np.array([0.30, 0.42, 0.22]), np.array([0.50, 0.47, 0.43]), np.array([0.95, 0.95, 0.97])
    color = np.where(t < 0.45, low + (rock - low) * (t / 0.45), rock + (snow - rock) * smoothstep(0.55, 0.65, t))
    color = np.where((height < 0.0)[..., None], np.array([0.25, 0.40, 0.60]), color)
    shade = preview.hillshade(height, CELL)[..., None]
    rgb = color * (0.25 + 0.75 * shade)
    preview.write_png(path, rgb)


def main():
    parser = argparse.ArgumentParser(description='Far mountains around the level map: heightmap of the ring')
    parser.add_argument('--level', default='Test')
    parser.add_argument('--seed', type=int, default=11)
    parser.add_argument('--preview', help='PNG with a hillshade')
    parser.add_argument('--no-db', action='store_true', help='do not write the FarTerrain row')
    args = parser.parse_args()

    db = sqlite3.connect('base.db3')
    row = db.execute('SELECT id FROM Levels WHERE name = ?', (args.level,)).fetchone()
    if row is None:
        sys.exit('error: level %s is not found' % args.level)
    loaded = terrain.load_level_terrain(db, row[0], '.')
    db.close()
    if loaded is None:
        sys.exit('error: level %s has no terrain' % args.level)
    map_heights, map_cell = loaded
    start = time.time()

    relief = far_relief(map_heights, map_cell, np.random.default_rng(args.seed))
    offset = math.floor(relief.min()) - 10.0
    multiplier = math.ceil(relief.max() - offset) + 10.0
    dds.write_r16(OUTPUT, (relief - offset) / multiplier)
    if args.preview:
        write_preview(args.preview, relief)
    print('far mountains written to %s in %.1f s: heights %.0f..%.0f m, height_multiplier %.0f, height_offset %.0f'
          % (OUTPUT, time.time() - start, relief.min(), relief.max(), multiplier, offset))
    if not args.no_db:
        far_terrain.store(args.level, os.path.relpath(OUTPUT, 'Textures'), multiplier, offset, (ORIGIN, ORIGIN), SIZE * CELL)


if __name__ == '__main__':
    main()
