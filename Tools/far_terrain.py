"""Дальние горы вокруг карты уровня в base.db3 (таблица FarTerrain и Levels.far_terrain, объект FarTerrain, docs/far_terrain.md).

Строка — карта высот гор (имя текстуры в Textures: R16 0…1, метры = значение · height_multiplier + height_offset), её угол
и сторона в метрах мира (кратны узлу карты уровня), дальность уровня, чей квад — тексель гор, полосы перехода от карты и
правила материала (скала, осыпи, снег, луг, лес) — те же поля правит окно «Far terrain», «Save level» пишет их обратно.
NULL в полях окна — значения по умолчанию движка. Карту, множитель и сдвиг пишет Tools/gen_far_terrain.py (store).

  python Tools/far_terrain.py show [--level Test]
  python Tools/far_terrain.py set [--level Test] --param поле=значение [...]
  python Tools/far_terrain.py remove [--level Test]

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


def migrate(db):
    db.executescript(SCHEMA)
    existing = [row[1] for row in db.execute('PRAGMA table_info(FarTerrain)')]
    for column, kind in (('macro_distance_from', 'REAL'), ('macro_distance_to', 'REAL'), ('macro_texture_scale', 'REAL'),
                         ('shadow_resolution', 'INTEGER')):
        if column not in existing:
            db.execute('ALTER TABLE FarTerrain ADD COLUMN %s %s' % (column, kind))
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
    parser.add_argument('command', choices=['show', 'set', 'remove'])
    parser.add_argument('--level', default='Test')
    parser.add_argument('--param', action='append', help='field=value, repeatable')
    args = parser.parse_args()

    db = sqlite3.connect(DB)
    migrate(db)
    level, far = level_row(db, args.level)
    columns = [r[1] for r in db.execute('PRAGMA table_info(FarTerrain)') if r[1] not in ('id', 'name')]

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
