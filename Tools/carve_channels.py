"""Русла по расходу воды из симуляции — растровый слой правки рельефа (TerrainEdits.raster, как растровые Edit Layers в UE).

Цикл: движок выгружает установившийся расход ячеек (консольная команда water discharge, WaterSimulation) → сценарий
режет русла там, где течёт вода, по гидравлической геометрии (Leopold, Maddock 1953): ширина w = a·Q^0.5, глубина
d = c·Q^0.4 → правка channels опускает рельеф и красит дно галькой → при следующей загрузке вода собирается в руслах.
Итерация повторяет выгрузку уже с руслами: поток сосредотачивается. Слой каждый раз считается заново от расхода и
заменяет прежний — опускание не копится (правка ложится на рельеф без неё самой). Подробно — docs/water.md, «Русла».

  python Tools/carve_channels.py [--iterations 2] [--config Release] [--level Test]
  python Tools/carve_channels.py --iterations 0 --discharge Textures/terrain/discharge.dds   (только резка по готовой карте)

Расход потока — наибольший по соседям 3 x 3 (поток в две-три ячейки делит расход). Профиль русла — d·cos^2(pi/2 · r / R),
R = w/2 + bank: дно и пологие берега без ступеньки. Сообщения — ASCII (Windows PowerShell 5.1, cmd).
"""
import argparse
import os
import sqlite3
import subprocess
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dds  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DB = os.path.join(ROOT, 'base.db3')
ENGINE = [sys.executable, os.path.join(ROOT, 'Tools', 'engine.py')]


def run_engine(args, discharge_path):
    """Старт движка, выгрузка расхода, штатный выход"""
    subprocess.run(ENGINE + ['start', '--config', args.config, '--nogui', '--nowind', '--level', args.level], check=True, cwd=ROOT)
    try:
        subprocess.run(ENGINE + ['water', 'discharge', os.path.relpath(discharge_path, ROOT)], check=True, cwd=ROOT)
    finally:
        subprocess.run(ENGINE + ['stop'], cwd=ROOT)


def cell_size(db, level):
    row = db.execute('SELECT t.id, t.width_multiplier FROM Levels l JOIN Terrain t ON t.id = l.terrain WHERE l.name = ?',
                     (level,)).fetchone()
    if row is None:
        sys.exit('level %s has no terrain' % level)
    return row[0], row[1]


def max_filter3(values):
    padded = np.pad(values, 1, mode='edge')
    result = values.copy()
    n, m = values.shape
    for dy in range(3):
        for dx in range(3):
            result = np.maximum(result, padded[dy:dy + n, dx:dx + m])
    return result


def box_blur3(values):
    padded = np.pad(values, 1, mode='edge')
    n, m = values.shape
    return sum(padded[dy:dy + n, dx:dx + m] for dy in range(3) for dx in range(3)) / 9.0


def carve(discharge, cell, args):
    """Опускание рельефа, м (< 0), по расходу ячеек, м3/с"""
    q = max_filter3(discharge)
    channel = q > args.min_discharge
    half = np.where(channel, 0.5 * args.width_coef * np.power(np.maximum(q, 0.0), 0.5), 0.0)
    depth = np.where(channel, args.depth_coef * np.power(np.maximum(q, 0.0), 0.4), 0.0)
    reach = np.where(channel, half + args.bank, 0.0)
    radius = int(np.ceil(reach.max() / cell)) if channel.any() else 0
    n, m = discharge.shape
    result = np.zeros_like(discharge)
    padded_reach = np.pad(reach, radius)
    padded_depth = np.pad(depth, radius)
    for dy in range(-radius, radius + 1):
        for dx in range(-radius, radius + 1):
            r = np.hypot(dx, dy) * cell
            # Вклад русла из ячейки (y + dy, x + dx) в ячейку (y, x)
            source_reach = padded_reach[radius + dy:radius + dy + n, radius + dx:radius + dx + m]
            source_depth = padded_depth[radius + dy:radius + dy + n, radius + dx:radius + dx + m]
            inside = r < source_reach
            profile = np.where(inside, np.cos(0.5 * np.pi * r / np.maximum(source_reach, 1e-6)) ** 2, 0.0)
            result = np.maximum(result, source_depth * profile)
    result = box_blur3(result)
    print('channel cells %d (Q > %.3f m3/s), widest %.2f m, deepest %.2f m, carved cells %d' %
          (channel.sum(), args.min_discharge, 2.0 * half.max(), depth.max(), (result > 0.01).sum()))
    return -result


def register(db, terrain, args):
    """Правка channels — растр, только опускание, после остальных правок (слой 1), дно галькой"""
    db.execute('DELETE FROM TerrainEdits WHERE terrain = ? AND name = ?', (terrain, args.edit))
    db.execute('INSERT INTO TerrainEdits (terrain, layer, name, raise_terrain, lower_terrain, relative, smooth, clear_foliage, '
               'paint_layer, raster) VALUES (?, 1, ?, 0, 1, 0, 1, 0, ?, ?)',
               (terrain, args.edit, args.paint_layer, args.out.replace('/', '\\')))
    db.commit()


def main():
    parser = argparse.ArgumentParser(description='Channels from simulated water discharge (raster terrain edit)')
    parser.add_argument('--iterations', type=int, default=2, help='engine runs (export, carve); 0 - carve from --discharge')
    parser.add_argument('--discharge', default=os.path.join('Textures', 'terrain', 'discharge.dds'))
    parser.add_argument('--out', default='terrain/channels.dds', help='raster file from Textures\\')
    parser.add_argument('--edit', default='channels', help='TerrainEdits name')
    parser.add_argument('--level', default='Test')
    parser.add_argument('--config', default='Release')
    parser.add_argument('--min-discharge', type=float, default=0.005, help='m3/s: a channel starts here')
    parser.add_argument('--width-coef', type=float, default=3.0, help='w = a * Q^0.5, m')
    parser.add_argument('--depth-coef', type=float, default=0.4, help='d = c * Q^0.4, m')
    parser.add_argument('--bank', type=float, default=1.5, help='bank slope width beyond the channel, m')
    parser.add_argument('--paint-layer', type=int, default=5, help='TerrainLayers.layer of the channel bed (pebbles)')
    args = parser.parse_args()

    db = sqlite3.connect(DB)
    columns = {row[1] for row in db.execute('PRAGMA table_info(TerrainEdits)')}
    if 'raster' not in columns:
        db.execute('ALTER TABLE TerrainEdits ADD COLUMN raster TEXT')
    terrain, cell = cell_size(db, args.level)
    discharge_path = os.path.join(ROOT, args.discharge)
    out_path = os.path.join(ROOT, 'Textures', args.out.replace('\\', os.sep))

    for iteration in range(max(args.iterations, 1)):
        if args.iterations > 0:
            print('iteration %d: engine exports the discharge' % (iteration + 1))
            run_engine(args, discharge_path)
        discharge = dds.read_r32f(discharge_path)
        dds.write_r32f(out_path, carve(discharge, cell, args))
        register(db, terrain, args)
    print('edit %s: raster %s' % (args.edit, args.out))


if __name__ == '__main__':
    main()
