# LOD уменьшением сетки для моделей без своих LOD (сканы с Fab не Megascans: обломки стволов, коряги) → .glb для
# Tools/import_gltf.py --scatter. Сценарий Blender без окна:
#   - импортирует файлы --inputs (glTF / GLB / FBX), каждый файл — вариант (<имя>_VarA, _VarB …); меши файла
#     объединяются (--split-objects — каждый меш файла свой вариант), положение и поворот — в вершины, начало координат —
#     середина по горизонтали, низ — z = 0;
#   - LOD — копии с модификатором Decimate (схлопывание рёбер) на доли --lods от треугольников исходника (первая доля —
#     LOD0, 1 — без упрощения): <имя>_VarA, <имя>_VarA_LOD1 … (суффикс понимает импортёр);
#   - текстуры сжимаются до --texture-size (сканы приходят 8K); --single-sided — отсечение задних граней (замкнутое тело);
#   - печатает треугольники и габариты LOD, экспортирует .glb (материалы glTF как есть, без цветов вершин).
# Запуск из корня проекта:
#   blender -b --factory-startup --python Tools/make_lods.py -- --inputs a.gltf b.gltf --name trunk
#       --out Meshes/source/trunk.glb --lods 0.2,0.05,0.012 [--texture-size 2048] [--single-sided]
import argparse
import os
import sys

import bpy
import mathutils
import numpy as np


def triangles(obj):
    mesh = obj.data
    mesh.calc_loop_triangles()
    return len(mesh.loop_triangles)


def import_file(path, split):
    """Объекты вариантов файла: один (меши объединены) или по мешу на вариант (split)"""
    before = set(bpy.data.objects)
    ext = os.path.splitext(path)[1].lower()
    if ext == '.fbx':
        bpy.ops.import_scene.fbx(filepath=os.path.abspath(path))
    else:
        bpy.ops.import_scene.gltf(filepath=os.path.abspath(path))
    new = set(bpy.data.objects) - before
    meshes = [o for o in new if o.type == 'MESH']
    if not meshes:
        raise SystemExit('error: no meshes in %s' % path)
    for o in meshes:
        o.data.transform(o.matrix_world)
        o.matrix_world = mathutils.Matrix.Identity(4)
    for o in new - set(meshes):
        bpy.data.objects.remove(o, do_unlink=True)
    if split:
        return [ground(o) for o in sorted(meshes, key=lambda o: o.name)]
    if len(meshes) > 1:
        for o in bpy.context.selected_objects:
            o.select_set(False)
        for o in meshes:
            o.select_set(True)
        bpy.context.view_layer.objects.active = meshes[0]
        bpy.ops.object.join()
    return [ground(meshes[0])]


def ground(obj):
    """Начало координат — середина по горизонтали, низ — z = 0"""
    corners = np.array([v.co for v in obj.data.vertices])
    low, high = corners.min(axis=0), corners.max(axis=0)
    obj.data.transform(mathutils.Matrix.Translation(-mathutils.Vector(((low[0] + high[0]) / 2.0, (low[1] + high[1]) / 2.0, low[2]))))
    return obj


def main():
    argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    parser = argparse.ArgumentParser(prog='make_lods.py')
    parser.add_argument('--inputs', nargs='+', required=True, help='glTF / GLB / FBX files, one per variant')
    parser.add_argument('--name', required=True, help='model name prefix: <name>_VarA …')
    parser.add_argument('--out', required=True, help='output .glb')
    parser.add_argument('--lods', default='1,0.25,0.06', help='share of the source triangles per LOD, LOD0 first')
    parser.add_argument('--texture-size', type=int, default=2048)
    parser.add_argument('--single-sided', action='store_true', help='back-face culling (closed scans)')
    parser.add_argument('--split-objects', action='store_true', help='every mesh of a file is its own variant')
    args = parser.parse_args(argv)
    ratios = [float(r) for r in args.lods.split(',')]

    bpy.ops.wm.read_factory_settings(use_empty=True)
    sources = [obj for path in args.inputs for obj in import_file(path, args.split_objects)]
    for index, source in enumerate(sources):
        variant = 'Var' + chr(ord('A') + index)
        source_tris = triangles(source)
        for lod, ratio in enumerate(ratios):
            obj = source.copy()
            obj.data = source.data.copy()
            bpy.context.scene.collection.objects.link(obj)
            if ratio < 1.0:
                modifier = obj.modifiers.new('decimate', 'DECIMATE')
                modifier.decimate_type = 'COLLAPSE'
                modifier.ratio = ratio
                bpy.context.view_layer.objects.active = obj
                bpy.ops.object.modifier_apply(modifier=modifier.name)
            obj.name = '%s_%s' % (args.name, variant) if lod == 0 else '%s_%s_LOD%d' % (args.name, variant, lod)
            corners = np.array([v.co for v in obj.data.vertices])
            size = corners.max(axis=0) - corners.min(axis=0)
            print('object %s: %d triangles (%.3f of %d), size %.2f x %.2f x %.2f m' % (
                obj.name, triangles(obj), ratio, source_tris, size[0], size[1], size[2]))
        bpy.data.objects.remove(source, do_unlink=True)

    for mat in bpy.data.materials:
        mat.use_backface_culling = args.single_sided
    for image in bpy.data.images:
        if image.size[0] > args.texture_size or image.size[1] > args.texture_size:
            print('texture %s: %dx%d -> %d' % (image.name, image.size[0], image.size[1], args.texture_size))
            image.scale(args.texture_size, args.texture_size)

    out = os.path.abspath(args.out)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    bpy.ops.export_scene.gltf(filepath=out, export_format='GLB', export_yup=True, export_apply=True,
                              export_tangents=True, export_image_format='AUTO', export_materials='EXPORT',
                              export_extras=True, export_animations=False, export_vertex_color='NONE')
    print('Written: ' + out)


if __name__ == '__main__':
    main()
