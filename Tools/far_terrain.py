"""Дальние горы вокруг карты уровня в base.db3 (таблица FarTerrain и Levels.far_terrain, объект FarTerrain, docs/far_terrain.md).

Строка — карта высот гор (имя текстуры в Textures: R16 0…1, метры = значение · height_multiplier + height_offset), её угол
и сторона в метрах мира (кратны узлу карты уровня), дальность уровня, чей квад — тексель гор, полосы перехода от карты и
правила материала (скала, осыпи, снег, луг, лес) — те же поля правит окно «Far terrain», «Save level» пишет их обратно.
NULL в полях окна — значения по умолчанию движка. Карту, множитель и сдвиг пишет Tools/gen_far_terrain.py (store).

  python Tools/far_terrain.py show [--level Test]
  python Tools/far_terrain.py set [--level Test] --param поле=значение [...]
  python Tools/far_terrain.py remove [--level Test]
  python Tools/far_terrain.py glacier [--level Test] [--param поле=значение ...]   (заводит строку GlacialErosion)
  python Tools/far_terrain.py no-glacier [--level Test]
  python Tools/far_terrain.py fluvial [--level Test] [--param поле=значение ...]   (заводит строку FluvialErosion)
  python Tools/far_terrain.py no-fluvial [--level Test]

Первый запуск заводит таблицу и колонку Levels.far_terrain. Сообщения — ASCII (Windows PowerShell 5.1, cmd).
"""
import argparse
import os
import sqlite3
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DB = os.path.join(ROOT, 'base.db3')
TEXTURE = 'far_heightmap'

SCHEMA = """
CREATE TABLE IF NOT EXISTS FarTerrain (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL,
    heightmap TEXT NOT NULL,
    origin TEXT NOT NULL DEFAULT '-32768,-32768',
    extent REAL NOT NULL DEFAULT 65536,
    height_multiplier REAL NOT NULL DEFAULT 1,
    height_offset REAL NOT NULL DEFAULT 0,
    lod_distance REAL,
    blend_distance REAL,
    material_blend REAL,
    macro_distance_from REAL, macro_distance_to REAL,
    macro_texture_scale REAL,
    shadow_resolution INTEGER,
    rock_slope_from REAL, rock_slope_to REAL,
    scree_slope_from REAL, scree_slope_to REAL,
    scree_height_from REAL, scree_height_to REAL,
    meadow_height_from REAL, meadow_height_to REAL,
    snow_height_from REAL, snow_height_to REAL,
    snow_north REAL,
    snow_noise REAL,
    snow_slope_from REAL, snow_slope_to REAL,
    forest_line_from REAL, forest_line_to REAL,
    forest_slope_from REAL, forest_slope_to REAL,
    forest_color TEXT
);
"""


FLUVIAL_SCHEMA = """
CREATE TABLE IF NOT EXISTS FluvialErosion (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL,
    resolution INTEGER,
    erodibility REAL,
    area_exponent REAL,
    years REAL,
    steps INTEGER,
    hillslope_diffusion REAL,
    routing_noise REAL,
    uplift_scale REAL,
    uplift_time REAL,
    uplift_floor REAL,
    map_fade_from REAL, map_fade_to REAL
);
"""

GLACIER_SCHEMA = """
CREATE TABLE IF NOT EXISTS GlacialErosion (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL,
    resolution INTEGER,
    ela REAL,
    balance_gradient REAL,
    max_accumulation REAL,
    flow REAL,
    sliding REAL,
    ice_years REAL,
    erosion_years REAL,
    erosion REAL,
    max_erosion REAL,
    map_fade_from REAL, map_fade_to REAL,
    polish REAL,
    frost_peaks REAL,
    frost_peak_spacing REAL,
    frost_peak_height_from REAL, frost_peak_height_to REAL,
    frost_peak_fade_from REAL, frost_peak_fade_to REAL
);
"""


def migrate(db):
    db.executescript(SCHEMA)
    db.executescript(GLACIER_SCHEMA)
    db.executescript(FLUVIAL_SCHEMA)
    glacier_columns = [row[1] for row in db.execute('PRAGMA table_info(GlacialErosion)')]
    for column in ('frost_peaks', 'frost_peak_spacing', 'frost_peak_height_from', 'frost_peak_height_to', 'frost_peak_fade_from',
                   'frost_peak_fade_to'):
        if column not in glacier_columns:
            db.execute('ALTER TABLE GlacialErosion ADD COLUMN %s REAL' % column)
    fluvial_columns = [row[1] for row in db.execute('PRAGMA table_info(FluvialErosion)')]
    for column in ('routing_noise', 'uplift_scale', 'uplift_time', 'uplift_floor'):
        if column not in fluvial_columns:
            db.execute('ALTER TABLE FluvialErosion ADD COLUMN %s REAL' % column)
    if 'fluvial_erosion' not in [row[1] for row in db.execute('PRAGMA table_info(FarTerrain)')]:
        db.execute('ALTER TABLE FarTerrain ADD COLUMN fluvial_erosion INTEGER REFERENCES FluvialErosion(id)')
        print('migrated: FarTerrain.fluvial_erosion added')
    existing = [row[1] for row in db.execute('PRAGMA table_info(FarTerrain)')]
    for column, kind in (('macro_distance_from', 'REAL'), ('macro_distance_to', 'REAL'), ('macro_texture_scale', 'REAL'),
                         ('shadow_resolution', 'INTEGER')):
        if column not in existing:
            db.execute('ALTER TABLE FarTerrain ADD COLUMN %s %s' % (column, kind))
    if 'glacial_erosion' not in [row[1] for row in db.execute('PRAGMA table_info(FarTerrain)')]:
        db.execute('ALTER TABLE FarTerrain ADD COLUMN glacial_erosion INTEGER REFERENCES GlacialErosion(id)')
        print('migrated: FarTerrain.glacial_erosion added')
    if 'far_terrain' not in [row[1] for row in db.execute('PRAGMA table_info(Levels)')]:
        db.execute('ALTER TABLE Levels ADD COLUMN far_terrain INTEGER REFERENCES FarTerrain(id)')
        print('migrated: Levels.far_terrain added')


def level_row(db, level):
    row = db.execute('SELECT id, far_terrain FROM Levels WHERE name = ?', (level,)).fetchone()
    if row is None:
        sys.exit('level %s is not found in Levels' % level)
    return row


def store(level, file, multiplier, offset, origin, extent):
    """Карта гор уровня: строка Textures (данные, без сжатия и мипов файла), строка FarTerrain (заводит, если нет) и ссылка"""
    db = sqlite3.connect(DB)
    migrate(db)
    level_id, far = level_row(db, level)
    if db.execute('SELECT id FROM Textures WHERE name = ?', (TEXTURE,)).fetchone() is None:
        db.execute("INSERT INTO Textures (name, file, generate_mipmap, sRGB, compression) VALUES (?, ?, 0, 0, 'none')",
                   (TEXTURE, file))
    else:
        db.execute('UPDATE Textures SET file = ? WHERE name = ?', (file, TEXTURE))
    if far is None:
        far = db.execute('INSERT INTO FarTerrain (name, heightmap) VALUES (?, ?)', (level, TEXTURE)).lastrowid
        db.execute('UPDATE Levels SET far_terrain = ? WHERE id = ?', (far, level_id))
    db.execute('UPDATE FarTerrain SET heightmap = ?, origin = ?, extent = ?, height_multiplier = ?, height_offset = ? WHERE id = ?',
               (TEXTURE, '%g,%g' % origin, extent, multiplier, offset, far))
    db.commit()
    db.close()
    print('far terrain of %s: row %d' % (level, far))


def main():
    parser = argparse.ArgumentParser(description='Far mountains around the level map (FarTerrain)')
    parser.add_argument('command', choices=['show', 'set', 'remove', 'glacier', 'no-glacier', 'fluvial', 'no-fluvial'])
    parser.add_argument('--level', default='Test')
    parser.add_argument('--param', action='append', help='field=value, repeatable')
    args = parser.parse_args()

    db = sqlite3.connect(DB)
    migrate(db)
    level, far = level_row(db, args.level)
    columns = [r[1] for r in db.execute('PRAGMA table_info(FarTerrain)') if r[1] not in ('id', 'name')]

    stages = {'glacier': ('GlacialErosion', 'glacial_erosion', 'glacial'), 'fluvial': ('FluvialErosion', 'fluvial_erosion', 'fluvial')}
    stage = args.command.replace('no-', '')
    if stage in stages:
        table, column, title = stages[stage]
        if far is None:
            sys.exit('level %s has no far terrain: run Tools/gen_far_terrain.py first' % args.level)
        row = db.execute('SELECT %s FROM FarTerrain WHERE id = ?' % column, (far,)).fetchone()[0]
        if args.command.startswith('no-'):
            db.execute('UPDATE FarTerrain SET %s = NULL WHERE id = ?' % column, (far,))
            print('%s erosion of %s: off' % (title, args.level))
        else:
            if row is None:
                row = db.execute('INSERT INTO %s (name) VALUES (?)' % table, (args.level,)).lastrowid
                db.execute('UPDATE FarTerrain SET %s = ? WHERE id = ?' % column, (row, far))
            fields = [r[1] for r in db.execute('PRAGMA table_info(%s)' % table) if r[1] not in ('id', 'name')]
            for item in args.param or []:
                if '=' not in item:
                    sys.exit('--param expects field=value: %s' % item)
                key, value = item.split('=', 1)
                if key not in fields:
                    sys.exit('unknown field %s; fields: %s' % (key, ', '.join(fields)))
                db.execute('UPDATE %s SET %s = ? WHERE id = ?' % (table, key), (None if value == '' else value, row))
            values = db.execute('SELECT %s FROM %s WHERE id = ?' % (', '.join(fields), table), (row,)).fetchone()
            for field, value in zip(fields, values):
                print('%s = %s' % (field, value))
        db.commit()
        return

    if args.command == 'show':
        if far is None:
            print('level %s has no far terrain (run Tools/gen_far_terrain.py)' % args.level)
        else:
            values = db.execute('SELECT %s FROM FarTerrain WHERE id = ?' % ', '.join(columns), (far,)).fetchone()
            for column, value in zip(columns, values):
                print('%s = %s' % (column, value))
    elif args.command == 'set':
        if far is None:
            sys.exit('level %s has no far terrain: run Tools/gen_far_terrain.py first' % args.level)
        for item in args.param or []:
            if '=' not in item:
                sys.exit('--param expects field=value: %s' % item)
            key, value = item.split('=', 1)
            if key not in columns:
                sys.exit('unknown field %s; fields: %s' % (key, ', '.join(columns)))
            db.execute('UPDATE FarTerrain SET %s = ? WHERE id = ?' % key, (None if value == '' else value, far))
        print('set far terrain of %s: done' % args.level)
    else:
        if far is not None:
            db.execute('UPDATE Levels SET far_terrain = NULL WHERE id = ?', (level,))
            if db.execute('SELECT COUNT(*) FROM Levels WHERE far_terrain = ?', (far,)).fetchone()[0] == 0:
                db.execute('DELETE FROM FarTerrain WHERE id = ?', (far,))
        print('removed far terrain of %s' % args.level)
    db.commit()


if __name__ == '__main__':
    main()
