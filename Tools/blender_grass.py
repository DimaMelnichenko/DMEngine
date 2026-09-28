# Пучок травы для слоёв расстановки (набор Meadow). Запускается внутри Blender без окна:
#   blender -b --factory-startup --python Tools/blender_grass.py -- <выход.glb>
# Затем импорт моделью для расстановки:
#   python Tools/import_gltf.py <выход.glb> --scatter
# Сцена:
#   GrassClump — пучок из 36 травинок геометрией (без альфа-карточек, как трава вблизи в Ghost of Tsushima):
#     каждая — сужающаяся изогнутая полоска из 4 сегментов с вершиной-кончиком; корни в круге радиусом 5 см,
#     внешние травинки наклонены сильнее, к кончику травинка клонится (изгиб растёт квадратично);
#   GrassClump_LOD1 — те же первые 16 травинок из 2 сегментов, в 2,6 раза шире: вдали тонкая травинка меньше
#     пикселя, и пучок выглядел бы редким — для дальнего кольца.
# Материал GrassMat двусторонний (Backface Culling выключен → doubleSided в glTF): у тонкой травинки видны обе
# стороны. Базовый цвет — текстура GrassColor: 8 столбцов оттенков (у каждой травинки свой), по высоте — градиент
# от тёмного корня к светлому, чуть выгоревшему кончику; тон подтянут к траве Poly Haven множителем (узел Mix,
# Multiply → baseColorFactor glTF). Параметры движка, которых нет в glTF (ветер, свет насквозь, смена LOD
# дизерингом), — Custom Properties материала: в glTF — extras, импортёр пишет их в экземпляр материала.
# Origin — у корней (ось Z Blender вверх), травинки начинаются чуть ниже нуля, чтобы на склоне не висели в воздухе.
# Сообщения скрипта — ASCII (правило Tools/).
import math
import os
import sys

import bpy
import numpy as np

BLADES = 36
LOD1_BLADES = 16
COLUMNS = 8          # оттенков в текстуре
SEED = 7
BASE_COLOR_FACTOR = (0.977, 0.8, 1.0, 1.0)   # тон — к траве Poly Haven grass_medium_01 рядом в том же слое


def output_path():
    argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    if not argv:
        raise SystemExit('usage: blender -b --factory-startup --python Tools/blender_grass.py -- <out.glb>')
    return os.path.abspath(argv[0])


def clear_scene():
    for obj in list(bpy.data.objects):
        bpy.data.objects.remove(obj, do_unlink=True)


def grass_color(folder, width=64, height=256):
    """Столбцы — оттенки, строки — высота травинки (пиксели Blender идут снизу вверх: строка 0 — корень)."""
    rng = np.random.default_rng(SEED)
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
        dry = rng.uniform(0.0, 0.4) * t[:, None] ** 2          # выгорает к кончику
        shade = (gradient * (1.0 - dry) + straw * dry) * brightness
        # Светлая жилка посередине травинки
        u = (np.arange(column_width) + 0.5) / column_width
        vein = 1.0 + 0.12 * np.exp(-((u - 0.5) / 0.12) ** 2)
        rgb[:, column * column_width:(column + 1) * column_width] = shade[:, None, :] * vein[None, :, None]
    rgba = np.concatenate([np.clip(rgb, 0.0, 1.0), np.ones((height, width, 1))], -1).astype(np.float32)

    image = bpy.data.images.new('GrassColor', width, height)
    image.pixels.foreach_set(rgba.ravel())   # значения в sRGB, как у картинки цвета
    image.filepath_raw = os.path.join(folder, image.name + '.png')
    image.file_format = 'PNG'
    image.save()
    return image


def grass_material(image):
    mat = bpy.data.materials.new('GrassMat')
    if bpy.app.version < (5, 0, 0):  # в Blender 5 узлы материала включены всегда
        mat.use_nodes = True
    mat.use_backface_culling = False
    nodes, links = mat.node_tree.nodes, mat.node_tree.links
    bsdf = nodes['Principled BSDF']
    bsdf.inputs['Metallic'].default_value = 0.0
    bsdf.inputs['Roughness'].default_value = 0.55
    tex = nodes.new('ShaderNodeTexImage')
    tex.image = image
    # Множитель тона — Mix в режиме Multiply с постоянным цветом: экспортёр glTF пишет его в baseColorFactor
    tint = nodes.new('ShaderNodeMix')
    tint.data_type = 'RGBA'
    tint.blend_type = 'MULTIPLY'
    tint.inputs['Factor'].default_value = 1.0
    color_a, color_b = [s for s in tint.inputs if s.type == 'RGBA']
    links.new(tex.outputs['Color'], color_a)
    color_b.default_value = BASE_COLOR_FACTOR
    links.new([s for s in tint.outputs if s.type == 'RGBA'][0], bsdf.inputs['Base Color'])
    # Параметры движка (docs/materials.md, docs/wind.md): extras glTF → экземпляр материала
    mat['WindWeight'] = 1.0
    mat['DiffuseTransmissionFactor'] = 0.4
    mat['DiffuseTransmissionColorFactor'] = [1.0, 1.0, 0.6]
    mat['DiffuseTransmissionColor'] = 'BaseColor'
    mat['DitheredLODTransition'] = 'true'
    return mat


def blade_params(rng):
    """Случайные параметры травинок: одни и те же для обоих LOD."""
    blades = []
    for index in range(BLADES):
        radius = 0.05 * math.sqrt(rng.uniform())
        around = rng.uniform(0.0, 2.0 * math.pi)
        outward = radius / 0.05
        blades.append({
            'root': (radius * math.cos(around), radius * math.sin(around), -0.02),
            'heading': around + rng.uniform(-0.6, 0.6),            # куда наклонена
            'twist': rng.uniform(-0.35, 0.35),                     # поворот плоскости травинки
            'lean': math.radians(rng.uniform(3.0, 10.0) + 25.0 * outward),
            'bend': math.radians(rng.uniform(20.0, 60.0)),         # сколько добавляется к наклону к кончику
            'length': rng.uniform(0.32, 0.55),
            'width': rng.uniform(0.012, 0.02),
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
                vertices.append(tuple(point))                     # кончик
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
    obj = bpy.data.objects.new(name, build_mesh(name, blades, segments, width_scale))
    obj.data.materials.append(material)
    obj.location = location
    bpy.context.collection.objects.link(obj)
    return obj


def main():
    out = output_path()
    folder = os.path.splitext(out)[0] + '_images'
    os.makedirs(folder, exist_ok=True)

    clear_scene()
    material = grass_material(grass_color(folder))
    blades = blade_params(np.random.default_rng(SEED))
    make_clump('GrassClump', blades, 4, 1.0, material, (0.0, 0.0, 0.0))
    # LOD1 стоит в стороне, как обычно в Blender: импортёр берёт LOD относительно своего origin
    make_clump('GrassClump_LOD1', blades[:LOD1_BLADES], 2, 2.6, material, (1.0, 0.0, 0.0))

    bpy.ops.export_scene.gltf(filepath=out, export_format='GLB', export_yup=True, export_apply=True,
                              export_tangents=True, export_image_format='AUTO', export_materials='EXPORT',
                              export_extras=True)
    print('Written: ' + out)


main()
