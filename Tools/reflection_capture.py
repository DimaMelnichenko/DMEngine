"""Карты отражений места уровня в base.db3 (таблица LevelReflectionCaptures, ReflectionCaptures, docs/reflection_captures.md).

Карта — как Sphere Reflection Capture в UE: кубическая карта сцены, снятая из точки; внутри радиуса влияния вода отражает её
там, где экранные отражения промахнулись (за краем кадра, за предметами), за радиусом — небо. Точку ставят над водой на
высоте глаза (1,5-2 м), радиус — чтобы накрыл озеро или участок русла.

  python Tools/reflection_capture.py list [--level ValleyOfStreams]
  python Tools/reflection_capture.py add ИМЯ --position x,y,z [--param поле=значение ...] [--level ValleyOfStreams]
  python Tools/reflection_capture.py set ИМЯ --param поле=значение [...]
  python Tools/reflection_capture.py delete ИМЯ / enable ИМЯ / disable ИМЯ [--level ValleyOfStreams]

Поля — колонки LevelReflectionCaptures: position ("x,y,z"), influence_radius (м), transition (доля радиуса у края, где карта
переходит в небо), face_size (64, 128, 256, 512), brightness. NULL — значение по умолчанию движка. Сообщения — ASCII
(Windows PowerShell 5.1, cmd).
"""
import argparse
import os
import sqlite3
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DB = os.path.join(ROOT, 'base.db3')

SCHEMA = """
CREATE TABLE IF NOT EXISTS LevelReflectionCaptures (
    id INTEGER PRIMARY KEY,
    level INTEGER NOT NULL REFERENCES Levels(id),
    name TEXT NOT NULL,
    enabled INTEGER NOT NULL DEFAULT 1,
    position TEXT NOT NULL DEFAULT '0,0,0',
    influence_radius REAL,
    transition REAL,
    face_size INTEGER,
    brightness REAL
);
"""

FIELDS = ['position', 'influence_radius', 'transition', 'face_size', 'brightness']


def level_id(db, level):
    row = db.execute('SELECT id FROM Levels WHERE name = ?', (level,)).fetchone()
    if row is None:
        sys.exit('level %s is not found in Levels' % level)
    return row[0]


def parse_params(items):
    values = {}
    for item in items or []:
        if '=' not in item:
            sys.exit('--param expects field=value: %s' % item)
        key, value = item.split('=', 1)
        if key not in FIELDS:
            sys.exit('unknown field %s; fields: %s' % (key, ', '.join(FIELDS)))
        values[key] = value if value != '' else None
    return values


def main():
    parser = argparse.ArgumentParser(description='Reflection captures of a level (LevelReflectionCaptures)')
    parser.add_argument('command', choices=['list', 'add', 'set', 'delete', 'enable', 'disable'])
    parser.add_argument('name', nargs='?')
    parser.add_argument('--level', default='ValleyOfStreams')
    parser.add_argument('--position', help='x,y,z of the capture point')
    parser.add_argument('--param', action='append', help='field=value, repeatable (empty value - NULL)')
    args = parser.parse_args()

    db = sqlite3.connect(DB)
    db.execute('PRAGMA foreign_keys = ON')
    db.executescript(SCHEMA)
    level = level_id(db, args.level)

    if args.command == 'list':
        rows = db.execute('SELECT name, enabled, position, influence_radius, transition, face_size, brightness '
                          'FROM LevelReflectionCaptures WHERE level = ? ORDER BY id', (level,)).fetchall()
        if not rows:
            print('no reflection captures')
        for name, enabled, position, radius, transition, face, brightness in rows:
            print('%s %s position %s radius %s transition %s face %s brightness %s' %
                  (name, 'enabled' if enabled else 'disabled', position, radius, transition, face, brightness))
        db.commit()
        return
    if not args.name:
        sys.exit('name is required')

    values = parse_params(args.param)
    if args.position:
        values['position'] = args.position
    if args.command == 'add':
        if 'position' not in values:
            sys.exit('add: --position x,y,z is required')
        db.execute('DELETE FROM LevelReflectionCaptures WHERE name = ? AND level = ?', (args.name, level))
        names = ['level', 'name'] + list(values)
        db.execute('INSERT INTO LevelReflectionCaptures (%s) VALUES (%s)' % (', '.join(names), ', '.join('?' * len(names))),
                   [level, args.name] + list(values.values()))
    else:
        row = db.execute('SELECT id FROM LevelReflectionCaptures WHERE name = ? AND level = ?', (args.name, level)).fetchone()
        if row is None:
            sys.exit('reflection capture %s is not found' % args.name)
        if args.command == 'set':
            for key, value in values.items():
                db.execute('UPDATE LevelReflectionCaptures SET %s = ? WHERE id = ?' % key, (value, row[0]))
        elif args.command == 'delete':
            db.execute('DELETE FROM LevelReflectionCaptures WHERE id = ?', (row[0],))
        else:
            db.execute('UPDATE LevelReflectionCaptures SET enabled = ? WHERE id = ?', (1 if args.command == 'enable' else 0, row[0]))
    db.commit()
    print('%s %s: done' % (args.command, args.name))


if __name__ == '__main__':
    main()
