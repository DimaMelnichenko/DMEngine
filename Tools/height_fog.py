"""Туман уровня в base.db3 (таблица ExponentialHeightFog и Levels.height_fog, VolumetricFog, docs/fog.md).

Строка — как Exponential Height Fog с Volumetric Fog в UE: два слоя по высоте (дымка над долиной и туман в низинах:
ниже height плотность ровная, выше спадает по экспоненте), альбедо, анизотропия рассеяния, объём со светом и тенями и его
дальность. Плотность — коэффициент ослабления, 1/м (видимость ~3 / density). Те же поля правит окно «Height fog», кнопка
«Save level environment» пишет их обратно.

  python Tools/height_fog.py show [--level Test]
  python Tools/height_fog.py set [--level Test] --param поле=значение [...]
  python Tools/height_fog.py remove [--level Test]

Первый запуск заводит таблицу и колонку Levels.height_fog. set без строки у уровня создаёт её со значениями по
умолчанию. Сообщения — ASCII (Windows PowerShell 5.1, cmd).
"""
import argparse
import os
import sqlite3
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DB = os.path.join(ROOT, 'base.db3')

SCHEMA = """
CREATE TABLE IF NOT EXISTS ExponentialHeightFog (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL,
    density REAL NOT NULL DEFAULT 0,
    height REAL NOT NULL DEFAULT 0,
    height_falloff REAL NOT NULL DEFAULT 0.01,
    second_density REAL NOT NULL DEFAULT 0,
    second_height REAL NOT NULL DEFAULT 0,
    second_height_falloff REAL NOT NULL DEFAULT 0.01,
    albedo TEXT NOT NULL DEFAULT '1,1,1',
    scattering_distribution REAL NOT NULL DEFAULT 0.2,
    volumetric INTEGER NOT NULL DEFAULT 1,
    view_distance REAL NOT NULL DEFAULT 200
);
"""


def migrate(db):
    db.executescript(SCHEMA)
    if 'height_fog' not in [row[1] for row in db.execute('PRAGMA table_info(Levels)')]:
        db.execute('ALTER TABLE Levels ADD COLUMN height_fog INTEGER REFERENCES ExponentialHeightFog(id)')
        print('migrated: Levels.height_fog added')


def main():
    parser = argparse.ArgumentParser(description='Height fog of a level (ExponentialHeightFog)')
    parser.add_argument('command', choices=['show', 'set', 'remove'])
    parser.add_argument('--level', default='Test')
    parser.add_argument('--param', action='append', help='field=value, repeatable')
    args = parser.parse_args()

    db = sqlite3.connect(DB)
    migrate(db)
    row = db.execute('SELECT id, height_fog FROM Levels WHERE name = ?', (args.level,)).fetchone()
    if row is None:
        sys.exit('level %s is not found in Levels' % args.level)
    level, fog = row
    columns = [r[1] for r in db.execute('PRAGMA table_info(ExponentialHeightFog)') if r[1] not in ('id', 'name')]

    if args.command == 'show':
        if fog is None:
            print('level %s has no height fog' % args.level)
        else:
            values = db.execute('SELECT %s FROM ExponentialHeightFog WHERE id = ?' % ', '.join(columns), (fog,)).fetchone()
            for column, value in zip(columns, values):
                print('%s = %s' % (column, value))
    elif args.command == 'set':
        if fog is None:
            fog = db.execute('INSERT INTO ExponentialHeightFog (name) VALUES (?)', (args.level,)).lastrowid
            db.execute('UPDATE Levels SET height_fog = ? WHERE id = ?', (fog, level))
        for item in args.param or []:
            if '=' not in item:
                sys.exit('--param expects field=value: %s' % item)
            key, value = item.split('=', 1)
            if key not in columns:
                sys.exit('unknown field %s; fields: %s' % (key, ', '.join(columns)))
            db.execute('UPDATE ExponentialHeightFog SET %s = ? WHERE id = ?' % key, (value, fog))
        print('set height fog of %s: done' % args.level)
    else:
        if fog is not None:
            db.execute('UPDATE Levels SET height_fog = NULL WHERE id = ?', (level,))
            db.execute('DELETE FROM ExponentialHeightFog WHERE id = ?', (fog,))
        print('removed height fog of %s' % args.level)
    db.commit()


if __name__ == '__main__':
    main()
