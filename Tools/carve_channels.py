"""Русла горных ручьёв — растровый слой правки рельефа (TerrainEdits.raster, как растровые Edit Layers в UE).

Русло режется по сети стока, а не по мокрым ячейкам: вода на склоне без русла течёт плёнкой шириной в десятки метров,
и прорезь по ней — широкое мелкое корыто, которое воду не собирает. Сценарий:
1. сток D8 по карте высот уровня с заполненными низинами (erosion.flow_accumulation, Priority-Flood);
2. расход каждой ячейки — сумма притока выше по течению, тот же приток, что даёт симуляция: доля «русла» по карте
   водосбора (WaterSimulation.flow_map, плавно от flow_start до flow_full в логарифме) × source_rate и источники-помощники
   (WaterSources) — поэтому русло там, где симуляция пустит воду;
3. ось русла — ячейки с расходом больше --min-discharge, вдоль течения сглажена (без лесенки D8);
4. профиль горного ручья: плоское дно шириной w = a·Q^0.5 (не уже --min-width: сетка 1 м уже не покажет), врез
   d = c·Q^0.4 и крутые борта (на метр вреза — --bank-slope м по горизонтали); в низинах, которые заполнит вода (озёра),
   не режет;
5. правка channels опускает рельеф, красит дно и борта галькой и убирает растительность (clear_foliage 1).
Слой каждый раз считается заново и заменяет прежний — опускание не копится. Подробно — docs/water.md, «Русла».

  python Tools/carve_channels.py [--level Test] [--min-discharge 0.005] [--width-coef 3] [--depth-coef 1] [--bank-slope 1]

Сообщения — ASCII (Windows PowerShell 5.1, cmd).
"""
import argparse
import math
import os
import sqlite3
import sys
import time

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dds  # noqa: E402
import erosion  # noqa: E402
import terrain  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DB = os.path.join(ROOT, 'base.db3')


def smoothstep(edge0, edge1, x):
    t = np.clip((x - edge0) / (edge1 - edge0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def inflow(db, level_id, flow, cell):
    """Приток ячеек, м3/с: как mainSources в Shaders/water_simulation.cs (без размытия — на сумму оно не влияет)"""
    row = db.execute('SELECT w.id, w.source_rate, w.flow_start, w.flow_full FROM Levels l '
                     'JOIN WaterSimulation w ON w.id = l.water_simulation WHERE l.id = ?', (level_id,)).fetchone()
    if row is None:
        sys.exit('level has no WaterSimulation row')
    simulation, source_rate, flow_start, flow_full = row
    rows, cols = flow.shape
    log_flow = np.log10(np.maximum(flow, 1.0))
    q = smoothstep(math.log10(flow_start), math.log10(flow_full), log_flow) * (source_rate / 1000.0)
    # Помощники — весь расход в ячейку центра (строка r <-> z = W - (r + 0.5) * cell)
    for x, z, rate in db.execute('SELECT x, z, rate FROM WaterSources WHERE water_simulation = ? AND enabled = 1', (simulation,)):
        c = int(np.clip(x / cell - 0.5 + 0.5, 0, cols - 1))
        r = int(np.clip((rows * cell - z) / cell - 0.5 + 0.5, 0, rows - 1))
        q[r, c] += rate / 1000.0
    return q, simulation


def read_texture(db, name):
    """Карта R32_FLOAT из таблицы Textures по имени"""
    texture = db.execute('SELECT file FROM Textures WHERE name = ?', (name,)).fetchone()
    if texture is None:
        sys.exit('texture %s is not found in Textures' % name)
    return dds.read_r32f(os.path.join(ROOT, 'Textures', texture[0].replace('\\', os.sep)))


def receivers(filled):
    """Получатель стока D8 каждой ячейки (индекс в плоском массиве; сам себе — сток за край или в никуда)"""
    rows, cols = filled.shape
    padded = np.pad(filled, 1, mode='edge')
    best = np.zeros((rows, cols))
    receiver = np.arange(rows * cols).reshape(rows, cols)
    for (dy, dx), d in zip(erosion.NEIGHBOURS, erosion.DISTANCES):
        neighbour = padded[1 + dy:1 + dy + rows, 1 + dx:1 + dx + cols]
        slope = (filled - neighbour) / d
        rr = np.clip(np.arange(rows)[:, None] + dy, 0, rows - 1)
        cc = np.clip(np.arange(cols)[None, :] + dx, 0, cols - 1)
        better = slope > best
        best = np.where(better, slope, best)
        receiver = np.where(better, rr * cols + cc, receiver)
    return receiver.ravel()


def discharge(filled, receiver, q):
    """Расход каждой ячейки, м3/с: свой приток и всё, что приходит сверху по D8"""
    total = q.ravel().astype(np.float64).tolist()
    recv = receiver.tolist()
    for i in np.argsort(filled.ravel(), kind='stable')[::-1].tolist():
        r = recv[i]
        if r != i:
            total[r] += total[i]
    return np.asarray(total).reshape(filled.shape)


def box_blur3(values):
    padded = np.pad(values, 1, mode='edge')
    n, m = values.shape
    return sum(padded[dy:dy + n, dx:dx + m] for dy in range(3) for dx in range(3)) / 9.0


def stream_nodes(flow, height, filled, receiver, args, cell):
    """Узлы оси: ячейки с расходом выше порога, положение (столбец, строка) — сглажено вдоль течения"""
    rows, cols = flow.shape
    stream = np.flatnonzero(flow.ravel() > args.min_discharge)
    index = {int(i): k for k, i in enumerate(stream)}
    position = np.stack([stream % cols + 0.5, stream // cols + 0.5], axis=1).astype(np.float64)
    down = np.array([index.get(int(receiver[i]), -1) if receiver[i] != i else -1 for i in stream])
    # Главный приток выше — с наибольшим расходом: сглаживание идёт вдоль него, боковые притоки на ось не тянут
    up = np.full(len(stream), -1)
    up_flow = np.zeros(len(stream))
    q = flow.ravel()[stream]
    for k, d in enumerate(down):
        if d >= 0 and q[k] > up_flow[d]:
            up_flow[d] = q[k]
            up[d] = k
    inner = (down >= 0) & (up >= 0)
    for _ in range(args.smooth):
        smoothed = position.copy()
        smoothed[inner] = 0.25 * position[up[inner]] + 0.5 * position[inner] + 0.25 * position[down[inner]]
        position = smoothed

    # Извилины: сдвиг поперёк течения шумом вдоль русла (длина пути от истока по главному притоку), амплитуда растёт
    # с расходом — у истока ручей почти прямой, ниже петляет. Узлы — от истоков вниз (по убыванию высоты)
    order = np.argsort(-filled.ravel()[stream], kind='stable')
    distance = np.zeros(len(stream))
    for k in order:
        d = down[k]
        if d >= 0 and up[d] == k:
            distance[d] = distance[k] + np.hypot(*(position[d] - position[k]))
    previous = np.where(up >= 0, up, np.arange(len(stream)))
    following = np.where(down >= 0, down, np.arange(len(stream)))
    tangent = position[following] - position[previous]
    tangent /= np.maximum(np.hypot(tangent[:, 0], tangent[:, 1]), 1e-6)[:, None]
    normal = np.stack([-tangent[:, 1], tangent[:, 0]], axis=1)
    phase = 2.0 * np.pi * distance / args.meander_length
    wave = np.sin(phase) + 0.5 * np.sin(2.3 * phase + 1.7)
    amplitude = np.minimum(args.meander_amplitude * np.sqrt(q / 0.1), args.meander_amplitude) / cell
    # Петляют ручьи на пологом дне долины, а на склоне идут почти по линии наибольшего уклона: ось, сдвинутая с ложбины
    # на склон, легла бы ниже бортом под уровень воды, и вода ушла бы из русла плёнкой по склону. Уклон — по рельефу,
    # сглаженному на несколько метров
    smooth = height
    for _ in range(3):
        smooth = box_blur3(smooth)
    gy, gx = np.gradient(smooth, cell)
    slope = np.hypot(gx, gy).ravel()[stream]
    amplitude *= np.clip(1.0 - slope / args.meander_max_slope, 0.0, 1.0)
    # Исток и устье на месте: к краям цепочки сдвиг гаснет
    edge = np.minimum(distance / args.meander_length, 1.0)
    edge[down < 0] = 0.0
    position = position + normal * (wave * amplitude * edge)[:, None]
    return position, down, q, order, stream


def sample_bilinear(values, x, y):
    """Значения массива в дробных (столбец, строка), билинейно"""
    rows, cols = values.shape
    x = np.clip(x, 0.0, cols - 1.001)
    y = np.clip(y, 0.0, rows - 1.001)
    x0 = x.astype(int)
    y0 = y.astype(int)
    fx = x - x0
    fy = y - y0
    top = values[y0, x0] * (1.0 - fx) + values[y0, x0 + 1] * fx
    bottom = values[y0 + 1, x0] * (1.0 - fx) + values[y0 + 1, x0 + 1] * fx
    return top * (1.0 - fy) + bottom * fy


def carve(height, filled, nodes, args, cell):
    """Опускание рельефа, м (< 0): по отрезку оси от узла к узлу ниже — профиль с дном и бортами"""
    position, down, q, order, _ = nodes
    rows, cols = height.shape
    result = np.zeros((rows, cols))
    half = 0.5 * np.maximum(args.width_coef * np.sqrt(q), args.min_width) / cell
    depth = args.depth_coef * np.power(q, 0.4)

    # Дно монотонно вниз по течению: не выше, чем дно любого узла выше минус --min-slope на метр. Иначе там, где ось
    # отошла от линии наибольшего уклона (извилина, сглаживание), в дне остались бы горбы, и вода вставала бы перед ними
    surface = sample_bilinear(height, position[:, 0] - 0.5, position[:, 1] - 0.5)
    bed = surface - depth
    for k in order:
        d = down[k]
        if d >= 0:
            step = np.hypot(*(position[d] - position[k])) * cell
            bed[d] = min(bed[d], bed[k] - args.min_slope * step)
    depth = surface - bed
    bank = depth * args.bank_slope / cell
    for k in range(len(q)):
        d = down[k]
        if d < 0:
            continue
        a, b = position[k], position[d]
        reach = max(half[k] + bank[k], half[d] + bank[d])
        c0 = int(max(math.floor(min(a[0], b[0]) - reach), 0))
        c1 = int(min(math.ceil(max(a[0], b[0]) + reach), cols - 1))
        r0 = int(max(math.floor(min(a[1], b[1]) - reach), 0))
        r1 = int(min(math.ceil(max(a[1], b[1]) + reach), rows - 1))
        cc, rr = np.meshgrid(np.arange(c0, c1 + 1) + 0.5, np.arange(r0, r1 + 1) + 0.5)
        segment = b - a
        length2 = max(float(segment @ segment), 1e-9)
        t = np.clip(((cc - a[0]) * segment[0] + (rr - a[1]) * segment[1]) / length2, 0.0, 1.0)
        distance = np.hypot(cc - (a[0] + t * segment[0]), rr - (a[1] + t * segment[1]))
        w = half[k] + (half[d] - half[k]) * t
        dep = depth[k] + (depth[d] - depth[k]) * t
        bw = np.maximum(bank[k] + (bank[d] - bank[k]) * t, 1e-3)
        profile = dep * (1.0 - smoothstep(0.0, 1.0, (distance - w) / bw))
        window = result[r0:r1 + 1, c0:c1 + 1]
        np.maximum(window, profile, out=window)
    # Низины, которые заполнит вода (озёра), не режутся: русло кончается у берега
    result[filled - height > args.lake_depth] = 0.0
    return -result


def channel_flow(flow_area, nodes, shape):
    """Карта водосбора для симуляции по оси русла: водосбор ячеек сети D8 переносится на сдвинутую извилинами ось
    (отрезки от узла к узлу — шагами по полметра). Иначе симуляция лила бы приток вдоль прежних прямых линий D8 рядом
    с руслом, и вода текла бы плёнкой мимо него"""
    position, down, q, order, stream = nodes
    rows, cols = shape
    result = flow_area.copy()
    area = flow_area.ravel()[stream]
    result.ravel()[stream] = 0.0
    for k in range(len(stream)):
        d = down[k] if down[k] >= 0 else k
        a, b = position[k], position[d]
        steps = max(int(np.ceil(np.hypot(*(b - a)) * 2.0)), 1)
        for s in range(steps + 1):
            p = a + (b - a) * (s / steps)
            c = int(np.clip(p[0], 0, cols - 1))
            r = int(np.clip(p[1], 0, rows - 1))
            result[r, c] = max(result[r, c], area[k])
    return result


def register(db, terrain_id, args):
    """Правка channels — растр, только опускание, после остальных правок (слой 1), дно галькой, без растительности"""
    db.execute('DELETE FROM TerrainEdits WHERE terrain = ? AND name = ?', (terrain_id, args.edit))
    db.execute('INSERT INTO TerrainEdits (terrain, layer, name, raise_terrain, lower_terrain, relative, smooth, clear_foliage, '
               'paint_layer, raster) VALUES (?, 1, ?, 0, 1, 0, 1, 1, ?, ?)',
               (terrain_id, args.edit, args.paint_layer, args.out.replace('/', '\\')))
    db.commit()


def main():
    parser = argparse.ArgumentParser(description='Mountain stream channels along the D8 network (raster terrain edit)')
    parser.add_argument('--level', default='Test')
    parser.add_argument('--out', default='terrain/channels.dds', help='raster file from Textures\\')
    parser.add_argument('--edit', default='channels', help='TerrainEdits name')
    parser.add_argument('--min-discharge', type=float, default=0.002, help='m3/s: a channel starts here')
    parser.add_argument('--width-coef', type=float, default=3.0, help='bed width w = a * Q^0.5, m')
    parser.add_argument('--min-width', type=float, default=1.5, help='narrowest bed, m')
    parser.add_argument('--depth-coef', type=float, default=1.0, help='incision d = c * Q^0.4, m')
    parser.add_argument('--bank-slope', type=float, default=1.0, help='bank: horizontal m per m of incision')
    parser.add_argument('--smooth', type=int, default=6, help='smoothing passes of the axis along the flow')
    parser.add_argument('--meander-length', type=float, default=35.0, help='meander wavelength along the stream, m')
    parser.add_argument('--meander-amplitude', type=float, default=2.5, help='lateral shift at Q >= 0.1 m3/s, m')
    parser.add_argument('--meander-max-slope', type=float, default=0.06, help='steeper terrain - no meanders (m per m)')
    parser.add_argument('--min-slope', type=float, default=0.002, help='bed drop per m downstream at least')
    parser.add_argument('--lake-depth', type=float, default=0.05, help='m: deeper filled depressions are lakes, not carved')
    parser.add_argument('--paint-layer', type=int, default=5, help='TerrainLayers.layer of the channel bed (pebbles)')
    parser.add_argument('--flow-map', default='terrain_flow', help='Textures name of the D8 catchment map (gen_heightmap.py)')
    parser.add_argument('--channel-flow', default='terrain_channel_flow', help='Textures name of the catchment along channels')
    parser.add_argument('--channel-flow-file', default='terrain/channel_flow.dds', help='its file from Textures\\')
    parser.add_argument('--source-radius', type=float, default=1.0, help='WaterSimulation.source_radius: inflow spread, cells')
    args = parser.parse_args()

    db = sqlite3.connect(DB)
    level = db.execute('SELECT id FROM Levels WHERE name = ?', (args.level,)).fetchone()
    if level is None:
        sys.exit('level %s is not found' % args.level)
    loaded = terrain.load_level_terrain(db, level[0], ROOT)
    if loaded is None:
        sys.exit('level %s has no terrain' % args.level)
    height, cell = loaded
    terrain_id = db.execute('SELECT terrain FROM Levels WHERE id = ?', (level[0],)).fetchone()[0]

    flow_area = read_texture(db, args.flow_map)
    if flow_area.shape != height.shape:
        sys.exit('flow map %s and heightmap differ in size' % args.flow_map)

    start = time.time()
    filled = erosion.fill_depressions(height)
    receiver = receivers(filled)
    sources, simulation = inflow(db, level[0], flow_area, cell)
    flow = discharge(filled, receiver, sources)
    print('flow routing: %.1f s, largest discharge %.3f m3/s' % (time.time() - start, flow.max()))

    nodes = stream_nodes(flow, height, filled, receiver, args, cell)
    lowered = carve(height, filled, nodes, args, cell)
    print('channel nodes %d (Q > %.3f m3/s), widest bed %.2f m, deepest %.2f m, carved cells %d' %
          (len(nodes[2]), args.min_discharge, max(args.min_width, args.width_coef * math.sqrt(flow.max())),
           -lowered.min(), (lowered < -0.01).sum()))
    dds.write_r32f(os.path.join(ROOT, 'Textures', args.out.replace('\\', os.sep)), lowered.astype(np.float32))
    register(db, terrain_id, args)
    print('edit %s: raster %s' % (args.edit, args.out))

    # Симуляция льёт приток по оси русла, узко: вся вода источников — в русле
    dds.write_r32f(os.path.join(ROOT, 'Textures', args.channel_flow_file.replace('\\', os.sep)),
                   channel_flow(flow_area, nodes, height.shape).astype(np.float32))
    db.execute('DELETE FROM Textures WHERE name = ?', (args.channel_flow,))
    db.execute('INSERT INTO Textures (name, file, generate_mipmap, sRGB) VALUES (?, ?, 0, 0)',
               (args.channel_flow, args.channel_flow_file.replace('/', '\\')))
    db.execute('UPDATE WaterSimulation SET flow_map = ?, source_radius = ? WHERE id = ?',
               (args.channel_flow, args.source_radius, simulation))
    db.commit()
    print('water simulation: flow map %s, source radius %g' % (args.channel_flow, args.source_radius))


if __name__ == '__main__':
    main()
