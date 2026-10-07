"""Облака уровня в base.db3 (таблица VolumetricCloud и Levels.volumetric_cloud, VolumetricCloud, docs/clouds.md).

Строка — как Volumetric Cloud в UE5: высота основания и толщина слоя, покрытие, плотность (коэффициент ослабления, 1/м),
альбедо, размеры шумов формы, краёв и карты погоды (м на повтор), скорость облаков (направление — ветра уровня), сила
тени на земле, дальность. Те же поля правит окно «Volumetric cloud», кнопка «Save level environment» пишет их обратно.

  python Tools/volumetric_cloud.py show [--level Test]
  python Tools/volumetric_cloud.py set [--level Test] --param поле=значение [...]
  python Tools/volumetric_cloud.py remove [--level Test]

Первый запуск заводит таблицу и колонку Levels.volumetric_cloud. set без строки у уровня создаёт её со значениями по
умолчанию. Сообщения — ASCII (Windows PowerShell 5.1, cmd).
"""
import argparse
import os
import sqlite3
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DB = os.path.join(ROOT, 'base.db3')

SCHEMA = """
CREATE TABLE IF NOT EXISTS VolumetricCloud (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL,
    layer_bottom_altitude REAL NOT NULL DEFAULT 1500,
    layer_height REAL NOT NULL DEFAULT 2500,
    coverage REAL NOT NULL DEFAULT 0.3,
    density REAL NOT NULL DEFAULT 0.02,
    albedo TEXT NOT NULL DEFAULT '1,1,1',
    shape_scale REAL NOT NULL DEFAULT 8000,
    detail_scale REAL NOT NULL DEFAULT 800,
    weather_scale REAL NOT NULL DEFAULT 40000,
    wind_speed REAL NOT NULL DEFAULT 10,
    shadow_strength REAL NOT NULL DEFAULT 1,
    tracing_max_distance REAL NOT NULL DEFAULT 50000
);
"""


def migrate(db):
    db.executescript(SCHEMA)
    if 'volumetric_cloud' not in [row[1] for row in db.execute('PRAGMA table_info(Levels)')]:
        db.execute('ALTER TABLE Levels ADD COLUMN volumetric_cloud INTEGER REFERENCES VolumetricCloud(id)')
        print('migrated: Levels.volumetric_cloud added')


def main():
    parser = argparse.ArgumentParser(description='Volumetric cloud of a level (VolumetricCloud)')
    parser.add_argument('command', choices=['show', 'set', 'remove'])
    parser.add_argument('--level', default='Test')
    parser.add_argument('--param', action='append', help='field=value, repeatable')
    args = parser.parse_args()

    db = sqlite3.connect(DB)
    migrate(db)
    row = db.execute('SELECT id, volumetric_cloud FROM Levels WHERE name = ?', (args.level,)).fetchone()
    if row is None:
        sys.exit('level %s is not found in Levels' % args.level)
    level, cloud = row
    columns = [r[1] for r in db.execute('PRAGMA table_info(VolumetricCloud)') if r[1] not in ('id', 'name')]

    if args.command == 'show':
        if cloud is None:
            print('level %s has no volumetric cloud' % args.level)
        else:
            values = db.execute('SELECT %s FROM VolumetricCloud WHERE id = ?' % ', '.join(columns), (cloud,)).fetchone()
            for column, value in zip(columns, values):
                print('%s = %s' % (column, value))
    elif args.command == 'set':
        if cloud is None:
            cloud = db.execute('INSERT INTO VolumetricCloud (name) VALUES (?)', (args.level,)).lastrowid
            db.execute('UPDATE Levels SET volumetric_cloud = ? WHERE id = ?', (cloud, level))
        for item in args.param or []:
            if '=' not in item:
                sys.exit('--param expects field=value: %s' % item)
            key, value = item.split('=', 1)
            if key not in columns:
                sys.exit('unknown field %s; fields: %s' % (key, ', '.join(columns)))
            db.execute('UPDATE VolumetricCloud SET %s = ? WHERE id = ?' % key, (value, cloud))
        print('set volumetric cloud of %s: done' % args.level)
    else:
        if cloud is not None:
            db.execute('UPDATE Levels SET volumetric_cloud = NULL WHERE id = ?', (level,))
            db.execute('DELETE FROM VolumetricCloud WHERE id = ?', (cloud,))
        print('removed volumetric cloud of %s' % args.level)
    db.commit()


if __name__ == '__main__':
    main()
