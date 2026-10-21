# Рогоз широколистный (Typha latifolia) для слоя расстановки на мелководье озёр (набор Meadow, вода «Shallows»,
# docs/scatter.md, «Рогоз»). Запускается внутри Blender без окна:
#   blender -b --factory-startup --python Tools/blender_cattail.py -- <выход.glb>
# Затем импорт моделями для расстановки:
#   python Tools/import_gltf.py <выход.glb> --scatter --lod-ranges 8,20 --shadow-min-lod 1
# Сцена — четыре куртины (варианты слоя), у каждой LOD0…LOD2:
#   Cattail_A — 3 побега высотой 1,3–1,6 м, 2 початка;   Cattail_B — 5 побегов 1,6–2,0 м, 3 початка;
#   Cattail_C — молодая: 3 побега 0,9–1,2 м без початков; Cattail_D — 4 побега 1,5–1,9 м, 1 початок.
# Побег — веер из 6–9 листьев геометрией (как травинки GrassClump): длинные плоские ленты 1–2 см шириной, прямые внизу и
# поникшие в верхней трети, плоскость листа к кончику поворачивается (лист рогоза винтом), ширина почти постоянна и сходит
# на нет в последних 15 %. Цветонос — тонкий стебель, на нём женский початок (бархатный коричневый цилиндр 15–22 см) и выше
# мужской — тонкий светлый, над ним — голый кончик.
# Один материал CattailMat двусторонний, атлас CattailColor: столбцы 0–5 — оттенки листа по высоте (бледное влагалище у
# корня, зелень, желтоватый кончик), 6 — стебель, 7 — початок (внизу столбца женский, вверху мужской). Параметры движка,
# которых нет в glTF (ветер, просвет, смена LOD дизерингом), — Custom Properties материала: extras glTF.
# Origin — у корней (ось Z Blender вверх), листья начинаются чуть ниже нуля: на неровном дне не висят.
# Сообщения скрипта — ASCII (правило Tools/).
import math
import os
import sys

import bpy
import numpy as np

COLUMNS = 8          # столбцов атласа: 6 оттенков листа, стебель, початок
LEAF_COLUMNS = 6
STALK_COLUMN = 6
SPIKE_COLUMN = 7
SEED = 11
# Варианты: имя, побегов, высота побега (м), листьев на побег, початков
VARIANTS = [
    ('Cattail_A', 3, (1.3, 1.6), (6, 8), 2),
    ('Cattail_B', 5, (1.6, 2.0), (7, 9), 3),
    ('Cattail_C', 3, (0.9, 1.2), (6, 7), 0),
    ('Cattail_D', 4, (1.5, 1.9), (6, 8), 1),
]
# LOD: доля листьев, сегментов листа, множитель ширины листа, граней стебля, граней початка
LODS = [(1.0, 7, 1.0, 6, 8), (0.7, 4, 1.35, 4, 6), (0.45, 2, 1.9, 3, 4)]


def output_path():
    argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    if not argv:
        raise SystemExit('usage: blender -b --factory-startup --python Tools/blender_cattail.py -- <out.glb>')
    return os.path.abspath(argv[0])


def clear_scene():
    for obj in list(bpy.data.objects):
        bpy.data.objects.remove(obj, do_unlink=True)


def cattail_color(folder, width=128, height=256):
    """Атлас: столбцы — оттенки листа, стебель и початок; строки — высота (пиксели Blender идут снизу вверх)."""
    rng = np.random.default_rng(SEED)
    t = (np.arange(height) + 0.5) / height
    column_width = width // COLUMNS
    rgb = np.zeros((height, width, 3))

    # Лист: бледное влагалище у корня → зелень → желтоватый, к самому кончику подсохший
    sheath = np.array([0.52, 0.56, 0.36])
    green = np.array([0.22, 0.38, 0.10])
    upper = np.array([0.36, 0.46, 0.15])
    dry = np.array([0.50, 0.42, 0.22])
    leaf = np.where(t[:, None] < 0.12, sheath + (green - sheath) * (t[:, None] / 0.12),
                    green + (upper - green) * ((t[:, None] - 0.12) / 0.88))
    tip = np.clip((t - 0.92) / 0.08, 0.0, 1.0)[:, None]
    leaf = leaf * (1.0 - tip) + dry * tip
    u = (np.arange(column_width) + 0.5) / column_width
    for column in range(LEAF_COLUMNS):
        brightness = rng.uniform(0.85, 1.12)
        yellow = rng.uniform(0.0, 0.25)
        shade = (leaf * (1.0 - yellow * t[:, None]) + dry * yellow * t[:, None]) * brightness
        # Продольные жилки: лист рогоза — в тонкую полоску
        veins = 1.0 + 0.06 * np.cos(u * math.pi * 6.0)
        rgb[:, column * column_width:(column + 1) * column_width] = shade[:, None, :] * veins[None, :, None]

    # Стебель: зелёный, к верху светлее
    stalk = np.array([0.26, 0.40, 0.13]) + np.array([0.10, 0.08, 0.04]) * t[:, None]
    rgb[:, STALK_COLUMN * column_width:(STALK_COLUMN + 1) * column_width] = stalk[:, None, :]

    # Початок: строки 0…0,5 — женский (тёмно-коричневый бархат с мелким шумом), 0,55…1 — мужской (светлый)
    female = np.array([0.20, 0.11, 0.05])
    male = np.array([0.55, 0.45, 0.28])
    noise = rng.uniform(0.85, 1.15, (height, column_width))
    spike = np.where(t[:, None] < 0.52, female, male)
    rgb[:, SPIKE_COLUMN * column_width:(SPIKE_COLUMN + 1) * column_width] = spike[:, None, :] * noise[:, :, None]

    rgba = np.concatenate([np.clip(rgb, 0.0, 1.0), np.ones((height, width, 1))], -1).astype(np.float32)
    image = bpy.data.images.new('CattailColor', width, height)
    image.pixels.foreach_set(rgba.ravel())   # значения в sRGB, как у картинки цвета
    image.filepath_raw = os.path.join(folder, image.name + '.png')
    image.file_format = 'PNG'
    image.save()
    return image


def cattail_material(image):
    mat = bpy.data.materials.new('CattailMat')
    if bpy.app.version < (5, 0, 0):  # в Blender 5 узлы материала включены всегда
        mat.use_nodes = True
    mat.use_backface_culling = False
    nodes, links = mat.node_tree.nodes, mat.node_tree.links
    bsdf = nodes['Principled BSDF']
    bsdf.inputs['Metallic'].default_value = 0.0
    bsdf.inputs['Roughness'].default_value = 0.5
    tex = nodes.new('ShaderNodeTexImage')
    tex.image = image
    links.new(tex.outputs['Color'], bsdf.inputs['Base Color'])
    # Параметры движка (docs/materials.md, docs/wind.md): extras glTF → экземпляр материала
    mat['WindWeight'] = 1.0
    mat['TrampleBendLength'] = 0.4      # раздвигается у корня, выше стоит (Shaders/foliage_interaction.sh)
    mat['DiffuseTransmissionFactor'] = 0.35
    mat['DiffuseTransmissionColorFactor'] = [1.0, 1.0, 0.6]
    mat['DiffuseTransmissionColor'] = 'BaseColor'
    mat['DitheredLODTransition'] = 'true'
    return mat


def column_uv(column):
    width = 1.0 / COLUMNS
    margin = width * 0.15
    return column * width + margin, (column + 1) * width - margin


def plan_variant(rng, shoots, height_range, leaf_range, spikes):
    """Случайные параметры куртины: одни и те же для всех LOD."""
    plan = {'leaves': [], 'stalks': []}
    for shoot in range(shoots):
        radius = 0.16 * math.sqrt(rng.uniform())
        around = rng.uniform(0.0, 2.0 * math.pi)
        base = np.array([radius * math.cos(around), radius * math.sin(around), -0.03])
        height = rng.uniform(*height_range)
        facing = rng.uniform(0.0, 2.0 * math.pi)      # плоскость веера листьев
        count = int(rng.integers(leaf_range[0], leaf_range[1] + 1))
        for i in range(count):
            # Листья попеременно в две стороны веера, внешние — длиннее и раскрыты сильнее
            side = 1.0 if i % 2 else -1.0
            order = i / max(count - 1, 1)
            plan['leaves'].append({
                'root': base + np.array([rng.uniform(-0.01, 0.01), rng.uniform(-0.01, 0.01), 0.0]),
                'heading': facing + (0.0 if side > 0 else math.pi) + rng.uniform(-0.35, 0.35),
                'face': facing + math.pi * 0.5,           # ширина листа — поперёк плоскости веера
                'twist': rng.uniform(-1.3, 1.3),          # поворот плоскости листа к кончику, рад
                'lean': math.radians(rng.uniform(1.0, 5.0) + 7.0 * order),
                'bend': math.radians(rng.uniform(25.0, 70.0)),   # поникание верхней трети
                'length': height * rng.uniform(0.75, 1.05) * (0.8 + 0.2 * order),
                'width': rng.uniform(0.012, 0.02),
                'column': int(rng.integers(0, LEAF_COLUMNS)),
                'order': rng.uniform(),                   # порядок отбора в дальних LOD
            })
        if shoot < spikes:
            plan['stalks'].append({
                'root': base,
                'height': height * rng.uniform(0.8, 0.95),
                'lean': math.radians(rng.uniform(0.0, 4.0)),
                'heading': rng.uniform(0.0, 2.0 * math.pi),
                'radius': rng.uniform(0.004, 0.006),
                'spike': rng.uniform(0.15, 0.22),
                'spike_radius': rng.uniform(0.012, 0.016),
                'male': rng.uniform(0.07, 0.14),
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
            # Прямой внизу, поникает в верхней трети: изгиб растёт с высотой быстро
            angle = leaf['lean'] + leaf['bend'] * max(middle - 0.45, 0.0) ** 1.5 / 0.55 ** 1.5
            direction = np.array([math.sin(angle) * math.cos(heading), math.sin(angle) * math.sin(heading), math.cos(angle)])
            point = point + direction * step
        face = leaf['face'] + leaf['twist'] * t * t
        side = np.array([math.cos(face), math.sin(face), 0.0])
        if i == segments:
            vertices.append(tuple(point))                 # кончик
            uvs.append(((u0 + u1) * 0.5, 1.0))
        else:
            # Ширина почти постоянна (у основания влагалище чуть уже), к кончику — сходит на нет в последних 15 %
            profile = min(1.0, (1.0 - t) / 0.15) * (0.85 + 0.15 * min(t / 0.1, 1.0))
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


def add_tube(start, axis, length, radius, sides, rings, column, v0, v1, caps, vertices, faces, uvs, taper=0.0):
    """Цилиндр вдоль axis от start: rings колец, радиус к концу × (1 − taper); caps — купола на концах (початок)."""
    axis = axis / np.linalg.norm(axis)
    helper = np.array([1.0, 0.0, 0.0]) if abs(axis[0]) < 0.9 else np.array([0.0, 1.0, 0.0])
    x = np.cross(axis, helper)
    x /= np.linalg.norm(x)
    y = np.cross(axis, x)
    u0, u1 = column_uv(column)
    first = len(vertices)
    for r in range(rings + 1):
        t = r / rings
        center = start + axis * length * t
        ring_radius = radius * (1.0 - taper * t)
        for s in range(sides + 1):                       # шов: последняя вершина кольца — копия первой с u1
            a = 2.0 * math.pi * s / sides
            vertices.append(tuple(center + (x * math.cos(a) + y * math.sin(a)) * ring_radius))
            uvs.append((u0 + (u1 - u0) * s / sides, v0 + (v1 - v0) * t))
    row = sides + 1
    for r in range(rings):
        for s in range(sides):
            a = first + r * row + s
            faces.append((a, a + 1, a + row + 1, a + row))
    if caps:
        for end, sign in ((0, -1.0), (rings, 1.0)):
            tip = len(vertices)
            vertices.append(tuple(start + axis * (length * end / rings + sign * radius * 0.6)))
            uvs.append(((u0 + u1) * 0.5, v0 if end == 0 else v1))
            for s in range(sides):
                a = first + end * row + s
                faces.append((a + 1, a, tip) if end == 0 else (a, a + 1, tip))


def add_stalk(stalk, sides, spike_sides, vertices, faces, uvs):
    lean, heading = stalk['lean'], stalk['heading']
    axis = np.array([math.sin(lean) * math.cos(heading), math.sin(lean) * math.sin(heading), math.cos(lean)])
    root = np.array(stalk['root'], dtype=float)
    height = stalk['height']
    spike_top = height - 0.03 - stalk['male']
    spike_bottom = spike_top - stalk['spike']
    rings = 3 if sides > 3 else 1
    # Стебель до початка, сквозь початки и голый кончик над мужским
    add_tube(root, axis, spike_bottom, stalk['radius'], sides, rings, STALK_COLUMN, 0.0, 0.9, False, vertices, faces, uvs, 0.2)
    add_tube(root + axis * spike_bottom, axis, stalk['spike'], stalk['spike_radius'], spike_sides, 2 if spike_sides > 4 else 1,
             SPIKE_COLUMN, 0.02, 0.5, True, vertices, faces, uvs)
    add_tube(root + axis * spike_top, axis, stalk['male'], stalk['radius'] * 1.2, max(sides - 1, 3), 1, SPIKE_COLUMN, 0.56, 0.98,
             False, vertices, faces, uvs, 0.4)
    add_tube(root + axis * (spike_top + stalk['male']), axis, 0.03, stalk['radius'] * 0.6, 3, 1, STALK_COLUMN, 0.9, 1.0, False,
             vertices, faces, uvs, 0.8)


def build_mesh(name, plan, lod):
    share, segments, width_scale, sides, spike_sides = LODS[lod]
    vertices, faces, uvs = [], [], []
    leaves = sorted(plan['leaves'], key=lambda leaf: leaf['order'])
    for leaf in leaves[:max(1, int(round(len(leaves) * share)))]:
        add_leaf(leaf, segments, width_scale, vertices, faces, uvs)
    for stalk in plan['stalks']:
        add_stalk(stalk, sides, spike_sides, vertices, faces, uvs)
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
    material = cattail_material(cattail_color(folder))
    rng = np.random.default_rng(SEED)
    triangles = []
    # Варианты и их LOD стоят в стороне друг от друга, как обычно в Blender: импортёр берёт LOD относительно своего origin
    for index, (name, shoots, height_range, leaf_range, spikes) in enumerate(VARIANTS):
        plan = plan_variant(rng, shoots, height_range, leaf_range, spikes)
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
