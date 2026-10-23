# Кочки осоки (Carex, кочкарная осока) для слоя расстановки у самой воды ручьёв и озёр (набор Meadow, вода «Bank»,
# docs/scatter.md, «Осока у воды»). Запускается внутри Blender без окна:
#   blender -b --factory-startup --python Tools/blender_sedge.py -- <выход.glb>
# Затем импорт моделями для расстановки:
#   python Tools/import_gltf.py <выход.glb> --scatter --lod-ranges 8,20 --shadow-min-lod 1
# Сцена — четыре кочки (варианты слоя), у каждой LOD0…LOD2:
#   Sedge_A — старая высокая: 110 листьев 0,65–0,9 м, кочка 0,07 м, сухих — 30 %, 4 стебля с колосками;
#   Sedge_B — средняя: 90 листьев 0,5–0,75 м, кочка 0,05 м, 2 стебля;
#   Sedge_C — молодая: 60 листьев 0,35–0,55 м, едва поднята, без стеблей;
#   Sedge_D — на кочке повыше, 0,11 м, сухих — 45 %, 3 стебля.
# Кочка — бугорок из старых оснований листьев: не сплошной купол (вблизи он читался гладкой бурой скорлупой), а юбка
# коротких широких волокон от верха кочки дугой вниз к земле. Из верха и с боков кочки — фонтан узких листьев (6–9 мм)
# геометрией, как травинки GrassClump: внутренние стоят, внешние раскрыты и дугой поникают (у земли ложатся вдоль неё, не
# в неё), к кончику лист сужается в острие; сухие прошлогодние листья соломенного цвета с края верха свисают по бокам.
# Стебли — тонкие трёхгранные, выше листьев, на верхушке тёмные колоски (верхний — тонкий мужской, ниже — толще женские).
# Один материал SedgeMat двусторонний, атлас SedgeColor: столбцы 0–3 — оттенки зелёного листа по высоте (бледное
# основание, зелень, желтоватый кончик), 4–5 — сухой лист, 6 — волокна кочки (у верха солома, к земле бурые), 7 — стебель
# внизу и колосок вверху. Параметры движка, которых нет в glTF (ветер, просвет, смена LOD дизерингом), — Custom
# Properties материала: extras.
# Origin — у земли в центре кочки (ось Z Blender вверх), волокна уходят чуть ниже нуля: на неровном берегу не висят.
# Сообщения скрипта — ASCII (правило Tools/).
import math
import os
import sys

import bpy
import numpy as np

COLUMNS = 8          # столбцов атласа: 4 оттенка зелёного листа, 2 сухого, волокна кочки, стебель с колоском
GREEN_COLUMNS = 4
DRY_COLUMN = 4       # 4 и 5
BASE_COLUMN = 6
CULM_COLUMN = 7
SEED = 23
# Варианты: имя, листьев, длина листа (м), радиус и высота кочки (м), доля сухих листьев, стеблей
VARIANTS = [
    ('Sedge_A', 110, (0.65, 0.9), 0.14, 0.07, 0.3, 4),
    ('Sedge_B', 90, (0.5, 0.75), 0.12, 0.05, 0.25, 2),
    ('Sedge_C', 60, (0.35, 0.55), 0.08, 0.02, 0.15, 0),
    ('Sedge_D', 95, (0.55, 0.8), 0.15, 0.11, 0.45, 3),
]
# LOD: доля листьев (и волокон кочки), сегментов листа, множитель ширины листа, граней колоска
LODS = [(1.0, 6, 1.0, 4), (0.6, 4, 1.4, 3), (0.35, 2, 2.0, 3)]


def output_path():
    argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    if not argv:
        raise SystemExit('usage: blender -b --factory-startup --python Tools/blender_sedge.py -- <out.glb>')
    return os.path.abspath(argv[0])


def clear_scene():
    for obj in list(bpy.data.objects):
        bpy.data.objects.remove(obj, do_unlink=True)


def sedge_color(folder, width=128, height=256):
    """Атлас: столбцы — оттенки листа, сухой лист, волокна кочки, стебель; строки — высота (пиксели Blender идут снизу вверх)."""
    rng = np.random.default_rng(SEED)
    t = (np.arange(height) + 0.5) / height
    column_width = width // COLUMNS
    u = (np.arange(column_width) + 0.5) / column_width
    rgb = np.zeros((height, width, 3))

    def put(column, values):
        rgb[:, column * column_width:(column + 1) * column_width] = values

    # Зелёный лист: бледное основание → зелень с сизым оттенком осоки → к кончику желтеет и подсыхает
    base = np.array([0.56, 0.56, 0.36])
    green = np.array([0.22, 0.34, 0.13])
    upper = np.array([0.30, 0.40, 0.15])
    tip = np.array([0.52, 0.45, 0.25])
    leaf = np.where(t[:, None] < 0.1, base + (green - base) * (t[:, None] / 0.1),
                    green + (upper - green) * ((t[:, None] - 0.1) / 0.9))
    dried = np.clip((t - 0.85) / 0.15, 0.0, 1.0)[:, None]
    leaf = leaf * (1.0 - dried) + tip * dried
    for column in range(GREEN_COLUMNS):
        brightness = rng.uniform(0.85, 1.12)
        yellow = rng.uniform(0.0, 0.2)
        shade = (leaf * (1.0 - yellow * t[:, None]) + tip * yellow * t[:, None]) * brightness
        # Средняя жилка (лист осоки сложен вдоль килем): светлее посередине
        keel = 1.0 + 0.08 * np.exp(-((u - 0.5) / 0.12) ** 2)
        put(column, shade[:, None, :] * keel[None, :, None])

    # Сухой лист: солома, к кончику серее; продольные полоски
    straw = np.array([0.62, 0.54, 0.36])
    grey = np.array([0.52, 0.48, 0.40])
    for column in range(DRY_COLUMN, DRY_COLUMN + 2):
        brightness = rng.uniform(0.85, 1.1)
        shade = (straw + (grey - straw) * t[:, None]) * brightness
        streaks = 1.0 + 0.08 * np.cos(u * math.pi * 5.0 + rng.uniform(0.0, math.pi))
        put(column, shade[:, None, :] * streaks[None, :, None])

    # Волокна кочки — старые основания листьев: от верха кочки (строка 0) — светлая солома, к земле (строка 1) — бурые,
    # продольные пряди
    dark = np.array([0.20, 0.15, 0.09])
    fibre = np.array([0.50, 0.42, 0.27])
    strands = rng.uniform(0.0, 1.0, column_width)
    strands = np.convolve(np.concatenate([strands, strands[:2]]), np.ones(3) / 3.0, 'valid')[:column_width]
    mix = np.clip(strands[None, :] * 0.6 + 0.7 * (1.0 - t[:, None]) - 0.2, 0.0, 1.0)
    base = dark[None, None, :] * (1.0 - mix[:, :, None]) + fibre[None, None, :] * mix[:, :, None]
    put(BASE_COLUMN, base * rng.uniform(0.9, 1.1, (height, column_width))[:, :, None])

    # Стебель (строки 0…0,7): зелёный, к верху желтее; колосок (0,72…1): тёмно-бурые чешуи с мелким шумом
    stalk = np.array([0.30, 0.40, 0.16]) + np.array([0.12, 0.08, 0.02]) * t[:, None]
    spike = np.array([0.16, 0.11, 0.07])
    noise = rng.uniform(0.75, 1.25, (height, column_width))
    culm = np.where(t[:, None, None] < 0.71, stalk[:, None, :] * np.ones((1, column_width, 1)), spike[None, None, :] * noise[:, :, None])
    put(CULM_COLUMN, culm)

    rgba = np.concatenate([np.clip(rgb, 0.0, 1.0), np.ones((height, width, 1))], -1).astype(np.float32)
    image = bpy.data.images.new('SedgeColor', width, height)
    image.pixels.foreach_set(rgba.ravel())   # значения в sRGB, как у картинки цвета
    image.filepath_raw = os.path.join(folder, image.name + '.png')
    image.file_format = 'PNG'
    image.save()
    return image


def sedge_material(image):
    mat = bpy.data.materials.new('SedgeMat')
    if bpy.app.version < (5, 0, 0):  # в Blender 5 узлы материала включены всегда
        mat.use_nodes = True
    mat.use_backface_culling = False
    nodes, links = mat.node_tree.nodes, mat.node_tree.links
    bsdf = nodes['Principled BSDF']
    bsdf.inputs['Metallic'].default_value = 0.0
    bsdf.inputs['Roughness'].default_value = 0.55
    tex = nodes.new('ShaderNodeTexImage')
    tex.image = image
    links.new(tex.outputs['Color'], bsdf.inputs['Base Color'])
    # Параметры движка (docs/materials.md, docs/wind.md): extras glTF → экземпляр материала
    mat['WindWeight'] = 1.0
    mat['TrampleBendLength'] = 0.3      # листья раздвигаются у кочки, сама она стоит (Shaders/foliage_interaction.sh)
    mat['DiffuseTransmissionFactor'] = 0.35
    mat['DiffuseTransmissionColorFactor'] = [1.0, 1.0, 0.6]
    mat['DiffuseTransmissionColor'] = 'BaseColor'
    mat['DitheredLODTransition'] = 'true'
    return mat


def column_uv(column):
    width = 1.0 / COLUMNS
    margin = width * 0.15
    return column * width + margin, (column + 1) * width - margin


def mound_radius_at(radius, t):
    """Радиус кочки на доле высоты t: пологий бугор, у земли шире."""
    return radius * (1.0 - 0.45 * t ** 1.6)


def plan_variant(rng, leaves, length_range, mound_radius, mound_height, dry_share, culms):
    """Случайные параметры кочки: одни и те же для всех LOD."""
    top = mound_height
    top_radius = mound_radius_at(mound_radius, 1.0)
    plan = {'base': [], 'leaves': [], 'culms': []}
    # Юбка кочки: короткие широкие волокна от края верха почти горизонтально наружу и дугой вниз до земли
    for i in range(int(300.0 * mound_radius)):
        around = rng.uniform(0.0, 2.0 * math.pi)
        radius = top_radius * rng.uniform(0.5, 0.95)
        plan['base'].append({
            'root': np.array([radius * math.cos(around), radius * math.sin(around), top + 0.005]),
            'heading': around + rng.uniform(-0.3, 0.3),
            'face': around + math.pi * 0.5 + rng.uniform(-0.3, 0.3),   # ширина — вдоль края кочки
            'twist': rng.uniform(-0.3, 0.3),
            'lean': math.radians(rng.uniform(55.0, 80.0)),
            'bend': math.radians(rng.uniform(70.0, 100.0)),
            'bend_start': 0.0,
            'length': (top + 0.04) * rng.uniform(1.5, 2.0) + (mound_radius - radius) * 0.5,
            'width': rng.uniform(0.012, 0.02),
            'column': BASE_COLUMN,
            'order': rng.uniform(),
        })
    for i in range(leaves):
        dry = rng.uniform() < dry_share
        around = rng.uniform(0.0, 2.0 * math.pi)
        if rng.uniform() < 0.6:
            # С верха кочки
            radius = top_radius * 0.9 * math.sqrt(rng.uniform())
            reach = radius / max(top_radius * 0.9, 1e-6)       # 0 — в центре, 1 — у края верха
            z = top - 0.01
        else:
            # С бока старой кочки, у верхней её половины: раскрыты сильнее
            t = rng.uniform(0.55, 0.95)
            radius = mound_radius_at(mound_radius, t) - 0.01
            reach = 1.0 + 0.5 * (1.0 - t)
            z = -0.04 + (top + 0.04) * t
        root = np.array([radius * math.cos(around), radius * math.sin(around), z])
        length = rng.uniform(*length_range) * (1.05 - 0.25 * min(reach, 1.0))
        bend_start = 0.25
        if dry:
            # Прошлогодний лист: с края верха перегибается и свисает по боку кочки к земле
            if reach < 0.7:
                radius = top_radius * rng.uniform(0.7, 0.95)
                root = np.array([radius * math.cos(around), radius * math.sin(around), top - 0.01])
            lean = math.radians(rng.uniform(35.0, 55.0))
            bend = math.radians(rng.uniform(110.0, 150.0))
            bend_start = 0.05
            length *= rng.uniform(0.6, 0.95)
        else:
            # Внутренние стоят, внешние раскрыты, верхняя часть поникает дугой — у края кочки кончики к земле
            lean = math.radians(rng.uniform(2.0, 10.0) + 40.0 * reach)
            bend = math.radians(rng.uniform(50.0, 110.0) * (0.6 + 0.6 * reach))
        plan['leaves'].append({
            'root': root,
            'heading': around + rng.uniform(-0.5, 0.5) if radius > 0.02 else rng.uniform(0.0, 2.0 * math.pi),
            'face': rng.uniform(0.0, 2.0 * math.pi),
            'twist': rng.uniform(-0.6, 0.6),
            'lean': lean,
            'bend': bend,
            'bend_start': bend_start,
            'length': length,
            'width': rng.uniform(0.006, 0.009),
            'column': DRY_COLUMN + int(rng.integers(0, 2)) if dry else int(rng.integers(0, GREEN_COLUMNS)),
            'order': rng.uniform(),
        })
    for c in range(culms):
        around = rng.uniform(0.0, 2.0 * math.pi)
        radius = top_radius * 0.5 * math.sqrt(rng.uniform())
        plan['culms'].append({
            'root': np.array([radius * math.cos(around), radius * math.sin(around), top - 0.01]),
            'height': length_range[1] * rng.uniform(0.95, 1.2),
            'lean': math.radians(rng.uniform(4.0, 14.0)),
            'heading': around + rng.uniform(-0.4, 0.4),
            'radius': rng.uniform(0.0016, 0.0024),
            'spikes': int(rng.integers(2, 4)),
        })
    return plan


def add_leaf(leaf, segments, width_scale, vertices, faces, uvs):
    u0, u1 = column_uv(leaf['column'])
    point = np.array(leaf['root'], dtype=float)
    step = leaf['length'] / segments
    heading = leaf['heading']
    first = len(vertices)
    for i in range(segments + 1):
        t = i / segments
        if i > 0:
            middle = t - 0.5 / segments
            # Дуга: прямой у основания, с bend_start длины поникает всё сильнее; у земли — вдоль неё, не в землю
            start = leaf['bend_start']
            angle = leaf['lean'] + leaf['bend'] * max(middle - start, 0.0) ** 1.3 / (1.0 - start) ** 1.3
            if point[2] < 0.04:
                angle = min(angle, math.radians(86.0))
            direction = np.array([math.sin(angle) * math.cos(heading), math.sin(angle) * math.sin(heading), math.cos(angle)])
            point = point + direction * step
        face = leaf['face'] + leaf['twist'] * t
        side = np.array([math.cos(face), math.sin(face), 0.0])
        if i == segments:
            vertices.append(tuple(point))                 # острый кончик
            uvs.append(((u0 + u1) * 0.5, 1.0))
        else:
            # Лист осоки длинно сужается: полная ширина до 60 % длины, дальше — к острию
            profile = min(1.0, (1.0 - t) / 0.4)
            half = leaf['width'] * width_scale * 0.5 * profile
            vertices.append(tuple(point - side * half))
            vertices.append(tuple(point + side * half))
            uvs.append((u0, t))
            uvs.append((u1, t))
    for i in range(segments - 1):
        a = first + 2 * i
        faces.append((a, a + 1, a + 3, a + 2))
    last = first + 2 * (segments - 1)
    faces.append((last, last + 1, first + 2 * segments))


def add_tube(start, axis, length, radius, sides, column, v0, v1, caps, vertices, faces, uvs, taper=0.0):
    """Цилиндр в одно кольцо вдоль axis от start: радиус к концу × (1 − taper); caps — острия на концах (колосок)."""
    axis = axis / np.linalg.norm(axis)
    helper = np.array([1.0, 0.0, 0.0]) if abs(axis[0]) < 0.9 else np.array([0.0, 1.0, 0.0])
    x = np.cross(axis, helper)
    x /= np.linalg.norm(x)
    y = np.cross(axis, x)
    u0, u1 = column_uv(column)
    first = len(vertices)
    for r in range(2):
        center = start + axis * length * r
        ring_radius = radius * (1.0 - taper * r)
        for s in range(sides + 1):                       # шов: последняя вершина кольца — копия первой с u1
            a = 2.0 * math.pi * s / sides
            vertices.append(tuple(center + (x * math.cos(a) + y * math.sin(a)) * ring_radius))
            uvs.append((u0 + (u1 - u0) * s / sides, v0 + (v1 - v0) * r))
    row = sides + 1
    for s in range(sides):
        a = first + s
        faces.append((a, a + 1, a + row + 1, a + row))
    if caps:
        for end, sign in ((0, -1.0), (1, 1.0)):
            tip = len(vertices)
            vertices.append(tuple(start + axis * (length * end + sign * radius * 1.5)))
            uvs.append(((u0 + u1) * 0.5, v0 if end == 0 else v1))
            for s in range(sides):
                a = first + end * row + s
                faces.append((a + 1, a, tip) if end == 0 else (a, a + 1, tip))


def add_culm(culm, spike_sides, vertices, faces, uvs):
    lean, heading = culm['lean'], culm['heading']
    axis = np.array([math.sin(lean) * math.cos(heading), math.sin(lean) * math.sin(heading), math.cos(lean)])
    root = np.array(culm['root'], dtype=float)
    height = culm['height']
    add_tube(root, axis, height, culm['radius'], 3, CULM_COLUMN, 0.0, 0.7, False, vertices, faces, uvs, 0.4)
    # Колоски у верхушки: верхний — тонкий мужской, ниже — толще женские, чуть в сторону от стебля
    side = np.cross(axis, np.array([0.0, 0.0, 1.0]))
    side = side / np.linalg.norm(side) if np.linalg.norm(side) > 1e-6 else np.array([1.0, 0.0, 0.0])
    for k in range(culm['spikes']):
        male = k == 0
        length = 0.035 if male else 0.028
        spike_radius = 0.0022 if male else 0.0035
        offset = side * (0.0 if male else (0.004 if k % 2 else -0.004))
        start = root + axis * (height - 0.002 - length - 0.045 * k) + offset
        add_tube(start, axis, length, spike_radius, spike_sides, CULM_COLUMN, 0.72, 0.98, True, vertices, faces, uvs, 0.2)


def build_mesh(name, plan, lod):
    share, segments, width_scale, spike_sides = LODS[lod]
    vertices, faces, uvs = [], [], []
    base = sorted(plan['base'], key=lambda strand: strand['order'])
    for strand in base[:int(round(len(base) * share))]:
        add_leaf(strand, min(segments, 3), width_scale, vertices, faces, uvs)
    leaves = sorted(plan['leaves'], key=lambda leaf: leaf['order'])
    for leaf in leaves[:max(1, int(round(len(leaves) * share)))]:
        add_leaf(leaf, segments, width_scale, vertices, faces, uvs)
    for culm in plan['culms']:
        add_culm(culm, spike_sides, vertices, faces, uvs)
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(vertices, [], faces)
    uv_layer = mesh.uv_layers.new(name='UVMap')
    for polygon in mesh.polygons:
        polygon.use_smooth = True
        for loop_index in polygon.loop_indices:
            uv_layer.data[loop_index].uv = uvs[mesh.loops[loop_index].vertex_index]
    mesh.update()
    return mesh


def main():
    out = output_path()
    folder = os.path.splitext(out)[0] + '_images'
    os.makedirs(folder, exist_ok=True)

    clear_scene()
    material = sedge_material(sedge_color(folder))
    rng = np.random.default_rng(SEED)
    triangles = []
    # Варианты и их LOD стоят в стороне друг от друга, как обычно в Blender: импортёр берёт LOD относительно своего origin
    for index, (name, leaves, length_range, mound_radius, mound_height, dry_share, culms) in enumerate(VARIANTS):
        plan = plan_variant(rng, leaves, length_range, mound_radius, mound_height, dry_share, culms)
        counts = []
        for lod in range(len(LODS)):
            object_name = name + ('_LOD%d' % lod if lod else '')
            mesh = build_mesh(object_name, plan, lod)
            obj = bpy.data.objects.new(object_name, mesh)
            obj.data.materials.append(material)
            obj.location = (3.0 * index, 3.0 * lod, 0.0)
            bpy.context.collection.objects.link(obj)
            counts.append(sum(len(p.vertices) - 2 for p in mesh.polygons))
        triangles.append('%s %s' % (name, '/'.join(str(c) for c in counts)))

    bpy.ops.export_scene.gltf(filepath=out, export_format='GLB', export_yup=True, export_apply=True,
                              export_tangents=True, export_image_format='AUTO', export_materials='EXPORT',
                              export_extras=True)
    print('Triangles (LOD0/LOD1/LOD2): ' + ', '.join(triangles))
    print('Written: ' + out)


main()
