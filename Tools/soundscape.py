"""Певцы уровня в base.db3 (таблица SoundscapeLayers, Soundscape, docs/audio.md, «Певцы»).

Слой — как Soundscape в UE: певцы одной записи (строка Sounds, Tools/pack_sound.py) садятся вокруг слушателя там, где их место
по маскам рельефа, и поют её фразы с паузами в свои часы суток.

  python Tools/soundscape.py list [--level Test]
  python Tools/soundscape.py add ИМЯ --sound запись [--param поле=значение ...] [--level Test]
  python Tools/soundscape.py set ИМЯ --param поле=значение [...] [--level Test]
  python Tools/soundscape.py delete ИМЯ / enable ИМЯ / disable ИМЯ [--level Test]
  python Tools/soundscape.py sounds                          — записи (Sounds) с громкостью
  python Tools/soundscape.py sound ЗАПИСЬ --volume 0.8       — громкость записи (Sounds.volume; пусто — NULL, 1)

Те же поля правит окно «Soundscape» (раздел Audio в Outliner): слои добавляются и удаляются кнопками, записи и места —
флажками; громкость записей — окно «Sounds». «Save level» пишет их обратно.

Поля — колонки SoundscapeLayers: sound (Sounds.name; несколько через запятую — у каждого певца одна из них; NULL — слой молчит), habitat (маски места через запятую: mask_forest_spruce, mask_grass …;
пусто — где угодно), hours_from / hours_to (часы суток; от > до — через полночь), singers (певцов сразу), distance_from /
distance_to (от слушателя, м), height_from / height_to (над землёй, м), bout_from / bout_to (поёт с одного места, с),
pause_from / pause_to (между фразами, с; меньше нуля — внахлёст), arrival_from / arrival_to (место опустело — следующий
певец через столько, с), volume (в 10 м), pitch (разброс высоты певцов, доля). NULL — значение по умолчанию
движка (SoundscapeLayerSettings). Сообщения — ASCII (Windows PowerShell 5.1, cmd).
"""
import argparse
import os
import sqlite3
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DB = os.path.join(ROOT, 'base.db3')

SCHEMA = """
CREATE TABLE IF NOT EXISTS SoundscapeLayers (
    id INTEGER PRIMARY KEY,
    level INTEGER NOT NULL REFERENCES Levels(id),
    name TEXT NOT NULL,
    enabled INTEGER NOT NULL DEFAULT 1,
    sound TEXT,
    habitat TEXT,
    hours_from REAL,
    hours_to REAL,
    singers INTEGER,
    distance_from REAL,
    distance_to REAL,
    height_from REAL,
    height_to REAL,
    bout_from REAL,
    bout_to REAL,
    pause_from REAL,
    pause_to REAL,
    arrival_from REAL,
    arrival_to REAL,
    volume REAL,
    pitch REAL
);
"""

FIELDS = ['sound', 'habitat', 'hours_from', 'hours_to', 'singers', 'distance_from', 'distance_to', 'height_from', 'height_to',
          'bout_from', 'bout_to', 'pause_from', 'pause_to', 'arrival_from', 'arrival_to', 'volume', 'pitch']

# Колонки, добавленные после первой версии таблицы: у базы без них — ALTER TABLE
ADDED = [('arrival_from', 'REAL'), ('arrival_to', 'REAL')]


def relax_sound(db):
    """sound был NOT NULL; слой, добавленный в окне «Soundscape», бывает без записи (NULL — молчит): таблица — заново без
    ограничения (SQLite не меняет ограничения колонки), строки — те же"""
    info = db.execute('PRAGMA table_info(SoundscapeLayers)').fetchall()
    if not any(row[1] == 'sound' and row[3] for row in info):
        return
    columns = ', '.join(row[1] for row in info)
    db.execute('ALTER TABLE SoundscapeLayers RENAME TO SoundscapeLayers_old')
    db.execute(SCHEMA.replace('CREATE TABLE IF NOT EXISTS', 'CREATE TABLE'))
    db.execute('INSERT INTO SoundscapeLayers (%s) SELECT %s FROM SoundscapeLayers_old' % (columns, columns))
    db.execute('DROP TABLE SoundscapeLayers_old')
    print('migrated: SoundscapeLayers.sound may be NULL')


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
    parser = argparse.ArgumentParser(description='Soundscape layers of a level (SoundscapeLayers)')
    parser.add_argument('command', choices=['list', 'add', 'set', 'delete', 'enable', 'disable', 'sounds', 'sound'])
    parser.add_argument('name', nargs='?')
    parser.add_argument('--level', default='Test')
    parser.add_argument('--sound', help='recordings: Sounds.name, several - comma-separated')
    parser.add_argument('--param', action='append', help='field=value, repeatable (empty value - NULL)')
    parser.add_argument('--volume', help='sound: gain of the recording (empty - NULL, 1)')
    args = parser.parse_args()

    db = sqlite3.connect(DB)
    db.execute('PRAGMA foreign_keys = ON')
    db.executescript(SCHEMA)
    existing = [row[1] for row in db.execute('PRAGMA table_info(SoundscapeLayers)')]
    for column, kind in ADDED:
        if column not in existing:
            db.execute('ALTER TABLE SoundscapeLayers ADD COLUMN %s %s' % (column, kind))
            print('migrated: SoundscapeLayers.%s added' % column)
    relax_sound(db)
    has_sounds = db.execute("SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = 'Sounds'").fetchone() is not None
    # Громкость записи (окно «Sounds») — колонка после первой версии таблицы Sounds (Tools/pack_sound.py)
    if has_sounds and 'volume' not in [row[1] for row in db.execute('PRAGMA table_info(Sounds)')]:
        db.execute('ALTER TABLE Sounds ADD COLUMN volume REAL')
        print('migrated: Sounds.volume added')

    if args.command in ('sounds', 'sound'):
        if not has_sounds:
            sys.exit('no table Sounds (Tools/pack_sound.py)')
        if args.command == 'sounds':
            for name, file, volume in db.execute('SELECT name, file, volume FROM Sounds ORDER BY name'):
                print('%s %s volume=%s' % (name, file, 1.0 if volume is None else volume))
        else:
            if not args.name or args.volume is None:
                sys.exit('sound: a recording name and --volume are required')
            volume = float(args.volume) if args.volume != '' else None
            if db.execute('UPDATE Sounds SET volume = ? WHERE name = ?', (volume, args.name)).rowcount == 0:
                sys.exit('sound %s is not found in Sounds' % args.name)
            print('sound %s: volume %s' % (args.name, 'NULL' if volume is None else volume))
        db.commit()
        return
    level = level_id(db, args.level)

    if args.command == 'list':
        rows = db.execute('SELECT name, enabled, %s FROM SoundscapeLayers WHERE level = ? ORDER BY id' % ', '.join(FIELDS),
                          (level,)).fetchall()
        if not rows:
            print('no soundscape layers')
        for row in rows:
            print('%s %s %s' % (row[0], 'enabled' if row[1] else 'disabled',
                                ', '.join('%s=%s' % (key, value) for key, value in zip(FIELDS, row[2:]) if value is not None)))
        db.commit()
        return
    if not args.name:
        sys.exit('name is required')

    values = parse_params(args.param)
    if args.sound:
        values['sound'] = args.sound
    if 'sound' in values and values['sound'] is not None and db.execute(
            "SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = 'Sounds'").fetchone():
        for name in [n.strip() for n in values['sound'].split(',') if n.strip()]:
            if not db.execute('SELECT 1 FROM Sounds WHERE name = ?', (name,)).fetchone():
                print('warning: no sound %s in table Sounds yet (Tools/pack_sound.py)' % name)
    if args.command == 'add':
        if not values.get('sound'):
            sys.exit('add: --sound <Sounds.name> is required')
        db.execute('DELETE FROM SoundscapeLayers WHERE name = ? AND level = ?', (args.name, level))
        names = ['level', 'name'] + list(values)
        db.execute('INSERT INTO SoundscapeLayers (%s) VALUES (%s)' % (', '.join(names), ', '.join('?' * len(names))),
                   [level, args.name] + list(values.values()))
    else:
        row = db.execute('SELECT id FROM SoundscapeLayers WHERE name = ? AND level = ?', (args.name, level)).fetchone()
        if row is None:
            sys.exit('soundscape layer %s is not found' % args.name)
        if args.command == 'set':
            for key, value in values.items():
                db.execute('UPDATE SoundscapeLayers SET %s = ? WHERE id = ?' % key, (value, row[0]))
        elif args.command == 'delete':
            db.execute('DELETE FROM SoundscapeLayers WHERE id = ?', (row[0],))
        else:
            db.execute('UPDATE SoundscapeLayers SET enabled = ? WHERE id = ?', (1 if args.command == 'enable' else 0, row[0]))
    db.commit()
    print('%s %s: done' % (args.command, args.name))


if __name__ == '__main__':
    main()
