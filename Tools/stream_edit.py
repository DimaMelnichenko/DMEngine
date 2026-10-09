"""Кривые русел уровня в base.db3 (таблицы Streams / StreamPoints, как Landscape Splines в UE; docs/water.md, «Кривые русел»).

Генератор движка (TerrainHydrology::generate) предлагает кривые по стоку и пишет их в базу при загрузке, когда сменились
рельеф или параметры генератора (отпечаток — таблица StreamGeneration). Правленная кривая (edited) и ручная (generated = 0)
при новой генерации остаются, сгенерированные в их полосе обрезаются. Любая правка здесь помечает кривую правленной.
Множители (--width-scale и т. п.) — к общим параметрам WaterChannels, 1 — как у всех. Применяется при следующей загрузке.

  python Tools/stream_edit.py list [--level Test]
  python Tools/stream_edit.py show ID
  python Tools/stream_edit.py set ID [--name N] [--enabled 0|1] [--width-scale S] [--depth-scale S] [--freeboard-scale S]
         [--bank-slope-scale S] [--thalweg-scale S] [--roughness-scale S] [--discharge-scale S]
  python Tools/stream_edit.py move ID POINT x,z           (точка POINT — в x, z мира)
  python Tools/stream_edit.py insert ID AFTER x,z         (новая точка после AFTER; -1 — в начало)
  python Tools/stream_edit.py remove ID POINT
  python Tools/stream_edit.py add NAME --points "x,z;x,z;..." [--discharge Q] [--level Test]
  python Tools/stream_edit.py delete ID
  python Tools/stream_edit.py reset ID                    (снова сгенерированная: заменится при следующей генерации)
  python Tools/stream_edit.py regenerate [--level Test]   (генерация при следующей загрузке; правленные остаются)

Точки — от истока вниз по течению. Расход ручной кривой — --discharge, м3/с, иначе по водосбору под точкой. Сообщения —
ASCII (Windows PowerShell 5.1, cmd).
"""
import argparse
import math
import os
import sqlite3
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DB = os.path.join(ROOT, 'base.db3')

SCALES = ['width_scale', 'depth_scale', 'freeboard_scale', 'bank_slope_scale', 'thalweg_scale', 'roughness_scale',
          'discharge_scale']


def water_simulation(db, level):
    row = db.execute('SELECT water_simulation FROM Levels WHERE name = ?', (level,)).fetchone()
    if not row or row[0] is None:
        sys.exit('level %s has no water simulation' % level)
    return row[0]


def stream(db, stream_id):
    row = db.execute('SELECT id, water_simulation, name, generated, edited, enabled FROM Streams WHERE id = ?',
                     (stream_id,)).fetchone()
    if not row:
        sys.exit('no stream %d' % stream_id)
    return row


def points(db, stream_id):
    return db.execute('SELECT point, x, z, discharge FROM StreamPoints WHERE stream = ? ORDER BY point',
                      (stream_id,)).fetchall()


def write_points(db, stream_id, values):
    db.execute('DELETE FROM StreamPoints WHERE stream = ?', (stream_id,))
    db.executemany('INSERT INTO StreamPoints (stream, point, x, z, discharge) VALUES (?, ?, ?, ?, ?)',
                   [(stream_id, i, x, z, q) for i, (x, z, q) in enumerate(values)])
    db.execute('UPDATE Streams SET edited = 1 WHERE id = ?', (stream_id,))


def parse_xz(text):
    parts = [float(v) for v in text.split(',')]
    if len(parts) != 2:
        sys.exit('point must be x,z: %s' % text)
    return parts


def length(values):
    return sum(math.hypot(b[0] - a[0], b[1] - a[1]) for a, b in zip(values, values[1:]))


def main():
    parser = argparse.ArgumentParser(description='Stream curves of a level (Streams / StreamPoints)')
    sub = parser.add_subparsers(dest='command', required=True)
    p = sub.add_parser('list')
    p.add_argument('--level', default='Test')
    p = sub.add_parser('show')
    p.add_argument('id', type=int)
    p = sub.add_parser('set')
    p.add_argument('id', type=int)
    p.add_argument('--name')
    p.add_argument('--enabled', type=int, choices=[0, 1])
    for scale in SCALES:
        p.add_argument('--' + scale.replace('_', '-'), type=float)
    p = sub.add_parser('move')
    p.add_argument('id', type=int)
    p.add_argument('point', type=int)
    p.add_argument('xz')
    p = sub.add_parser('insert')
    p.add_argument('id', type=int)
    p.add_argument('after', type=int)
    p.add_argument('xz')
    p = sub.add_parser('remove')
    p.add_argument('id', type=int)
    p.add_argument('point', type=int)
    p = sub.add_parser('add')
    p.add_argument('name')
    p.add_argument('--points', required=True)
    p.add_argument('--discharge', type=float)
    p.add_argument('--level', default='Test')
    p = sub.add_parser('delete')
    p.add_argument('id', type=int)
    p = sub.add_parser('reset')
    p.add_argument('id', type=int)
    p = sub.add_parser('regenerate')
    p.add_argument('--level', default='Test')
    args = parser.parse_args()

    db = sqlite3.connect(DB)
    if args.command == 'list':
        water = water_simulation(db, args.level)
        rows = db.execute('SELECT id, name, generated, edited, enabled FROM Streams WHERE water_simulation = ? ORDER BY id',
                          (water,)).fetchall()
        for sid, name, generated, edited, enabled in rows:
            values = [(x, z) for _, x, z, _ in points(db, sid)]
            kind = ('generated' if generated else 'manual') + (' edited' if edited else '') + ('' if enabled else ' off')
            first = values[0] if values else (0, 0)
            print('%4d  %-12s %-22s points %4d  length %7.1f m  from %.0f,%.0f' % (
                sid, name, kind, len(values), length(values), first[0], first[1]))
        key = db.execute('SELECT key FROM StreamGeneration WHERE water_simulation = ?', (water,)).fetchone()
        print('streams: %d, generation key: %s' % (len(rows), key[0] if key else '(none - generated at the next load)'))
    elif args.command == 'show':
        sid, water, name, generated, edited, enabled = stream(db, args.id)
        scales = db.execute('SELECT ' + ', '.join(SCALES) + ' FROM Streams WHERE id = ?', (sid,)).fetchone()
        print('%d %s: %s%s%s' % (sid, name, 'generated' if generated else 'manual', ', edited' if edited else '',
                                 '' if enabled else ', off'))
        print('  ' + ', '.join('%s %s' % (s, 1.0 if v is None else v) for s, v in zip(SCALES, scales)))
        for point, x, z, q in points(db, sid):
            print('  %4d  %8.2f %8.2f  %s' % (point, x, z, '' if q is None else 'Q %.4f m3/s' % q))
    elif args.command == 'set':
        stream(db, args.id)
        if args.name is not None:
            db.execute('UPDATE Streams SET name = ? WHERE id = ?', (args.name, args.id))
        if args.enabled is not None:
            db.execute('UPDATE Streams SET enabled = ? WHERE id = ?', (args.enabled, args.id))
        for scale in SCALES:
            value = getattr(args, scale)
            if value is not None:
                db.execute('UPDATE Streams SET %s = ? WHERE id = ?' % scale, (value, args.id))
        db.execute('UPDATE Streams SET edited = 1 WHERE id = ?', (args.id,))
        print('stream %d updated (edited)' % args.id)
    elif args.command in ('move', 'insert', 'remove'):
        stream(db, args.id)
        values = [[x, z, q] for _, x, z, q in points(db, args.id)]
        if args.command == 'move':
            if not 0 <= args.point < len(values):
                sys.exit('no point %d' % args.point)
            values[args.point][0:2] = parse_xz(args.xz)
        elif args.command == 'insert':
            if not -1 <= args.after < len(values):
                sys.exit('no point %d' % args.after)
            x, z = parse_xz(args.xz)
            # Расход новой точки — как у соседней выше по течению
            q = values[args.after][2] if args.after >= 0 else (values[0][2] if values else None)
            values.insert(args.after + 1, [x, z, q])
        else:
            if not 0 <= args.point < len(values) or len(values) <= 2:
                sys.exit('no point %d or the curve would be shorter than 2 points' % args.point)
            del values[args.point]
        write_points(db, args.id, values)
        print('stream %d: %d points (edited)' % (args.id, len(values)))
    elif args.command == 'add':
        water = water_simulation(db, args.level)
        values = [parse_xz(text) for text in args.points.split(';') if text.strip()]
        if len(values) < 2:
            sys.exit('a stream needs 2 points or more')
        cursor = db.execute('INSERT INTO Streams (water_simulation, name, generated, edited, enabled) VALUES (?, ?, 0, 1, 1)',
                            (water, args.name))
        write_points(db, cursor.lastrowid, [(x, z, args.discharge) for x, z in values])
        print('stream %d %s: %d points, length %.1f m (manual)' % (cursor.lastrowid, args.name, len(values), length(values)))
    elif args.command == 'delete':
        sid, water, name, generated, _, _ = stream(db, args.id)
        db.execute('DELETE FROM StreamPoints WHERE stream = ?', (sid,))
        db.execute('DELETE FROM Streams WHERE id = ?', (sid,))
        if generated:
            # Сгенерированная вернулась бы при следующей генерации — выключенная правленная держит место
            print('note: a generated stream comes back at the next generation; "set ID --enabled 0" keeps it off')
        print('stream %d %s deleted' % (sid, name))
    elif args.command == 'reset':
        sid, water, name, generated, _, _ = stream(db, args.id)
        if not generated:
            sys.exit('stream %d is manual: delete it instead' % sid)
        db.execute('UPDATE Streams SET edited = 0 WHERE id = ?', (sid,))
        db.execute('DELETE FROM StreamGeneration WHERE water_simulation = ?', (water,))
        print('stream %d %s: generated again at the next load' % (sid, name))
    elif args.command == 'regenerate':
        water = water_simulation(db, args.level)
        db.execute('DELETE FROM StreamGeneration WHERE water_simulation = ?', (water,))
        print('stream curves are generated at the next load (edited and manual stay)')
    db.commit()


if __name__ == '__main__':
    main()
