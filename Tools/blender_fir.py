# Ель для уровня Test (этап 9, деревья): геометрия — своя, текстуры и карточки веточек — из ели Poly Haven
# (fir_tree_01, CC0: https://polyhaven.com/a/fir_tree_01; архив .blend — в DownloadResources\). Запуск внутри Blender
# без окна:
#   blender -b --factory-startup --python Tools/blender_fir.py -- --archive DownloadResources/fir_tree_01_2k.blend.zip
#       --out Meshes/source/fir.glb
# Затем импорт моделями уровня на землю:
#   python Tools/import_gltf.py Meshes/source/fir.glb --level Test --position 480,0,345 --snap-to-terrain --lod-ranges 35,90
# У ели Poly Haven LOD0 — 0,5–4 млн треугольников, у LOD1 половина — плотный ствол, низких LOD нет, силуэт лесной
# (голый ствол, крона сверху), а данных для ветра нет. Поэтому сценарий строит ель сам — конус, как в Valley, с
# известной иерархией ствол → мутовки ветвей → веточки:
#   - ствол — сужающаяся труба с лёгкой кривизной, от 0,3 м ниже нуля (на склоне не висит), развёртка цилиндрическая;
#   - мутовки — через 0,3–0,5 м (к верхушке чаще), по 4–6 ветвей; длина ветви — по конусу кроны, внизу ветви свисают,
#     к кончику загибаются вверх; ветвь — тонкая труба коры;
#   - хвоя — карточки веточек Poly Haven (готовые плоскости с развёрткой в атлас хвои) вдоль ветви в обе стороны,
#     на кончике — веточка-кончик; у лидера наверху — вертикальная веточка.
# Варианты: Fir_A — 20 м, крона почти от земли (одиночная у опушки), Fir_B — 14 м, крона с трети высоты и сухие сучья
# ниже (ель из леса), Fir_C — молодая, 7 м. LOD (суффикс _LOD<N>, дальности — --lod-ranges при импорте):
#   - LOD0 — ствол 12 граней, ветви 4 грани, все карточки (LOD1 Poly Haven);
#   - LOD1 — ствол 8 граней, ветви длиннее метра 3 грани, карточки через одну крупнее (LOD2 Poly Haven);
#   - LOD2 — ствол 5 граней, по одной крупной карточке на ветвь. Импостер — вместе с лесом (TODO.md).
# Материалы — секции: FirBark (кора, непрозрачная) и FirTwig (хвоя, Masked, двусторонняя). Параметры движка, которых
# нет в glTF, — Custom Properties материала (в glTF — extras, импортёр пишет их в экземпляр): пропускание хвои,
# смена LOD дизерингом. Сцена — по одной ели каждого варианта у луга и роща из связанных дубликатов (экземпляры
# одной модели); origin файла — --position импорта, высота — по земле (--snap-to-terrain).
# Сообщения скрипта — ASCII (правило Tools/).
import argparse
import math
import os
import sys

import bpy
import numpy as np
from mathutils import Matrix, Vector

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import export_polyhaven as polyhaven   # noqa: E402 — open_archive, export_material

TEXTURE_SIZE = 1024       # 2k PNG грузится движком ~1 с на картинку
BARK_TILE = 0.8           # м коры на повтор текстуры
GROUND_CLEARANCE = 0.4    # м: ниже нижние ветви не свисают

VARIANTS = {
    #          высота, низ кроны (доля), радиус ствола, радиус кроны, seed
    'Fir_A': dict(height=20.0, crown_base=0.05, radius=0.3, crown_radius=3.6, seed=11),
    'Fir_B': dict(height=14.0, crown_base=0.32, radius=0.2, crown_radius=2.5, seed=23),
    'Fir_C': dict(height=7.0, crown_base=0.03, radius=0.1, crown_radius=1.6, seed=37),
}

# LOD: грани ствола, шаг колец ствола (м), грани ветви (0 — без ветвей), доля карточек, масштаб карточек, LOD карточек
# Poly Haven; LOD2 — одна карточка на ветвь
LODS = [
    dict(trunk_sides=12, trunk_step=0.4, branch_sides=4, branch_min=0.0, card_every=1, card_scale=1.0, source='LOD1'),
    dict(trunk_sides=8, trunk_step=1.0, branch_sides=3, branch_min=1.0, card_every=2, card_scale=1.3, source='LOD2'),
    dict(trunk_sides=5, trunk_step=2.5, branch_sides=0, branch_min=0.0, card_every=0, card_scale=1.0, source='LOD2'),
]

MAIN_CARDS = ['twig_main_a', 'twig_main_b', 'twig_main_c', 'twig_tip_d']
TIP_CARDS = ['twig_tip_a', 'twig_tip_b', 'twig_tip_c']

# Роща: число елей по вариантам, радиус пятна, наименьшее расстояние между стволами; витрина — ели по одной у луга
GROVE = {'Fir_A': 5, 'Fir_B': 9, 'Fir_C': 6}
GROVE_RADIUS = 22.0
GROVE_SPACING = 4.5
SHOWCASE = {'Fir_A': (-14.0, -27.0), 'Fir_B': (0.0, -30.0), 'Fir_C': (12.0, -27.0)}


def parse_args():
    argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    parser = argparse.ArgumentParser(description='Fir tree for the Test level (glTF for Tools/import_gltf.py)')
    parser.add_argument('--archive', required=True, help='Poly Haven fir_tree_01 .blend.zip')
    parser.add_argument('--out', required=True, help='output .glb')
    return parser.parse_args(argv)


# --- Источник: карточки и материалы Poly Haven ------------------------------------------------------------------------

def read_cards():
    """Карточки веточек: вершины (основание у y = 0, растёт вдоль +Y, ширина — X, изгиб — Z), развёртка по углам,
    треугольники; длина — по Y. Ключ — имя без префикса и суффикса и LOD Poly Haven"""
    cards = {}
    for obj in bpy.data.objects:
        name = obj.name.replace('fir_tree_01_', '')
        if obj.type != 'MESH' or not name.startswith('twig_') or '_LOD' not in name:
            continue
        base, lod = name.rsplit('_', 1)
        me = obj.data
        me.calc_loop_triangles()
        co = np.array([v.co[:] for v in me.vertices], np.float32)
        uv = me.uv_layers.active.data
        tris = [(tuple(t.vertices), tuple(uv[i].uv[:] for i in t.loops)) for t in me.loop_triangles]
        cards[(base, lod)] = dict(co=co, tris=tris, length=float(co[:, 1].max()))
    missing = [(b, l) for b in MAIN_CARDS + TIP_CARDS for l in ('LOD1', 'LOD2') if (b, l) not in cards]
    if missing:
        raise SystemExit('error: twig cards are not found in the archive: %s' % missing)
    return cards


def make_materials(folder):
    bark = polyhaven.export_material(bpy.data.materials['fir_tree_01_bark'], folder, TEXTURE_SIZE, False)
    twig = polyhaven.export_material(bpy.data.materials['fir_tree_01_twig'], folder, TEXTURE_SIZE, True)
    bark.name, twig.name = 'FirBark', 'FirTwig'
    # Параметры движка (docs/materials.md): extras glTF → экземпляр материала
    for mat in (bark, twig):
        mat['DitheredLODTransition'] = 'true'
    twig['DiffuseTransmissionFactor'] = 0.35
    twig['DiffuseTransmissionColorFactor'] = [1.0, 1.0, 0.6]
    twig['DiffuseTransmissionColor'] = 'BaseColor'
    return bark, twig


# --- Геометрия ---------------------------------------------------------------------------------------------------------

class Builder:
    """Меш LOD: вершины, треугольники с развёрткой по углам и материалом (0 — кора, 1 — хвоя)"""

    def __init__(self):
        self.verts = []
        self.faces = []
        self.uvs = []
        self.mats = []

    def vertex(self, p):
        self.verts.append(tuple(p))
        return len(self.verts) - 1

    def face(self, indices, uvs, mat):
        self.faces.append(tuple(indices))
        self.uvs.append(tuple(uvs))
        self.mats.append(mat)

    def tube(self, points, radii, sides, v_scale, twist=0.0):
        """Труба коры по ломаной: кольцо на точку, развёртка — вокруг и вдоль (BARK_TILE на повтор)"""
        u_tiles = max(1, round(2.0 * math.pi * radii[0] / BARK_TILE))
        rings = []
        length = 0.0
        for k, p in enumerate(points):
            if k:
                length += (points[k] - points[k - 1]).length
            forward = (points[min(k + 1, len(points) - 1)] - points[max(k - 1, 0)]).normalized()
            side = forward.cross(Vector((0.0, 0.0, 1.0)))
            if side.length < 1e-4:
                side = Vector((1.0, 0.0, 0.0))
            side.normalize()
            up = side.cross(forward)
            ring = []
            for s in range(sides + 1):
                a = 2.0 * math.pi * s / sides + twist
                ring.append((self.vertex(p + (side * math.cos(a) + up * math.sin(a)) * radii[k]),
                             (u_tiles * s / sides, length * v_scale / BARK_TILE)))
            rings.append(ring)
        # Кольцо обходит ось по часовой стрелке, если смотреть вдоль трубы: грани — против, нормалью наружу
        for k in range(len(rings) - 1):
            for s in range(sides):
                a, b = rings[k][s], rings[k][s + 1]
                c, d = rings[k + 1][s + 1], rings[k + 1][s]
                self.face((a[0], c[0], b[0]), (a[1], c[1], b[1]), 0)
                self.face((a[0], d[0], c[0]), (a[1], d[1], c[1]), 0)

    def card(self, card, origin, forward, up, length):
        """Карточка веточки: основание в origin, длинная ось (Y карточки) — forward, нормаль (Z) — примерно up"""
        side = forward.cross(up).normalized()
        normal = side.cross(forward).normalized()
        scale = length / card['length']
        basis = Matrix((side, forward, normal)).transposed()   # столбцы — оси X, Y, Z карточки
        first = len(self.verts)
        for v in card['co']:
            self.verts.append(tuple(origin + basis @ Vector(v) * scale))
        for tri, uv in card['tris']:
            self.face(tuple(first + i for i in tri), uv, 1)

    def build(self, name, materials):
        mesh = bpy.data.meshes.new(name)
        mesh.from_pydata(self.verts, [], self.faces)
        for mat in materials:
            mesh.materials.append(mat)
        layer = mesh.uv_layers.new(name='UVMap')
        flat = [uv for face in self.uvs for uv in face]
        layer.data.foreach_set('uv', np.array(flat, np.float32).ravel())
        mesh.polygons.foreach_set('material_index', np.array(self.mats, np.int32))
        mesh.polygons.foreach_set('use_smooth', np.ones(len(self.faces), bool))
        mesh.validate()
        mesh.update()
        return mesh


def trunk_offset(z, height, phase):
    """Лёгкая кривизна ствола: боковое смещение по высоте, к верхушке меньше"""
    t = z / height
    amplitude = 0.012 * height * math.sin(math.pi * t)
    return Vector((amplitude * math.sin(2.1 * t * math.pi + phase), amplitude * math.cos(1.3 * t * math.pi + phase), 0.0))


def trunk_radius(z, variant):
    t = max(z, 0.0) / variant['height']
    flare = 1.0 + 0.35 * math.exp(-max(z, 0.0) / (0.06 * variant['height']))   # расширение у корней
    return max(variant['radius'] * (1.0 - t) ** 0.9 * flare, 0.008)


def make_branches(variant, rng):
    """Ветви ели: высота крепления, азимут, длина, начальный наклон и загиб к кончику (радианы); сухие — без хвои"""
    height, base = variant['height'], variant['crown_base'] * variant['height']
    branches = []
    # Сухие сучья ниже кроны (у ели из леса)
    z = 0.25 * height
    while z < base - 0.3:
        for _ in range(rng.integers(1, 4)):
            branches.append(dict(z=z, azimuth=rng.uniform(0.0, 2.0 * math.pi), length=rng.uniform(0.3, 0.9),
                                 elevation=math.radians(rng.uniform(-30.0, -5.0)), curl=0.0, dead=True))
        z += rng.uniform(0.4, 0.8)
    z = max(base, 0.4)
    while z < height - 0.35:
        rel = z / height
        crown = (height - z) / (height - base)   # 1 у низа кроны, 0 у верхушки
        count = int(rng.integers(4, 7))
        azimuth0 = rng.uniform(0.0, 2.0 * math.pi)
        for i in range(count):
            branch = dict(
                z=z + rng.uniform(-0.05, 0.05),
                azimuth=azimuth0 + 2.0 * math.pi * i / count + rng.normal(0.0, 0.25),
                length=variant['crown_radius'] * crown ** 0.85 * rng.uniform(0.8, 1.1) + 0.15,
                elevation=math.radians(-35.0 + 60.0 * rel + rng.normal(0.0, 5.0)),
                curl=math.radians(30.0 - 15.0 * rel), dead=False)
            # Нижние ветви свисают не до земли: с карточками (они свисают ещё на ~0,3 м) — выше GROUND_CLEARANCE.
            # Точка крепления не в счёт: у нижней мутовки она сама бывает ниже
            for _ in range(20):
                points = branch_points(branch, Vector((0.0, 0.0, branch['z'])), 5)
                if min(p.z for p in points[1:]) >= GROUND_CLEARANCE:
                    break
                branch['elevation'] += math.radians(4.0)
            branches.append(branch)
        spacing = (0.5 - 0.2 * rel) * math.sqrt(height / 20.0)
        z += spacing * rng.uniform(0.85, 1.15)
    return branches


def branch_points(branch, start, segments):
    """Ломаная ветви: наклон растёт к кончику (ветвь свисает и загибается вверх)"""
    points = [start]
    step = branch['length'] / segments
    for k in range(segments):
        s = (k + 0.5) / segments
        elevation = branch['elevation'] + branch['curl'] * s * s
        d = Vector((math.cos(branch['azimuth']) * math.cos(elevation), math.sin(branch['azimuth']) * math.cos(elevation),
                    math.sin(elevation)))
        points.append(points[-1] + d * step)
    return points


def point_on(points, s):
    """Точка и направление ломаной на доле длины s"""
    lengths = [(points[k + 1] - points[k]).length for k in range(len(points) - 1)]
    target = s * sum(lengths)
    for k, segment in enumerate(lengths):
        if target <= segment or k == len(lengths) - 1:
            f = min(target / max(segment, 1e-6), 1.0)
            return points[k].lerp(points[k + 1], f), (points[k + 1] - points[k]).normalized()
        target -= segment


def rotate(v, axis, angle):
    return Matrix.Rotation(angle, 3, axis) @ v


def build_fir(name, variant, lod, cards, rng_seed):
    rng = np.random.default_rng(rng_seed)
    height = variant['height']
    phase = rng.uniform(0.0, 2.0 * math.pi)
    branches = make_branches(variant, rng)
    builder = Builder()

    # Ствол
    zs = list(np.arange(-0.3, height - 0.2, lod['trunk_step'])) + [height]
    points = [Vector((0.0, 0.0, z)) + trunk_offset(z, height, phase) for z in zs]
    builder.tube(points, [trunk_radius(z, variant) for z in zs], lod['trunk_sides'], 1.0)

    card_rng = np.random.default_rng(rng_seed + 1000)
    source = lod['source']
    for index, branch in enumerate(branches):
        z = branch['z']
        start = Vector((0.0, 0.0, z)) + trunk_offset(z, height, phase)
        segments = 5 if lod['trunk_sides'] >= 12 else 3
        pts = branch_points(branch, start, segments)
        # Случайные числа карточек не зависят от LOD: одни и те же веточки на одних и тех же местах
        rolls = card_rng.uniform(-0.4, 0.4, 16)
        picks = card_rng.integers(0, 1 << 30, 16)
        if lod['branch_sides'] and branch['length'] >= lod['branch_min']:
            radius = 0.012 + 0.012 * branch['length']
            builder.tube(pts, [radius * (1.0 - 0.85 * k / segments) for k in range(segments + 1)], lod['branch_sides'],
                         1.0, twist=branch['azimuth'])
        if branch['dead']:
            continue

        outward = Vector((math.cos(branch['azimuth']), math.sin(branch['azimuth']), 0.0))
        up = Vector((0.0, 0.0, 1.0))
        if lod['card_every'] == 0:
            # Дальний LOD: одна крупная карточка на ветвь — от основания почти до кончика; крупнее — крона гуще,
            # чем у ближних LOD, и на смене LOD заметно «толстеет»
            p, d = point_on(pts, 0.15)
            forward = (pts[-1] - p).normalized()
            builder.card(cards[(MAIN_CARDS[picks[0] % 3], source)], p, forward, up, branch['length'] * 0.8)
            continue

        # Веточки вдоль ветви в обе стороны, от основания к кончику короче; на кончике — веточка-кончик
        spacing = 0.28 * lod['card_every']
        count = max(1, int(branch['length'] * 0.85 / spacing))
        for k in range(count):
            s = 0.12 + 0.8 * (k + 0.5) / count
            p, d = point_on(pts, s)
            sign = 1.0 if (k + index) % 2 == 0 else -1.0
            forward = rotate(d, up, sign * math.radians(55.0))
            forward = rotate(forward, forward.cross(up).normalized() if forward.cross(up).length > 1e-4 else outward,
                             -math.radians(12.0))
            length = min(1.0, 0.3 + 0.45 * branch['length'] * (1.0 - s)) * lod['card_scale']
            card = cards[(MAIN_CARDS[picks[k % 16] % len(MAIN_CARDS)], source)]
            builder.card(card, p, forward, rotate(up, forward, rolls[k % 16]), length)
        p, d = point_on(pts, 0.92)
        tip = cards[(TIP_CARDS[picks[15] % len(TIP_CARDS)], source)]
        builder.card(tip, p, d, rotate(up, d, rolls[15]), min(0.7, 0.25 + 0.2 * branch['length']) * lod['card_scale'])

    # Лидер: верхушка — вертикальная веточка
    top = Vector((0.0, 0.0, height - 0.5)) + trunk_offset(height - 0.5, height, phase)
    builder.card(cards[('twig_tip_b', source)], top, Vector((0.0, 0.0, 1.0)), Vector((1.0, 0.0, 0.0)),
                 0.9 * lod['card_scale'])
    return builder


def triangle_count(mesh):
    mesh.calc_loop_triangles()
    return len(mesh.loop_triangles)


def grove_positions(rng):
    """Стволы рощи в пятне GROVE_RADIUS не ближе GROVE_SPACING друг к другу (отбор с отказом)"""
    positions = []
    while len(positions) < sum(GROVE.values()):
        p = rng.uniform(-GROVE_RADIUS, GROVE_RADIUS, 2)
        if p @ p <= GROVE_RADIUS ** 2 and all(np.hypot(*(p - q)) >= GROVE_SPACING for q in positions):
            positions.append(p)
    return positions


def main():
    args = parse_args()
    folder = polyhaven.open_archive(os.path.abspath(args.archive))
    cards = read_cards()
    bark, twig = make_materials(folder)

    # Сцена Poly Haven больше не нужна: остаются только новые объекты
    for obj in list(bpy.data.objects):
        bpy.data.objects.remove(obj, do_unlink=True)
    scene = bpy.context.scene

    meshes = {}
    for name, variant in VARIANTS.items():
        meshes[name] = []
        for index, lod in enumerate(LODS):
            mesh = build_fir(name, variant, lod, cards, variant['seed']).build(
                name if index == 0 else '%s_LOD%d' % (name, index), [bark, twig])
            meshes[name].append(mesh)
        print('%s: %.0f m, %s' % (name, variant['height'], ', '.join(
            'LOD%d %d tris' % (i, triangle_count(m)) for i, m in enumerate(meshes[name]))))

    def place(name, suffix, x, y, angle):
        for index, mesh in enumerate(meshes[name]):
            obj_name = name + suffix + ('' if index == 0 else '_LOD%d' % index)
            obj = bpy.data.objects.new(obj_name, mesh)
            obj.location = (x, y, 0.0)
            obj.rotation_euler = (0.0, 0.0, angle)
            scene.collection.objects.link(obj)

    rng = np.random.default_rng(5)
    for name, (x, y) in SHOWCASE.items():
        place(name, '', x, y, rng.uniform(0.0, 2.0 * math.pi))
    positions = grove_positions(rng)
    order = [name for name, count in GROVE.items() for _ in range(count)]
    rng.shuffle(order)
    for k, (name, p) in enumerate(zip(order, positions)):
        place(name, '_g%02d' % k, float(p[0]), float(p[1]), rng.uniform(0.0, 2.0 * math.pi))

    out = os.path.abspath(args.out)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    bpy.ops.export_scene.gltf(filepath=out, export_format='GLB', export_yup=True, export_apply=True,
                              export_tangents=True, export_image_format='AUTO', export_materials='EXPORT',
                              export_extras=True, export_animations=False)
    print('Written: ' + out)


main()
