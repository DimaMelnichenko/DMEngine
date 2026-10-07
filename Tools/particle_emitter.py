"""Эмиттеры частиц уровня в base.db3 (таблицы ParticleEmitters и LevelParticleEmitters, ParticleSystem, docs/particles.md).

Эмиттер — ассет: где рождаются частицы (точка, шар, поле вокруг камеры по маске плотности, быстрая вода симуляции), как
движутся (тяжесть, сопротивление, ветер, вихри, течение) и как выглядят (пятно или хвоинка, размер, цвет, просвет).
Экземпляр — эмиттер на уровне. add создаёт и то и другое: из готового набора (--preset) с заменой полей (--param).

  python Tools/particle_emitter.py list [--level Test]
  python Tools/particle_emitter.py add ИМЯ --preset pollen|needles|spray [--position x,y,z] [--param поле=значение ...]
  python Tools/particle_emitter.py set ИМЯ --param поле=значение [...]
  python Tools/particle_emitter.py delete ИМЯ / enable ИМЯ / disable ИМЯ [--level Test]

Поля — колонки ParticleEmitters (spawn, shape, radius, rate, size_start, color, gravity, drag, wind, curl, ...; цвет и
скорость — "r,g,b"). Первый запуск переносит базу со старой заготовки: таблица Particles и Levels.particles удаляются,
материал Particle — тоже, а небо (SkySphere), чей экземпляр 5 был на нём, получает свой экземпляр материала Texture с
Albedo = sky3. Сообщения — ASCII (Windows PowerShell 5.1, cmd).
"""
import argparse
import os
import sqlite3
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DB = os.path.join(ROOT, 'base.db3')

SCHEMA = """
CREATE TABLE IF NOT EXISTS ParticleEmitters (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL UNIQUE,
    spawn TEXT NOT NULL DEFAULT 'point',
    shape TEXT NOT NULL DEFAULT 'dot',
    radius REAL NOT NULL DEFAULT 1,
    height_min REAL NOT NULL DEFAULT 0,
    height_max REAL NOT NULL DEFAULT 0,
    mask TEXT,
    rate REAL NOT NULL DEFAULT 10,
    max_particles INTEGER NOT NULL DEFAULT 4096,
    lifetime_min REAL NOT NULL DEFAULT 1,
    lifetime_max REAL NOT NULL DEFAULT 2,
    size_start REAL NOT NULL DEFAULT 0.05,
    size_end REAL NOT NULL DEFAULT 0.05,
    color TEXT NOT NULL DEFAULT '1,1,1',
    alpha REAL NOT NULL DEFAULT 1,
    fade_in REAL NOT NULL DEFAULT 0.1,
    fade_out REAL NOT NULL DEFAULT 0.2,
    velocity TEXT NOT NULL DEFAULT '0,0,0',
    velocity_spread REAL NOT NULL DEFAULT 0,
    gravity REAL NOT NULL DEFAULT 0,
    drag REAL NOT NULL DEFAULT 0,
    wind REAL NOT NULL DEFAULT 0,
    curl REAL NOT NULL DEFAULT 0,
    curl_scale REAL NOT NULL DEFAULT 4,
    water_flow REAL NOT NULL DEFAULT 0,
    water_speed REAL NOT NULL DEFAULT 1,
    collide INTEGER NOT NULL DEFAULT 0,
    transmission REAL NOT NULL DEFAULT 0,
    emissive REAL NOT NULL DEFAULT 0
);
CREATE TABLE IF NOT EXISTS LevelParticleEmitters (
    id INTEGER PRIMARY KEY,
    level INTEGER NOT NULL REFERENCES Levels(id),
    emitter INTEGER NOT NULL REFERENCES ParticleEmitters(id) ON DELETE CASCADE,
    position TEXT NOT NULL DEFAULT '0,0,0',
    enabled INTEGER NOT NULL DEFAULT 1
);
"""

# Готовые наборы: пыльца и пух в солнце над лугом, сухая хвоя под ельником, брызги на перекатах
PRESETS = {
    'pollen': dict(spawn='camera', shape='dot', radius=20, height_min=0.3, height_max=4, mask='mask_grass', rate=15,
                   max_particles=8192, lifetime_min=8, lifetime_max=14, size_start=0.03, size_end=0.03,
                   color='1,0.95,0.8', alpha=1, fade_in=0.15, fade_out=0.25, velocity_spread=0.05, gravity=0.02,
                   drag=1.5, wind=0.4, curl=0.3, curl_scale=3, collide=1, transmission=2.5),
    'needles': dict(spawn='camera', shape='needle', radius=25, height_min=3, height_max=12, mask='mask_forest_spruce',
                    rate=30, max_particles=16384, lifetime_min=6, lifetime_max=10, size_start=0.1, size_end=0.1,
                    color='0.45,0.33,0.15', alpha=1, fade_in=0.05, fade_out=0.15, velocity_spread=0.2, gravity=9.8,
                    drag=6, wind=0.6, curl=0.5, curl_scale=2, collide=1, transmission=0.3),
    'spray': dict(spawn='water', shape='dot', radius=40, height_min=0, height_max=0.05, rate=800, max_particles=4096,
                  lifetime_min=0.5, lifetime_max=1.0, size_start=0.03, size_end=0.12, color='0.9,0.95,1', alpha=0.45,
                  fade_in=0.05, fade_out=0.5, velocity='0,1.5,0', velocity_spread=0.8, gravity=9.8, drag=0.5, wind=0.1,
                  water_flow=1, water_speed=0.9, collide=1, transmission=0.5),
}


def migrate(db):
    """Старая заготовка частиц: таблица Particles, Levels.particles, материал Particle (5) и экземпляр неба на нём"""
    db.executescript(SCHEMA)
    if db.execute("SELECT 1 FROM Materials WHERE id = 5 AND class = 'Particle'").fetchone():
        sky = db.execute("SELECT id FROM Textures WHERE name = 'sky3'").fetchone()
        albedo = db.execute("SELECT id_parameter_def FROM MaterialParameterDef WHERE id_material = 2 AND param_name = 'Albedo'").fetchone()
        cursor = db.execute("INSERT INTO MaterialInstance (id_material, name) VALUES (2, 'SkySphere')")
        if sky and albedo:
            db.execute('INSERT INTO MaterialParameterInstance (id_instance, id_material_def, value) VALUES (?, ?, ?)',
                       (cursor.lastrowid, albedo[0], str(sky[0])))
        db.execute('UPDATE ModelProperties SET material_instance_id = ? WHERE material_instance_id = 5', (cursor.lastrowid,))
        shaders = [row[0] for row in db.execute('SELECT shader_id FROM MaterialShaderLink WHERE material_id = 5')]
        db.execute('DELETE FROM MaterialParameterInstance WHERE id_instance = 5')
        db.execute('DELETE FROM MaterialInstance WHERE id_instance = 5')
        db.execute('DELETE FROM MaterialShaderLink WHERE material_id = 5')
        for shader in shaders:
            if not db.execute('SELECT 1 FROM MaterialShaderLink WHERE shader_id = ?', (shader,)).fetchone():
                db.execute('DELETE FROM Shader WHERE id = ?', (shader,))
        db.execute('DELETE FROM Materials WHERE id = 5')
        print('migrated: material Particle removed, SkySphere uses MaterialInstance %d (Texture, sky3)' % cursor.lastrowid)
    if 'particles' in [row[1] for row in db.execute('PRAGMA table_info(Levels)')]:
        db.execute('ALTER TABLE Levels DROP COLUMN particles')
        db.execute('DROP TABLE IF EXISTS Particles')
        print('migrated: table Particles and Levels.particles removed')


def level_id(db, level):
    row = db.execute('SELECT id FROM Levels WHERE name = ?', (level,)).fetchone()
    if row is None:
        sys.exit('level %s is not found in Levels' % level)
    return row[0]


def parse_params(items, columns):
    values = {}
    for item in items or []:
        if '=' not in item:
            sys.exit('--param expects field=value: %s' % item)
        key, value = item.split('=', 1)
        if key not in columns:
            sys.exit('unknown field %s; fields: %s' % (key, ', '.join(columns)))
        values[key] = value
    return values


def main():
    parser = argparse.ArgumentParser(description='Particle emitters of a level (ParticleEmitters, LevelParticleEmitters)')
    parser.add_argument('command', choices=['list', 'add', 'set', 'delete', 'enable', 'disable'])
    parser.add_argument('name', nargs='?')
    parser.add_argument('--level', default='Test')
    parser.add_argument('--preset', choices=sorted(PRESETS))
    parser.add_argument('--position', default='0,0,0', help='x,y,z of the instance (point, sphere)')
    parser.add_argument('--param', action='append', help='field=value, repeatable')
    args = parser.parse_args()

    db = sqlite3.connect(DB)
    db.execute('PRAGMA foreign_keys = ON')
    migrate(db)
    level = level_id(db, args.level)
    columns = [row[1] for row in db.execute('PRAGMA table_info(ParticleEmitters)') if row[1] not in ('id', 'name')]

    if args.command == 'list':
        rows = db.execute('SELECT e.name, l.enabled, e.spawn, e.shape, e.rate, e.radius, l.position FROM LevelParticleEmitters l '
                          'JOIN ParticleEmitters e ON e.id = l.emitter WHERE l.level = ? ORDER BY l.id', (level,)).fetchall()
        if not rows:
            print('no particle emitters')
        for name, enabled, spawn, shape, rate, radius, position in rows:
            print('%s %s spawn %s shape %s rate %g radius %g position %s' %
                  (name, 'enabled' if enabled else 'disabled', spawn, shape, rate, radius, position))
        db.commit()
        return
    if not args.name:
        sys.exit('name is required')

    if args.command == 'add':
        values = dict(PRESETS[args.preset]) if args.preset else {}
        values.update(parse_params(args.param, columns))
        db.execute('DELETE FROM ParticleEmitters WHERE name = ?', (args.name,))
        names = ['name'] + list(values)
        cursor = db.execute('INSERT INTO ParticleEmitters (%s) VALUES (%s)' % (', '.join(names), ', '.join('?' * len(names))),
                            [args.name] + list(values.values()))
        db.execute('INSERT INTO LevelParticleEmitters (level, emitter, position) VALUES (?, ?, ?)',
                   (level, cursor.lastrowid, args.position))
    else:
        row = db.execute('SELECT id FROM ParticleEmitters WHERE name = ?', (args.name,)).fetchone()
        if row is None:
            sys.exit('particle emitter %s is not found' % args.name)
        if args.command == 'set':
            for key, value in parse_params(args.param, columns).items():
                db.execute('UPDATE ParticleEmitters SET %s = ? WHERE id = ?' % key, (value, row[0]))
        elif args.command == 'delete':
            db.execute('DELETE FROM LevelParticleEmitters WHERE emitter = ?', (row[0],))
            db.execute('DELETE FROM ParticleEmitters WHERE id = ?', (row[0],))
        else:
            db.execute('UPDATE LevelParticleEmitters SET enabled = ? WHERE emitter = ? AND level = ?',
                       (1 if args.command == 'enable' else 0, row[0], level))
    db.commit()
    print('%s %s: done' % (args.command, args.name))


if __name__ == '__main__':
    main()
