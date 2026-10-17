"""Звуки окружения уровня в base.db3 (таблица AmbientSound и Levels.ambient_sound; AmbientSound, docs/audio.md).

Строка — как Ambient Sound и Soundscape в UE, но синтезом в движке: ветер у камеры, шелест травы вокруг и кроны леса по
сторонам — по ветру уровня, полю ветра по рельефу и маскам травы и леса; журчание ручьёв — по растру воды. Громкости — доли (амплитуда), частоты — середины
полос шума, Гц. NULL — значение по умолчанию движка (AmbientSoundSettings). Те же поля правит окно «Ambient sound»,
«Save level» пишет их обратно.

  python Tools/ambient_sound.py show [--level Test]
  python Tools/ambient_sound.py set [--level Test] [--param поле=значение ...]
  python Tools/ambient_sound.py remove [--level Test]

Первый запуск заводит таблицу и колонку Levels.ambient_sound. set без строки у уровня создаёт её (все поля NULL).
Сообщения — ASCII (Windows PowerShell 5.1, cmd).
"""
import argparse
import os
import sqlite3
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DB = os.path.join(ROOT, 'base.db3')

SCHEMA = """
CREATE TABLE IF NOT EXISTS AmbientSound (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL,
    volume REAL,
    wind_volume REAL,
    wind_reference_speed REAL,
    wind_frequency REAL,
    grass_volume REAL,
    grass_radius REAL,
    grass_height REAL,
    grass_frequency REAL,
    crown_volume REAL,
    crown_radius REAL,
    crown_frequency REAL,
    stream_volume REAL,
    stream_voices INTEGER,
    stream_distance REAL
);
"""

# Колонки, добавленные после первой версии таблицы: у базы без них — ALTER TABLE
ADDED = [('stream_volume', 'REAL'), ('stream_voices', 'INTEGER'), ('stream_distance', 'REAL')]


def migrate(db):
    db.executescript(SCHEMA)
    existing = [row[1] for row in db.execute('PRAGMA table_info(AmbientSound)')]
    for column, kind in ADDED:
        if column not in existing:
            db.execute('ALTER TABLE AmbientSound ADD COLUMN %s %s' % (column, kind))
            print('migrated: AmbientSound.%s added' % column)
    if 'ambient_sound' not in [row[1] for row in db.execute('PRAGMA table_info(Levels)')]:
        db.execute('ALTER TABLE Levels ADD COLUMN ambient_sound INTEGER REFERENCES AmbientSound(id)')
        print('migrated: Levels.ambient_sound added')


def main():
    parser = argparse.ArgumentParser(description='Ambient sound of a level (AmbientSound)')
    parser.add_argument('command', choices=['show', 'set', 'remove'])
    parser.add_argument('--level', default='Test')
    parser.add_argument('--param', action='append', help='field=value (empty value - NULL), repeatable')
    args = parser.parse_args()

    db = sqlite3.connect(DB)
    migrate(db)
    row = db.execute('SELECT id, ambient_sound FROM Levels WHERE name = ?', (args.level,)).fetchone()
    if row is None:
        sys.exit('level %s is not found in Levels' % args.level)
    level, sound = row
    columns = [r[1] for r in db.execute('PRAGMA table_info(AmbientSound)') if r[1] not in ('id', 'name')]

    if args.command == 'show':
        if sound is None:
            print('level %s has no ambient sound' % args.level)
        else:
            values = db.execute('SELECT %s FROM AmbientSound WHERE id = ?' % ', '.join(columns), (sound,)).fetchone()
            for column, value in zip(columns, values):
                print('%s = %s' % (column, 'NULL (engine default)' if value is None else value))
    elif args.command == 'set':
        if sound is None:
            sound = db.execute('INSERT INTO AmbientSound (name) VALUES (?)', (args.level,)).lastrowid
            db.execute('UPDATE Levels SET ambient_sound = ? WHERE id = ?', (sound, level))
            print('created AmbientSound %d for level %s' % (sound, args.level))
        for item in args.param or []:
            if '=' not in item:
                sys.exit('--param expects field=value: %s' % item)
            key, value = item.split('=', 1)
            if key not in columns:
                sys.exit('unknown field %s; fields: %s' % (key, ', '.join(columns)))
            db.execute('UPDATE AmbientSound SET %s = ? WHERE id = ?' % key, (value if value != '' else None, sound))
    elif args.command == 'remove':
        if sound is not None:
            db.execute('UPDATE Levels SET ambient_sound = NULL WHERE id = ?', (level,))
            db.execute('DELETE FROM AmbientSound WHERE id = ?', (sound,))
            print('removed ambient sound of level %s' % args.level)
    db.commit()


if __name__ == '__main__':
    main()
