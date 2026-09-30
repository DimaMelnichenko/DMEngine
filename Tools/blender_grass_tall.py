# Высокие пучки травы — подмена моделей Poly Haven grass_medium_01_tall_a/b/c в слое Meadow, пока архива
# grass_medium_01 нет на машине (без файлов мешей расстановка рисует кубы-заглушки, и замер нагрузки луга
# ничего не значит). Геометрия — как у Tools/blender_grass.py (травинки полосками, без альфа-карточек), но выше
# (0,55–1,0 м) и с тремя LOD; число треугольников — порядка пучков Poly Haven (LOD0 ~900, LOD1 ~300, LOD2 ~90).
# Имена объектов и картинки — как у настоящего ассета, чтобы файлы легли на пути из base.db3
# (Meshes\models\grass_medium_01\grass_medium_01_tall_a_LOD0.bin, Textures\models\grass_medium_01\grass_medium_01_BaseColor.png)
# и строки базы не менялись: импорт — в копию базы (--db), см. docs/models.md. Запуск внутри Blender без окна:
#   blender -b --factory-startup --python Tools/blender_grass_tall.py -- Meshes/source/grass_medium_01.glb
# Затем (копия базы, чтобы base.db3 осталась прежней):
#   copy base.db3 %TEMP%\base_sandbox.db3
#   python Tools/import_gltf.py Meshes/source/grass_medium_01.glb --scatter --lod-ranges 4,12 --db %TEMP%\base_sandbox.db3
# Карты MetallicRoughness и Normal экземпляра материала (Textures 52, 53) — Tools/gen_flat_pbr_maps.py.
# Настоящий ассет возвращается командами из docs/models.md (export_polyhaven.py + import_gltf.py) — файлы перезапишутся.
# Сообщения скрипта — ASCII (правило Tools/).
import math
import os
import sys

import bpy
import numpy as np

ASSET = 'grass_medium_01'
COLUMNS = 8                                  # оттенков в текстуре
# Варианты: seed, число травинок LOD0, длина травинок (м), радиус пучка (м)
VARIANTS = {
    'tall_a': {'seed': 11, 'blades': 48, 'length': (0.55, 0.80), 'radius': 0.07},
    'tall_b': {'seed': 12, 'blades': 44, 'length': (0.70, 1.00), 'radius': 0.08},
    'tall_c': {'seed': 13, 'blades': 40, 'length': (0.60, 0.90), 'radius': 0.09},
}
# LOD: доля травинок, сегментов на травинку, множитель ширины (вдали травинка тоньше пикселя — шире, чтобы пучок не редел)
LODS = [(1.0, 8, 1.0), (0.5, 4, 1.8), (0.25, 2, 3.0)]
TINT = (0.977, 0.8, 1.0)                     # тон GrassClump (BASE_COLOR_FACTOR в blender_grass.py), здесь запечён в картинку


def output_path():
    argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    if not argv:
        raise SystemExit('usage: blender -b --factory-startup --python Tools/blender_grass_tall.py -- <out.glb>')
    return os.path.abspath(argv[0])


def clear_scene():
    for obj in list(bpy.data.objects):
        bpy.data.objects.remove(obj, do_unlink=True)


def base_color(folder, width=64, height=256):
    """Столбцы — оттенки, строки — высота травинки (строка 0 — корень). Как GrassColor, тон запечён."""
    rng = np.random.default_rng(7)
    t = (np.arange(height) + 0.5) / height
    root = np.array([0.10, 0.18, 0.05])
    middle = np.array([0.32, 0.50, 0.12])
    tip = np.array([0.58, 0.66, 0.25])
    straw = np.array([0.72, 0.66, 0.36])
    lower = t < 0.5
    k = np.where(lower, t * 2.0, (t - 0.5) * 2.0)[:, None]
    gradient = np.where(lower[:, None], root + (middle - root) * k, middle + (tip - middle) * k)

    column_width = width // COLUMNS
    rgb = np.zeros((height, width, 3))
    for column in range(COLUMNS):
        brightness = rng.uniform(0.8, 1.15)
        dry = rng.uniform(0.0, 0.5) * t[:, None] ** 2
        shade = (gradient * (1.0 - dry) + straw * dry) * brightness
        u = (np.arange(column_width) + 0.5) / column_width
        vein = 1.0 + 0.12 * np.exp(-((u - 0.5) / 0.12) ** 2)
        rgb[:, column * column_width:(column + 1) * column_width] = shade[:, None, :] * vein[None, :, None]
    rgb *= np.array(TINT)
    rgba = np.concatenate([np.clip(rgb, 0.0, 1.0), np.ones((height, width, 1))], -1).astype(np.float32)

    image = bpy.data.images.new(ASSET + '_BaseColor', width, height)
    image.pixels.foreach_set(rgba.ravel())
    image.filepath_raw = os.path.join(folder, image.name + '.png')
    image.file_format = 'PNG'
    image.save()
    return image


def grass_material(image):
    mat = bpy.data.materials.new(ASSET)
    if bpy.app.version < (5, 0, 0):
        mat.use_nodes = True
    mat.use_backface_culling = False
    nodes, links = mat.node_tree.nodes, mat.node_tree.links
    bsdf = nodes['Principled BSDF']
    bsdf.inputs['Metallic'].default_value = 0.0
    bsdf.inputs['Roughness'].default_value = 0.6
    tex = nodes.new('ShaderNodeTexImage')
    tex.image = image
    links.new(tex.outputs['Color'], bsdf.inputs['Base Color'])
    # Параметры движка — как у настоящего grass_medium_01 (экземпляр материала 30 в base.db3)
    mat['WindWeight'] = 1.0
    mat['DiffuseTransmissionFactor'] = 0.4
    mat['DiffuseTransmissionColorFactor'] = [1.0, 1.0, 0.6]
    mat['DiffuseTransmissionColor'] = 'BaseColor'
    mat['DitheredLODTransition'] = 'true'
    return mat


def blade_params(variant):
    rng = np.random.default_rng(variant['seed'])
    blades = []
    for index in range(variant['blades']):
        radius = variant['radius'] * math.sqrt(rng.uniform())
        around = rng.uniform(0.0, 2.0 * math.pi)
        outward = radius / variant['radius']
        blades.append({
            'root': (radius * math.cos(around), radius * math.sin(around), -0.03),
            'heading': around + rng.uniform(-0.7, 0.7),
            'twist': rng.uniform(-0.4, 0.4),
            'lean': math.radians(rng.uniform(2.0, 8.0) + 22.0 * outward),
            'bend': math.radians(rng.uniform(25.0, 70.0)),
            'length': rng.uniform(*variant['length']),
            'width': rng.uniform(0.012, 0.022),
            'column': index % COLUMNS,
        })
    return blades


def build_mesh(name, blades, segments, width_scale):
    vertices, faces, uvs = [], [], []
    column_width = 1.0 / COLUMNS
    margin = column_width * 0.15
    for blade in blades:
        heading = blade['heading']
        side = (-math.sin(heading + blade['twist']), math.cos(heading + blade['twist']), 0.0)
        u0 = blade['column'] * column_width + margin
        u1 = (blade['column'] + 1) * column_width - margin

        point = np.array(blade['root'])
        step = blade['length'] / segments
        first = len(vertices)
        for i in range(segments + 1):
            t = i / segments
            if i > 0:
                middle = t - 0.5 / segments
                angle = blade['lean'] + blade['bend'] * middle * middle
                direction = np.array([math.sin(angle) * math.cos(heading), math.sin(angle) * math.sin(heading),
                                      math.cos(angle)])
                point = point + direction * step
            if i == segments:
                vertices.append(tuple(point))
                uvs.append(((u0 + u1) * 0.5, 1.0))
            else:
                half = blade['width'] * width_scale * 0.5 * (1.0 - t) ** 0.6
                vertices.append(tuple(point - np.array(side) * half))
                vertices.append(tuple(point + np.array(side) * half))
                uvs.append((u0, t))
                uvs.append((u1, t))
        for i in range(segments - 1):
            a = first + 2 * i
            faces.append((a, a + 1, a + 3, a + 2))
        last = first + 2 * (segments - 1)
        faces.append((last, last + 1, first + 2 * segments))

    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(vertices, [], faces)
    uv_layer = mesh.uv_layers.new(name='UVMap')
    for polygon in mesh.polygons:
        polygon.use_smooth = True
        for loop_index in polygon.loop_indices:
            uv_layer.data[loop_index].uv = uvs[mesh.loops[loop_index].vertex_index]
    mesh.update()
    return mesh


def make_clump(name, blades, segments, width_scale, material, location):
    mesh = build_mesh(name, blades, segments, width_scale)
    obj = bpy.data.objects.new(name, mesh)
    obj.data.materials.append(material)
    obj.location = location
    bpy.context.collection.objects.link(obj)
    triangles = sum(len(p.vertices) - 2 for p in mesh.polygons)
    print('%s: %d blades, %d triangles' % (name, len(blades), triangles))
    return obj


def main():
    out = output_path()
    folder = os.path.splitext(out)[0] + '_images'
    os.makedirs(folder, exist_ok=True)

    clear_scene()
    material = grass_material(base_color(folder))
    for row, (variant_name, variant) in enumerate(VARIANTS.items()):
        blades = blade_params(variant)
        for lod, (share, segments, width_scale) in enumerate(LODS):
            name = '%s_%s' % (ASSET, variant_name) + ('_LOD%d' % lod if lod else '')
            count = max(4, int(round(len(blades) * share)))
            # LOD стоят в ряд, как принято в Blender: импортёр берёт LOD относительно своего origin
            make_clump(name, blades[:count], segments, width_scale, material, (lod * 1.0, row * 1.0, 0.0))

    bpy.ops.export_scene.gltf(filepath=out, export_format='GLB', export_yup=True, export_apply=True,
                              export_tangents=True, export_image_format='AUTO', export_materials='EXPORT',
                              export_extras=True)
    print('Written: ' + out)


main()
