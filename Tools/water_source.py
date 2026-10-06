"""Источники воды уровня в base.db3 (таблица WaterSources) — родник, ледниковое озеро: точка, расход и пятно.

Движок добавляет их к источникам по карте водосбора при загрузке (WaterSimulation, mainSources): гауссово пятно
притока с суммой, равной расходу. Источник в низине (котловина кара) наливает её до перелива ещё при загрузке, и из
неё вытекает ручей; источник на склоне — родник, ручей начинается из мокрого пятна. Подробно — docs/water.md.

  python Tools/water_source.py list [--level Test]
  python Tools/water_source.py add ИМЯ --position x,z [--rate 5] [--radius 4] [--level Test]
  python Tools/water_source.py set ИМЯ [--position x,z] [--rate L] [--radius R] [--level Test]
  python Tools/water_source.py delete ИМЯ [--level Test]
  python Tools/water_source.py enable ИМЯ / disable ИМЯ [--level Test]

rate — расход, л/с (родник 0,5–5, ручей из ледникового озера 20–200); radius — радиус пятна, м (родник 2–4, ледник —
пол-озера). Источники принадлежат строке WaterSimulation уровня (Levels.water_simulation). Повторный add с тем же
именем заменяет источник. Сообщения — ASCII (Windows PowerShell 5.1, cmd).
"""
import argparse
import os
import sqlite3
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DB = os.path.join(ROOT, 'base.db3')

SCHEMA = """
CREATE TABLE IF NOT EXISTS WaterSources (
    id INTEGER PRIMARY KEY,
    water_simulation INTEGER NOT NULL REFERENCES WaterSimulation(id),
    name TEXT NOT NULL,
    enabled INTEGER NOT NULL DEFAULT 1,
    x REAL NOT NULL,
    z REAL NOT NULL,
    rate REAL NOT NULL DEFAULT 5,
    radius REAL NOT NULL DEFAULT 4
);
"""


def water_simulation(db, level):
    row = db.execute('SELECT water_simulation FROM Levels WHERE name = ?', (level,)).fetchone()
    if row is None:
        sys.exit('level %s is not found in Levels' % level)
    if row[0] is None:
        sys.exit('level %s has no water simulation (Levels.water_simulation is NULL)' % level)
    return row[0]


def parse_position(text):
    try:
        x, z = (float(v) for v in text.split(','))
    except ValueError:
        sys.exit('wrong --position "%s": expected x,z' % text)
    return x, z


def find(db, water, name):
    row = db.execute('SELECT id FROM WaterSources WHERE water_simulation = ? AND name = ?', (water, name)).fetchone()
    if row is None:
        sys.exit('water source %s is not found' % name)
    return row[0]


def main():
    parser = argparse.ArgumentParser(description='Water sources of a level (table WaterSources)')
    parser.add_argument('command', choices=['list', 'add', 'set', 'delete', 'enable', 'disable'])
    parser.add_argument('name', nargs='?')
    parser.add_argument('--level', default='Test')
    parser.add_argument('--position', help='x,z in world meters')
    parser.add_argument('--rate', type=float, help='discharge, l/s')
    parser.add_argument('--radius', type=float, help='radius of the wet spot, m')
    args = parser.parse_args()

    db = sqlite3.connect(DB)
    db.executescript(SCHEMA)
    water = water_simulation(db, args.level)

    if args.command == 'list':
        rows = db.execute('SELECT name, enabled, x, z, rate, radius FROM WaterSources WHERE water_simulation = ? ORDER BY id',
                          (water,)).fetchall()
        if not rows:
            print('no water sources')
        for name, enabled, x, z, rate, radius in rows:
            print('%s %s at %.1f,%.1f rate %.2f l/s radius %.1f m' % (name, 'enabled' if enabled else 'disabled', x, z, rate, radius))
        return

    if not args.name:
        sys.exit('name is required')

    if args.command == 'add':
        if not args.position:
            sys.exit('--position x,z is required')
        x, z = parse_position(args.position)
        db.execute('DELETE FROM WaterSources WHERE water_simulation = ? AND name = ?', (water, args.name))
        db.execute('INSERT INTO WaterSources (water_simulation, name, x, z, rate, radius) VALUES (?, ?, ?, ?, ?, ?)',
                   (water, args.name, x, z, args.rate if args.rate is not None else 5.0,
                    args.radius if args.radius is not None else 4.0))
    elif args.command == 'set':
        source = find(db, water, args.name)
        if args.position:
            x, z = parse_position(args.position)
            db.execute('UPDATE WaterSources SET x = ?, z = ? WHERE id = ?', (x, z, source))
        if args.rate is not None:
            db.execute('UPDATE WaterSources SET rate = ? WHERE id = ?', (args.rate, source))
        if args.radius is not None:
            db.execute('UPDATE WaterSources SET radius = ? WHERE id = ?', (args.radius, source))
    elif args.command == 'delete':
        db.execute('DELETE FROM WaterSources WHERE id = ?', (find(db, water, args.name),))
    else:
        db.execute('UPDATE WaterSources SET enabled = ? WHERE id = ?', (1 if args.command == 'enable' else 0,
                                                                         find(db, water, args.name)))
    db.commit()
    print('%s %s: done' % (args.command, args.name))


if __name__ == '__main__':
    main()
