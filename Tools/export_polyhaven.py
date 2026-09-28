# Модели Poly Haven (архив .blend.zip) → .glb для Tools/import_gltf.py --scatter: растения и камни расстановки.
# Сцена Poly Haven не подходит экспортёру glTF как есть: материалы собраны на узлах-группах (цвет, «сухой» цвет,
# альфа, подповерхностное рассеяние), которых экспортёр не понимает, альфа — отдельной картинкой, нормали и
# шероховатость — в EXR, у травы — 4k. Сценарий:
#   - распаковывает архив во временную папку и открывает .blend;
#   - оставляет объекты по шаблону --objects (регулярное выражение по имени), служебные копии (geonodes_, _geo,
#     шар превью) отбрасываются шаблоном --exclude;
#   - LOD — суффикс _LOD<N> Poly Haven (его понимает импортёр); --lod-offset k отбрасывает LOD ниже k (у одуванчика
#     LOD0 — 23 тыс. треугольников) и нумерует оставшиеся с нуля; у объектов без LOD --decimate-tris строит LOD
#     модификатором Decimate до заданного числа треугольников (у камней LOD нет, 5–16 тыс. треугольников);
#   - собирает материал заново простым Principled BSDF: базовый цвет с альфой из отдельной карты (Round — alphaMode
#     MASK), шероховатость в G картинки metallicRoughness, нормаль (соглашение OpenGL, как у glTF) — в 8-битный PNG;
#     картинки — до --texture-size; двусторонний, если у исходного материала не включено отсечение задних граней
#     или задан --double-sided (--single-sided — одностороний для замкнутых мешей, камней);
#   - экспортирует .glb (Apply Modifiers, касательные).
# Запуск из корня проекта:
#   blender -b --factory-startup --python Tools/export_polyhaven.py -- --archive DownloadResources/grass_medium_01_4k.blend.zip
#       --objects "grass_medium_01_(mid_b|tall_a)_LOD\d" --out Meshes/source/grass_medium_01.glb [--texture-size 2048]
import argparse
import os
import re
import sys
import tempfile
import zipfile

import bpy
import numpy as np

ROLES = (
    ('alpha', re.compile(r'_alpha_', re.I)),
    ('normal', re.compile(r'_nor_gl_', re.I)),
    ('arm', re.compile(r'_arm_', re.I)),
    ('rough', re.compile(r'_rough_', re.I)),
    ('diff', re.compile(r'(?<!dry)_diff_', re.I)),
)
LOD_PATTERN = re.compile(r'^(.*)_LOD(\d+)$', re.I)


def open_archive(archive):
    folder = tempfile.mkdtemp(prefix='dm_polyhaven_')
    with zipfile.ZipFile(archive) as zf:
        zf.extractall(folder)
    blends = [os.path.join(root, f) for root, _, files in os.walk(folder) for f in files if f.lower().endswith('.blend')]
    if len(blends) != 1:
        raise SystemExit('error: expected one .blend in %s, found %d' % (archive, len(blends)))
    bpy.ops.wm.open_mainfile(filepath=blends[0])
    return folder


def image_roles(material):
    """Картинки материала по назначению — по имени файла Poly Haven (_diff_, _alpha_, _nor_gl_, _rough_, _arm_)"""
    found = {}
    for node in material.node_tree.nodes:
        if node.type != 'TEX_IMAGE' or not node.image:
            continue
        name = bpy.path.basename(node.image.filepath)   # «//textures\…»: os.path в Windows принял бы его за сетевой путь
        for role, pattern in ROLES:
            if pattern.search(name) and role not in found:
                found[role] = node.image
                break
    return found


def pixels(image, size):
    """Значения картинки как в файле (байты JPG / PNG, числа EXR), высота × ширина × 4 в порядке строк Blender"""
    copy = bpy.data.images.load(bpy.path.abspath(image.filepath), check_existing=False)
    copy.colorspace_settings.name = 'Non-Color'
    if tuple(copy.size) != (size, size):
        copy.scale(size, size)
    data = np.empty(size * size * 4, dtype=np.float32)
    copy.pixels.foreach_get(data)
    bpy.data.images.remove(copy)
    return data.reshape(size, size, 4)


def save_png(name, rgba, folder, color):
    height, width = rgba.shape[:2]
    image = bpy.data.images.new(name, width, height, alpha=True)
    image.colorspace_settings.name = 'sRGB' if color else 'Non-Color'
    image.pixels.foreach_set(np.clip(rgba, 0.0, 1.0).ravel())
    image.filepath_raw = os.path.join(folder, name + '.png')
    image.file_format = 'PNG'
    image.save()
    return image


def export_material(source, folder, size, double_sided):
    roles = image_roles(source)
    if 'diff' not in roles:
        raise SystemExit('error: material %s has no _diff_ texture' % source.name)
    base = pixels(roles['diff'], size)
    has_alpha = 'alpha' in roles
    if has_alpha:
        base[..., 3] = pixels(roles['alpha'], size)[..., 0]
    else:
        base[..., 3] = 1.0
    # metallicRoughness glTF: G — шероховатость, B — металличность (0); R не читается (затенение — отдельная карта)
    rough = pixels(roles['arm'], size)[..., 1] if 'arm' in roles else \
        pixels(roles['rough'], size)[..., 0] if 'rough' in roles else np.full((size, size), 0.8, np.float32)
    mr = np.stack([np.ones_like(rough), rough, np.zeros_like(rough), np.ones_like(rough)], axis=-1)

    # Имя — как у исходного материала: по нему импортёр называет экземпляр материала
    name = source.name
    source.name = name + '_polyhaven'
    mat = bpy.data.materials.new(name)
    if bpy.app.version < (5, 0, 0):  # в Blender 5 узлы материала включены всегда
        mat.use_nodes = True
    mat.use_backface_culling = not double_sided
    nodes, links = mat.node_tree.nodes, mat.node_tree.links
    bsdf = nodes['Principled BSDF']

    tex = nodes.new('ShaderNodeTexImage')
    tex.image = save_png(name + '_BaseColor', base, folder, True)
    links.new(tex.outputs['Color'], bsdf.inputs['Base Color'])
    if has_alpha:
        # Альфа через Round — экспортёр glTF распознаёт отсечение: alphaMode MASK, alphaCutoff 0,5
        clip = nodes.new('ShaderNodeMath')
        clip.operation = 'ROUND'
        links.new(tex.outputs['Alpha'], clip.inputs[0])
        links.new(clip.outputs[0], bsdf.inputs['Alpha'])

    mr_tex = nodes.new('ShaderNodeTexImage')
    mr_tex.image = save_png(name + '_MetallicRoughness', mr, folder, False)
    split = nodes.new('ShaderNodeSeparateColor')
    links.new(mr_tex.outputs['Color'], split.inputs['Color'])
    links.new(split.outputs['Green'], bsdf.inputs['Roughness'])
    links.new(split.outputs['Blue'], bsdf.inputs['Metallic'])

    if 'normal' in roles:
        normal = pixels(roles['normal'], size)
        normal[..., 3] = 1.0
        n_tex = nodes.new('ShaderNodeTexImage')
        n_tex.image = save_png(name + '_Normal', normal, folder, False)
        n_map = nodes.new('ShaderNodeNormalMap')
        links.new(n_tex.outputs['Color'], n_map.inputs['Color'])
        links.new(n_map.outputs['Normal'], bsdf.inputs['Normal'])
    print('material %s: %dx%d%s%s' % (name, size, size, ', alpha' if has_alpha else '', ', double-sided' if double_sided else ''))
    return mat


def triangle_count(obj):
    # После модификаторов (Decimate, Displace) — как их применит экспорт
    evaluated = obj.evaluated_get(bpy.context.evaluated_depsgraph_get())
    mesh = evaluated.to_mesh()
    mesh.calc_loop_triangles()
    count = len(mesh.loop_triangles)
    evaluated.to_mesh_clear()
    return count


def main():
    argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    parser = argparse.ArgumentParser(prog='export_polyhaven.py')
    parser.add_argument('--archive', required=True, help='Poly Haven .blend.zip')
    parser.add_argument('--objects', required=True, help='regular expression for object names to export')
    parser.add_argument('--exclude', default=r'geonodes_|_geo$|geometry_nodes|sphere', help='regular expression for names to skip')
    parser.add_argument('--out', required=True, help='output .glb')
    parser.add_argument('--texture-size', type=int, default=2048)
    parser.add_argument('--lod-offset', type=int, default=0, help='drop LODs below this one and renumber from 0')
    parser.add_argument('--decimate-tris', help='for objects without LODs: triangle counts of generated LODs, e.g. 1200,300,80')
    parser.add_argument('--double-sided', action='store_true', help='double-sided materials regardless of the source')
    parser.add_argument('--single-sided', action='store_true', help='single-sided materials (closed meshes: rocks)')
    args = parser.parse_args(argv)

    open_archive(args.archive)
    include = re.compile(args.objects)
    exclude = re.compile(args.exclude, re.I)
    keep = [o for o in bpy.data.objects if o.type == 'MESH' and include.search(o.name) and not exclude.search(o.name)]
    for obj in list(bpy.data.objects):
        if obj not in keep:
            bpy.data.objects.remove(obj, do_unlink=True)
    if not keep:
        raise SystemExit('error: no objects match %s' % args.objects)

    # LOD: отбросить нижние и перенумеровать
    for obj in keep[:]:
        match = LOD_PATTERN.match(obj.name)
        if not match:
            continue
        lod = int(match.group(2))
        if lod < args.lod_offset:
            keep.remove(obj)
            bpy.data.objects.remove(obj, do_unlink=True)
        else:
            obj.name = '%s_LOD%d' % (match.group(1), lod - args.lod_offset)

    # LOD модификатором Decimate у объектов без своих LOD
    if args.decimate_tris:
        targets = [int(t) for t in args.decimate_tris.split(',')]
        for obj in [o for o in keep if not LOD_PATTERN.match(o.name)]:
            source_tris = triangle_count(obj)
            base = obj.name
            # Копии — до модификаторов: копия объекта унаследовала бы Decimate предыдущего LOD
            lod_objects = [obj]
            for lod in range(1, len(targets)):
                copy = obj.copy()
                copy.data = obj.data.copy()
                bpy.context.scene.collection.objects.link(copy)
                keep.append(copy)
                lod_objects.append(copy)
            for lod, (lod_obj, target) in enumerate(zip(lod_objects, targets)):
                if target < source_tris:
                    modifier = lod_obj.modifiers.new('LOD decimate', 'DECIMATE')
                    modifier.ratio = target / source_tris
                lod_obj.name = '%s_LOD%d' % (base, lod)
            print('%s: %d triangles -> LOD %s' % (base, source_tris, targets))

    # Материалы: по одному на исходный
    folder = tempfile.mkdtemp(prefix='dm_polyhaven_textures_')
    replaced = {}
    for obj in keep:
        for slot in obj.material_slots:
            source = slot.material
            if source is None:
                continue
            if source.name not in replaced:
                double_sided = args.double_sided or (not args.single_sided and not source.use_backface_culling)
                replaced[source.name] = export_material(source, folder, args.texture_size, double_sided)
            slot.material = replaced[source.name]

    for obj in sorted(keep, key=lambda o: o.name):
        print('object %s: %d triangles' % (obj.name, triangle_count(obj)))

    out = os.path.abspath(args.out)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    bpy.ops.export_scene.gltf(filepath=out, export_format='GLB', export_yup=True, export_apply=True,
                              export_tangents=True, export_image_format='AUTO', export_materials='EXPORT',
                              export_animations=False)
    print('Written: ' + out)


# Функции (open_archive, export_material) берёт и Tools/blender_fir.py
if __name__ == '__main__':
    main()
