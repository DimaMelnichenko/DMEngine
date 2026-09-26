# Ромашка для слоя расстановки (набор Meadow, маска mask_camomile). Запускается внутри Blender без окна:
#   blender -b --factory-startup --python Tools/blender_camomile.py -- <выход.glb>
# Затем импорт моделью для расстановки:
#   python Tools/import_gltf.py <выход.glb> --scatter
# Сцена — объект Camomile, кустик высотой 0,38–0,5 м (как луговая ромашка — нивяник; цветки выше травы):
#   три стебля (трёхгранные трубки) с цветками: лепестки — «зонтик» из 12 треугольников с альфа-текстурой кольца
#     из 16 белых лепестков (слегка опущены к краю), серединка — низкий жёлтый купол, чтобы цветок был виден и сбоку;
#   шесть перистых листьев розеткой у земли и по листу на двух стеблях — изогнутые карточки с альфа-текстурой.
# Один материал Camomile на всё — атлас 512×512 (слою расстановки нужна одна модель, а объект с несколькими
# материалами импортёр делит на несколько моделей): вверху кольцо лепестков и лист, внизу зелень стебля
# и серединка. Альфа идёт в материал через Math → Round — экспортёр glTF пишет alphaMode MASK с порогом 0,5;
# материал двусторонний (Backface Culling выключен). У прозрачных пикселей цвет соседних (лепестка, листа), чтобы
# на мипах по краю не было тёмной каймы.
# Origin — у земли (ось Z Blender вверх). Сообщения скрипта — ASCII (правило Tools/).
import math
import os
import sys

import bpy
import numpy as np

SEED = 11
ATLAS = 512
REGION = ATLAS // 2
# Области атласа в UV Blender (v вверх): (u0, v0) левого нижнего угла, сторона — 0,5
PETALS = (0.0, 0.5)
LEAF = (0.5, 0.5)
STEM = (0.0, 0.0)
DISC = (0.5, 0.0)
HEAD_RADIUS = 0.025      # м: радиус кольца лепестков — край текстуры
LEAF_WIDTH = 0.04        # м: ширина карточки листа — ширина области атласа


def output_path():
    argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    if not argv:
        raise SystemExit('usage: blender -b --factory-startup --python Tools/blender_camomile.py -- <out.glb>')
    return os.path.abspath(argv[0])


def clear_scene():
    for obj in list(bpy.data.objects):
        bpy.data.objects.remove(obj, do_unlink=True)


# ---------------------------------------------------------------------------------------------------------------------
# Текстура

def region_grid():
    """Координаты пикселей области −1…1; y растёт вверх, как строки пикселей Blender."""
    c = (np.arange(REGION) + 0.5) / REGION * 2.0 - 1.0
    return np.meshgrid(c, c)


def edge(distance, width):
    """Сглаженный край: 1 внутри (distance > 0), 0 снаружи, переход шириной width."""
    return np.clip(distance / width + 0.5, 0.0, 1.0)


def petals_region(rng):
    x, y = region_grid()
    r = np.hypot(x, y)
    theta = np.arctan2(y, x)
    texel = 2.0 / REGION
    alpha = np.zeros_like(r)
    shade = np.ones_like(r)
    count = 16
    for k in range(count):
        center = 2.0 * math.pi * k / count + rng.uniform(-0.08, 0.08)
        r0, r1 = 0.2, rng.uniform(0.84, 0.92)
        half_width = rng.uniform(0.075, 0.09)
        delta = np.angle(np.exp(1j * (theta - center)))
        along = r * np.cos(delta)
        side = r * np.sin(delta)
        s = np.clip((along - r0) / (r1 - r0), 0.0, 1.0)
        # Узкий у основания, шире к концу, скруглённый кончик
        width = half_width * np.sqrt(np.sin(math.pi * s ** 0.7)) + 1e-4
        inside = (edge(width - np.abs(side), 1.5 * texel) * edge(along - r0, texel) * edge(r1 - along, 1.5 * texel) *
                  (np.cos(delta) > 0.0))
        # Две-три продольные жилки и чуть желтоватое основание
        vein = 1.0 - 0.05 * np.abs(np.cos(side / width * math.pi * 1.5))
        petal_shade = vein * (0.88 + 0.12 * np.clip(s * 3.0, 0.0, 1.0))
        shade = np.where(inside > alpha, petal_shade, shade)
        alpha = np.maximum(alpha, inside)
    white = np.array([0.97, 0.97, 0.94])
    base = np.array([0.90, 0.92, 0.72])
    t = np.clip((r - 0.2) / 0.3, 0.0, 1.0)[..., None]
    rgb = (base + (white - base) * t) * shade[..., None]

    # Серединка: жёлтая, темнее к краю
    disc = edge(0.22 - r, 1.5 * texel)
    yellow = np.array([0.98, 0.78, 0.12]) * (1.0 - 0.25 * np.clip(r / 0.22, 0.0, 1.0) ** 4)[..., None]
    rgb = rgb * (1.0 - disc[..., None]) + yellow * disc[..., None]
    alpha = np.maximum(alpha, disc)
    return rgb, alpha


def segment_distance(points, a, b):
    """Расстояния от точек (N, 2) до отрезков a→b (M, 2) — матрица (N, M)."""
    ab = b - a
    t = np.clip(((points[:, None, :] - a[None]) * ab[None]).sum(-1) / np.maximum((ab * ab).sum(-1), 1e-12)[None],
                0.0, 1.0)
    nearest = a[None] + t[..., None] * ab[None]
    return np.linalg.norm(points[:, None, :] - nearest, axis=-1)


def leaf_region(rng, length=0.11):
    """Перистый лист в метрах: x — поперёк (ширина LEAF_WIDTH), y — вдоль (длина length) от черешка к кончику."""
    gx, gy = region_grid()
    points = np.stack([gx.ravel() * LEAF_WIDTH * 0.5, (gy.ravel() + 1.0) * 0.5 * length], -1)
    starts, ends, widths = [], [], []

    def line(a, b, width):
        starts.append(a), ends.append(b), widths.append(width)

    line((0.0, 0.0), (0.0, length * 0.97), 0.0018)          # черешок и главная жилка
    count = 14
    for i in range(count):
        v = 0.10 + 0.80 * i / (count - 1)
        side = 1.0 if i % 2 == 0 else -1.0
        # Доли длиннее посередине листа, короче у черешка и кончика
        size = LEAF_WIDTH * 0.46 * math.sin(math.pi * min(v * 1.08, 1.0)) ** 0.8 * rng.uniform(0.85, 1.0)
        angle = math.radians(rng.uniform(45.0, 60.0))
        a = np.array([0.0, v * length])
        direction = np.array([side * math.sin(angle), math.cos(angle)])
        b = a + direction * size
        line(a, b, 0.0017)
        # Мелкие дольки по бокам доли
        for j, f in enumerate((0.25, 0.45, 0.65, 0.85)):
            twig_side = 1.0 if j % 2 == 0 else -1.0
            turn = math.radians(45.0) * twig_side
            rotated = np.array([direction[0] * math.cos(turn) - direction[1] * math.sin(turn),
                                direction[0] * math.sin(turn) + direction[1] * math.cos(turn)])
            p = a + direction * size * f
            line(p, p + rotated * size * 0.32, 0.0013)
    distance = segment_distance(points, np.array(starts, float), np.array(ends, float))
    coverage = (np.array(widths)[None] * 0.5 - distance).max(axis=1)
    texel = length / REGION
    alpha = edge(coverage, 1.5 * texel).reshape(REGION, REGION)
    v = ((gy + 1.0) * 0.5)[..., None]
    rgb = np.array([0.17, 0.34, 0.08]) + (np.array([0.26, 0.45, 0.12]) - np.array([0.17, 0.34, 0.08])) * v
    return np.broadcast_to(rgb, (REGION, REGION, 3)).copy(), alpha


def stem_region():
    _, y = region_grid()
    v = ((y + 1.0) * 0.5)[..., None]
    rgb = np.array([0.20, 0.36, 0.09]) + (np.array([0.27, 0.45, 0.13]) - np.array([0.20, 0.36, 0.09])) * v
    return np.broadcast_to(rgb, (REGION, REGION, 3)).copy(), np.ones((REGION, REGION))


def disc_region(rng):
    rgb = np.broadcast_to(np.array([0.98, 0.76, 0.10]), (REGION, REGION, 3)).copy()
    dots = rng.random((REGION, REGION)) > 0.9
    rgb[dots] = np.array([0.85, 0.50, 0.06])
    return rgb, np.ones((REGION, REGION))


def camomile_atlas(folder):
    rng = np.random.default_rng(SEED)
    rgba = np.zeros((ATLAS, ATLAS, 4), np.float32)
    for (u0, v0), (rgb, alpha) in ((PETALS, petals_region(rng)), (LEAF, leaf_region(rng)),
                                   (STEM, stem_region()), (DISC, disc_region(rng))):
        x0, y0 = int(u0 * ATLAS), int(v0 * ATLAS)
        rgba[y0:y0 + REGION, x0:x0 + REGION, :3] = np.clip(rgb, 0.0, 1.0)
        rgba[y0:y0 + REGION, x0:x0 + REGION, 3] = alpha

    image = bpy.data.images.new('CamomileAtlas', ATLAS, ATLAS, alpha=True)
    image.pixels.foreach_set(rgba.ravel())   # значения в sRGB, как у картинки цвета
    image.filepath_raw = os.path.join(folder, image.name + '.png')
    image.file_format = 'PNG'
    image.save()
    return image


def camomile_material(image):
    mat = bpy.data.materials.new('Camomile')
    if bpy.app.version < (5, 0, 0):  # в Blender 5 узлы материала включены всегда
        mat.use_nodes = True
    mat.use_backface_culling = False
    nodes, links = mat.node_tree.nodes, mat.node_tree.links
    bsdf = nodes['Principled BSDF']
    bsdf.inputs['Metallic'].default_value = 0.0
    bsdf.inputs['Roughness'].default_value = 0.6
    tex = nodes.new('ShaderNodeTexImage')
    tex.image = image
    links.new(tex.outputs['Color'], bsdf.inputs['Base Color'])
    # Альфа через Round — экспортёр glTF распознаёт отсечение: alphaMode MASK, alphaCutoff 0,5
    clip = nodes.new('ShaderNodeMath')
    clip.operation = 'ROUND'
    links.new(tex.outputs['Alpha'], clip.inputs[0])
    links.new(clip.outputs[0], bsdf.inputs['Alpha'])
    return mat


# ---------------------------------------------------------------------------------------------------------------------
# Геометрия

class MeshBuilder:
    def __init__(self):
        self.vertices, self.faces, self.uvs = [], [], []

    def vertex(self, position, uv):
        self.vertices.append(tuple(float(c) for c in position))
        self.uvs.append(uv)
        return len(self.vertices) - 1

    def build(self, name):
        mesh = bpy.data.meshes.new(name)
        mesh.from_pydata(self.vertices, [], self.faces)
        uv_layer = mesh.uv_layers.new(name='UVMap')
        for polygon in mesh.polygons:
            polygon.use_smooth = True
            for loop_index in polygon.loop_indices:
                uv_layer.data[loop_index].uv = self.uvs[mesh.loops[loop_index].vertex_index]
        mesh.update()
        return mesh


def region_uv(region, u, v, margin=0.01):
    """UV точки (u, v) ∈ [0, 1]² области атласа."""
    u0, v0 = region
    return (u0 + margin + (0.5 - 2 * margin) * u, v0 + margin + (0.5 - 2 * margin) * v)


def rotation_to(direction):
    """Матрица поворота, переводящая +Z в direction (формула Родрига)."""
    z = np.array([0.0, 0.0, 1.0])
    d = direction / np.linalg.norm(direction)
    axis = np.cross(z, d)
    s, c = np.linalg.norm(axis), float(np.dot(z, d))
    if s < 1e-8:
        return np.eye(3) if c > 0 else np.diag([1.0, -1.0, -1.0])
    k = axis / s
    kx = np.array([[0.0, -k[2], k[1]], [k[2], 0.0, -k[0]], [-k[1], k[0], 0.0]])
    return np.eye(3) + s * kx + (1.0 - c) * kx @ kx


def direction(heading, elevation):
    """Направление по азимуту и углу над горизонтом."""
    return np.array([math.cos(elevation) * math.cos(heading), math.cos(elevation) * math.sin(heading),
                     math.sin(elevation)])


def add_stem(builder, spine, radius_base, radius_top):
    """Трёхгранная трубка вдоль точек spine."""
    rings = []
    for i, point in enumerate(spine):
        t = i / (len(spine) - 1)
        forward = spine[min(i + 1, len(spine) - 1)] - spine[max(i - 1, 0)]
        frame = rotation_to(forward)
        radius = radius_base + (radius_top - radius_base) * t
        ring = []
        for k in range(3):
            a = 2.0 * math.pi * k / 3
            ring.append(builder.vertex(point + frame @ np.array([math.cos(a), math.sin(a), 0.0]) * radius,
                                       region_uv(STEM, k / 3, t)))
        rings.append(ring)
    for lower, upper in zip(rings, rings[1:]):
        for k in range(3):
            builder.faces.append((lower[k], lower[(k + 1) % 3], upper[(k + 1) % 3], upper[k]))


def add_head(builder, center, normal, spin):
    """Цветок: «зонтик» лепестков с альфа-текстурой и жёлтый купол серединки."""
    frame = rotation_to(normal) @ np.array([[math.cos(spin), -math.sin(spin), 0.0],
                                            [math.sin(spin), math.cos(spin), 0.0], [0.0, 0.0, 1.0]])
    at = lambda local: center + frame @ np.array(local)
    rim = 12
    middle = builder.vertex(at([0.0, 0.0, 0.004]), region_uv(PETALS, 0.5, 0.5))
    ring = [builder.vertex(at([HEAD_RADIUS * math.cos(a), HEAD_RADIUS * math.sin(a), -0.004]),
                           region_uv(PETALS, 0.5 + 0.5 * math.cos(a), 0.5 + 0.5 * math.sin(a)))
            for a in (2.0 * math.pi * j / rim for j in range(rim))]
    for j in range(rim):
        builder.faces.append((middle, ring[j], ring[(j + 1) % rim]))

    # Низкий скруглённый купол: два кольца и вершина
    sides = 8
    apex = builder.vertex(at([0.0, 0.0, 0.0068]), region_uv(DISC, 0.5, 0.5))
    rings = []
    for radius, height, uv_radius in ((0.0056, 0.0030, 0.45), (0.0038, 0.0060, 0.25)):
        rings.append([builder.vertex(at([radius * math.cos(a), radius * math.sin(a), height]),
                                     region_uv(DISC, 0.5 + uv_radius * math.cos(a), 0.5 + uv_radius * math.sin(a)))
                      for a in (2.0 * math.pi * j / sides for j in range(sides))])
    lower, upper = rings
    for j in range(sides):
        k = (j + 1) % sides
        builder.faces.append((lower[j], lower[k], upper[k], upper[j]))
        builder.faces.append((apex, upper[j], upper[k]))


def add_leaf(builder, root, heading, elevation_start, elevation_end, length, width, roll):
    """Изогнутая карточка листа из трёх сегментов: поднимается от root и опускается к кончику."""
    side = np.array([-math.sin(heading), math.cos(heading), 0.0])
    side = side * math.cos(roll) + np.array([0.0, 0.0, 1.0]) * math.sin(roll)
    segments = 3
    point = np.array(root, float)
    rows = []
    for i in range(segments + 1):
        t = i / segments
        if i > 0:
            elevation = elevation_start + (elevation_end - elevation_start) * (t - 0.5 / segments)
            point = point + direction(heading, elevation) * length / segments
        rows.append((builder.vertex(point - side * width * 0.5, region_uv(LEAF, 0.0, t)),
                     builder.vertex(point + side * width * 0.5, region_uv(LEAF, 1.0, t))))
    for (a, b), (c, d) in zip(rows, rows[1:]):
        builder.faces.append((a, b, d, c))


def build_camomile(rng):
    builder = MeshBuilder()
    up = np.array([0.0, 0.0, 1.0])
    stems = []
    for height in (0.50, 0.44, 0.38):
        heading = rng.uniform(0.0, 2.0 * math.pi)
        root = np.array([rng.uniform(-0.012, 0.012), rng.uniform(-0.012, 0.012), -0.01])
        lean = math.radians(rng.uniform(4.0, 14.0))
        bend = math.radians(rng.uniform(-6.0, 10.0))
        spine = [root]
        segments = 5
        for i in range(1, segments + 1):
            t = (i - 0.5) / segments
            angle = lean + bend * t
            spine.append(spine[-1] + np.array([math.sin(angle) * math.cos(heading), math.sin(angle) * math.sin(heading),
                                               math.cos(angle)]) * height / segments)
        add_stem(builder, spine, 0.0018, 0.0012)
        stems.append((spine, heading))

        # Цветок смотрит вверх и немного вбок, по наклону стебля
        top_direction = spine[-1] - spine[-2]
        tilt = direction(heading + rng.uniform(-0.8, 0.8), math.radians(rng.uniform(55.0, 75.0)))
        normal = top_direction / np.linalg.norm(top_direction) * 0.4 + tilt * 0.6
        add_head(builder, spine[-1] + normal / np.linalg.norm(normal) * 0.002, normal, rng.uniform(0.0, 2.0 * math.pi))

    # Розетка листьев у земли
    leaves = 6
    for i in range(leaves):
        heading = 2.0 * math.pi * i / leaves + rng.uniform(-0.3, 0.3)
        root = np.array([rng.uniform(-0.01, 0.01), rng.uniform(-0.01, 0.01), -0.005])
        add_leaf(builder, root, heading, math.radians(rng.uniform(40.0, 55.0)), math.radians(rng.uniform(-5.0, 10.0)),
                 rng.uniform(0.09, 0.13), LEAF_WIDTH, math.radians(rng.uniform(-15.0, 15.0)))
    # По листу на двух стеблях
    for spine, heading in stems[:2]:
        attach = spine[2]
        add_leaf(builder, attach, heading + math.pi + rng.uniform(-0.5, 0.5), math.radians(45.0), math.radians(15.0),
                 0.065, LEAF_WIDTH * 0.7, 0.0)
    return builder.build('Camomile')


def main():
    out = output_path()
    folder = os.path.splitext(out)[0] + '_images'
    os.makedirs(folder, exist_ok=True)

    clear_scene()
    material = camomile_material(camomile_atlas(folder))
    obj = bpy.data.objects.new('Camomile', build_camomile(np.random.default_rng(SEED)))
    obj.data.materials.append(material)
    bpy.context.collection.objects.link(obj)

    bpy.ops.export_scene.gltf(filepath=out, export_format='GLB', export_yup=True, export_apply=True,
                              export_tangents=True, export_image_format='AUTO', export_materials='EXPORT')
    print('Written: ' + out)


main()
