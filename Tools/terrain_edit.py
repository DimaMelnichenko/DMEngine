"""Правки рельефа уровня в base.db3 (таблицы TerrainEdits / TerrainEditPoints, как Landscape Splines и Edit Layers в UE).

Движок накладывает их на карту высот при загрузке (Scene/Terrain/TerrainEdits): кривая через точки поднимает и (или)
опускает рельеф к своей высоте — русло, насыпь тропы; одна точка — круглая площадка. Та же полоса убирает растительность
(--clear-foliage: доля 0…1) и красит слой splat-карты (--paint-layer: TerrainLayers.layer), как Paint Layer у Landscape
Splines; --paint-only — правка без высоты, только очистка и покраска. Подробно — docs/terrain.md.

  python Tools/terrain_edit.py list [--level Test]
  python Tools/terrain_edit.py add ИМЯ --points "x,z[,y[,ширина[,полоса]]];..." [--width 4] [--falloff 4]
         [--height Y] [--relative] [--raise-only | --lower-only | --paint-only] [--linear] [--layer N]
         [--clear-foliage F] [--paint-layer N] [--level Test]
  python Tools/terrain_edit.py set ИМЯ [--clear-foliage F] [--paint-layer N | --no-paint] [--level Test]
  python Tools/terrain_edit.py delete ИМЯ [--level Test]
  python Tools/terrain_edit.py enable ИМЯ / disable ИМЯ [--level Test]

y точки — высота, м; у --relative — смещение от рельефа под точкой (−1.5 — русло на полтора метра ниже земли). Без y —
--height, иначе (абсолютная правка) — высота рельефа под точкой по карте высот: площадка «как есть сейчас». Повторный
add с тем же именем заменяет правку. Сообщения — ASCII (Windows PowerShell 5.1, cmd).
"""
import argparse
import os
import sqlite3
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dds  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DB = os.path.join(ROOT, 'base.db3')

SCHEMA = """
CREATE TABLE IF NOT EXISTS TerrainEdits (
    id INTEGER PRIMARY KEY,
    terrain INTEGER NOT NULL REFERENCES Terrain(id),
    layer INTEGER NOT NULL DEFAULT 0,
    name TEXT NOT NULL,
    enabled INTEGER NOT NULL DEFAULT 1,
    raise_terrain INTEGER NOT NULL DEFAULT 1,
    lower_terrain INTEGER NOT NULL DEFAULT 1,
    relative INTEGER NOT NULL DEFAULT 0,
    smooth INTEGER NOT NULL DEFAULT 1,
    clear_foliage REAL NOT NULL DEFAULT 0,
    paint_layer INTEGER
);
CREATE TABLE IF NOT EXISTS TerrainEditPoints (
    edit INTEGER NOT NULL REFERENCES TerrainEdits(id) ON DELETE CASCADE,
    point INTEGER NOT NULL,
    x REAL NOT NULL,
    y REAL NOT NULL DEFAULT 0,
    z REAL NOT NULL,
    width REAL NOT NULL DEFAULT 4,
    falloff REAL NOT NULL DEFAULT 4,
    PRIMARY KEY (edit, point)
);
"""
# Колонки, добавленные после первой версии таблицы: у базы без них — ALTER TABLE
COLUMNS = [('clear_foliage', 'REAL NOT NULL DEFAULT 0'), ('paint_layer', 'INTEGER')]


def migrate(db):
    existing = {row[1] for row in db.execute('PRAGMA table_info(TerrainEdits)')}
    for name, declaration in COLUMNS:
        if name not in existing:
            db.execute('ALTER TABLE TerrainEdits ADD COLUMN %s %s' % (name, declaration))


def terrain_of(db, level):
    row = db.execute('SELECT terrain FROM Levels WHERE name = ?', (level,)).fetchone()
    if not row or row[0] is None:
        sys.exit('level %s has no terrain' % level)
    return row[0]


def height_sampler(db, terrain):
    """Высота исходного рельефа в точке мира, м (билинейно, как Scene/Terrain/TerrainEdits)."""
    name, multiplier, offset, texel = db.execute(
        'SELECT heightmap, height_multiplier, height_offset, width_multiplier FROM Terrain WHERE id = ?', (terrain,)).fetchone()
    path = db.execute('SELECT file FROM Textures WHERE name = ?', (name,)).fetchone()[0]
    heights = dds.read_r16(os.path.join(ROOT, 'Textures', path.replace('\\', os.sep)))
    size = heights.shape[0]

    def sample(x, z):
        col = min(max(x / texel - 0.5, 0.0), size - 1.0)
        row = min(max(size - z / texel - 0.5, 0.0), size - 1.0)
        c0, r0 = int(col), int(row)
        c1, r1 = min(c0 + 1, size - 1), min(r0 + 1, size - 1)
        fc, fr = col - c0, row - r0
        top = heights[r0, c0] + (heights[r0, c1] - heights[r0, c0]) * fc
        bottom = heights[r1, c0] + (heights[r1, c1] - heights[r1, c0]) * fc
        return (top + (bottom - top) * fr) * multiplier + offset
    return sample


def parse_points(text, args, sample):
    points = []
    for item in text.split(';'):
        item = item.strip()
        if not item:
            continue
        values = [float(v) for v in item.split(',')]
        if len(values) < 2:
            sys.exit('point needs at least x,z: %s' % item)
        x, z = values[0], values[1]
        if len(values) > 2:
            y = values[2]
        elif args.height is not None:
            y = args.height
        else:
            y = 0.0 if args.relative else sample(x, z)
        width = values[3] if len(values) > 3 else args.width
        falloff = values[4] if len(values) > 4 else args.falloff
        points.append((x, y, z, width, falloff))
    if not points:
        sys.exit('no points')
    return points


def main():
    parser = argparse.ArgumentParser(description='Terrain edits in base.db3 (TerrainEdits / TerrainEditPoints)')
    parser.add_argument('command', choices=['list', 'add', 'set', 'delete', 'enable', 'disable'])
    parser.add_argument('name', nargs='?')
    parser.add_argument('--level', default='Test')
    parser.add_argument('--points', help='"x,z[,y[,width[,falloff]]];..."')
    parser.add_argument('--width', type=float, default=4.0, help='flat part width, m')
    parser.add_argument('--falloff', type=float, default=4.0, help='blend band to the original terrain on each side, m')
    parser.add_argument('--height', type=float, help='y for points without one')
    parser.add_argument('--relative', action='store_true', help='y is an offset from the terrain under the point')
    group = parser.add_mutually_exclusive_group()
    group.add_argument('--raise-only', action='store_true')
    group.add_argument('--lower-only', action='store_true')
    group.add_argument('--paint-only', action='store_true', help='no height change: only clear foliage and paint')
    parser.add_argument('--linear', action='store_true', help='polyline instead of Catmull-Rom')
    parser.add_argument('--layer', type=int, default=0, help='edits apply in layer order')
    parser.add_argument('--clear-foliage', type=float, help='0..1: share of scatter (grass, forest) removed under the edit')
    paint = parser.add_mutually_exclusive_group()
    paint.add_argument('--paint-layer', type=int, choices=range(8), help='TerrainLayers.layer painted under the edit')
    paint.add_argument('--no-paint', action='store_true', help='set: stop painting')
    args = parser.parse_args()

    db = sqlite3.connect(DB)
    db.execute('PRAGMA foreign_keys = ON')
    db.executescript(SCHEMA)
    migrate(db)
    terrain = terrain_of(db, args.level)

    if args.command == 'list':
        for row in db.execute('SELECT id, layer, name, enabled, raise_terrain, lower_terrain, relative, smooth, clear_foliage, '
                              'paint_layer FROM TerrainEdits WHERE terrain = ? ORDER BY layer, id', (terrain,)):
            count = db.execute('SELECT COUNT(*) FROM TerrainEditPoints WHERE edit = ?', (row[0],)).fetchone()[0]
            paint_layer = '-' if row[9] is None else str(row[9])
            print('%d layer %d %s enabled %d raise %d lower %d relative %d smooth %d clear %g paint %s points %d' %
                  (row[:9] + (paint_layer, count)))
        return
    if not args.name:
        sys.exit('name is required')

    if args.command == 'add':
        if not args.points:
            sys.exit('--points is required')
        points = parse_points(args.points, args, height_sampler(db, terrain))
        db.execute('DELETE FROM TerrainEdits WHERE terrain = ? AND name = ?', (terrain, args.name))
        cursor = db.execute('INSERT INTO TerrainEdits (terrain, layer, name, raise_terrain, lower_terrain, relative, smooth, '
                            'clear_foliage, paint_layer) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)',
                            (terrain, args.layer, args.name, 0 if args.lower_only or args.paint_only else 1,
                             0 if args.raise_only or args.paint_only else 1, 1 if args.relative else 0, 0 if args.linear else 1,
                             args.clear_foliage or 0.0, args.paint_layer))
        edit = cursor.lastrowid
        db.executemany('INSERT INTO TerrainEditPoints (edit, point, x, y, z, width, falloff) VALUES (?, ?, ?, ?, ?, ?, ?)',
                       [(edit, i) + p for i, p in enumerate(points)])
        print('edit %s: %d points' % (args.name, len(points)))
    elif args.command == 'set':
        if not db.execute('SELECT 1 FROM TerrainEdits WHERE terrain = ? AND name = ?', (terrain, args.name)).fetchone():
            sys.exit('no edit %s' % args.name)
        if args.clear_foliage is not None:
            db.execute('UPDATE TerrainEdits SET clear_foliage = ? WHERE terrain = ? AND name = ?', (args.clear_foliage, terrain, args.name))
        if args.paint_layer is not None or args.no_paint:
            db.execute('UPDATE TerrainEdits SET paint_layer = ? WHERE terrain = ? AND name = ?', (args.paint_layer, terrain, args.name))
        print('set %s' % args.name)
    elif args.command == 'delete':
        db.execute('DELETE FROM TerrainEdits WHERE terrain = ? AND name = ?', (terrain, args.name))
        print('deleted %s' % args.name)
    else:
        db.execute('UPDATE TerrainEdits SET enabled = ? WHERE terrain = ? AND name = ?',
                   (1 if args.command == 'enable' else 0, terrain, args.name))
        print('%sd %s' % (args.command, args.name))
    db.commit()


if __name__ == '__main__':
    main()
