# Дерево из готового меша (скан или модель с Fab, Sketchfab: ствол и ветви — куски труб, листва — карточки) → .glb для
# Tools/import_gltf.py с данными ветра PBRTree (docs/wind.md, «Ветер дерева — схема SpeedTree») и LOD. Сценарий Blender
# без окна:
#   - импортирует FBX, объединяет меши древесины (--wood, подстрока имени) и листвы (--leaves); положение и поворот — в
#     вершины, начало координат — середина основания ствола;
#   - иерархия ветвей — по касаниям: куски древесины (острова сетки) цепляются от корня (остров с самой низкой точкой) к
#     ближайшему уже прицепленному, допуск растёт ступенями; ствол — путь от корня по самым толстым кускам, пока кусок не
#     тоньше --trunk-min-radius; ветвь первого уровня — поддерево куска, отходящего от ствола;
#   - данные ветра: ствол — только доля высоты; ветвь — начало на стволе и вес (расстояние от начала / наибольшее в
#     ветви); карточка листвы — ветвь ближайшей к основанию древесины, веточка — основание карточки и доля длины (её вес
#     и рябь); атрибуты _wind_branch1 / _wind_branch2 (FLOAT_COLOR, оси glTF: x, z, −y) и _wind_weights (FLOAT2);
#   - LOD: древесина — без кусков тоньше --wood-drop (м; веточки вдали не видны, а Decimate тонкую трубку не упрощает) и
#     Decimate на доли --wood-ratio, листва — доля карточек --leaf-keep (выбор по номеру карточки, одни
#     и те же во всех LOD), увеличенных на --leaf-scale вокруг основания; данные ветра вершины LOD — у ближайшей вершины
#     LOD0 (движение при смене LOD совпадает);
#   - материалы по картам из FBX: кора — цвет, нормаль, шероховатость; листва — цвет с альфой из карты прозрачности,
#     нормаль, шероховатость (--leaf-roughness, если карты нет); нормали OpenGL; параметры движка (extras) — оба материала
#     одинаково (ветер), листве ещё просвет (--transmission) и нормаль кроны;
#   - LOD0 — простая коллизия ствола: капсула радиусом ствола у земли.
# Запуск из корня проекта:
#   blender -b --factory-startup --python Tools/tree_from_mesh.py -- --fbx "DownloadResources/<ассет>/Beech Tree.fbx"
#       --name Beech --out Meshes/source/beech.glb --wood Trunk --leaves Leaves [--material-param WindBranchAngle=3]
import argparse
import math
import os
import sys
import tempfile

import bpy
import mathutils
import numpy as np
from mathutils.kdtree import KDTree

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from export_polyhaven import material_param, save_png  # noqa: E402


def parse_list(text, count):
    values = [float(v) for v in text.split(',')]
    if len(values) != count:
        raise SystemExit('error: expected %d values in %s' % (count, text))
    return values


def join_objects(substring):
    found = [o for o in bpy.data.objects if o.type == 'MESH' and substring.lower() in o.name.lower()]
    if not found:
        raise SystemExit('error: no mesh with "%s" in the name' % substring)
    for o in found:
        # Сначала отцепить от родителя (у FBX — пустышка с масштабом 0,01 и поворотом): иначе после удаления пустышки её
        # обратная матрица остаётся у объекта, и узел glTF выходит с масштабом 100
        world = o.matrix_world.copy()
        o.parent = None
        o.data.transform(world)
        o.matrix_world = mathutils.Matrix.Identity(4)
    if len(found) > 1:
        for o in bpy.context.selected_objects:
            o.select_set(False)
        for o in found:
            o.select_set(True)
        bpy.context.view_layer.objects.active = found[0]
        bpy.ops.object.join()
    return found[0]


def islands(mesh):
    """Номер острова (связной части сетки) вершины"""
    parent = np.arange(len(mesh.vertices))

    def find(x):
        root = x
        while parent[root] != root:
            root = parent[root]
        while parent[x] != root:
            parent[x], x = root, parent[x]
        return root
    for e in mesh.edges:
        a, b = find(e.vertices[0]), find(e.vertices[1])
        if a != b:
            parent[a] = b
    roots = np.array([find(i) for i in range(len(mesh.vertices))])
    _, index = np.unique(roots, return_inverse=True)
    return index


def kdtree(points):
    tree = KDTree(len(points))
    for i, p in enumerate(points):
        tree.insert(p, i)
    tree.balance()
    return tree


def wood_hierarchy(co, island, trunk_min_radius):
    """Родитель и точка крепления каждого острова древесины; путь ствола. Острова цепляются от корня к ближайшему
    прицепленному с растущим допуском"""
    count = island.max() + 1
    members = [np.flatnonzero(island == i) for i in range(count)]
    root = int(island[np.argmin(co[:, 2])])
    parent = np.full(count, -1)
    attach = np.zeros((count, 3))
    attach[root] = co[members[root]].mean(axis=0) * np.array([1.0, 1.0, 0.0]) + np.array([0.0, 0.0, co[:, 2].min()])
    assigned = {root}
    for tolerance in (0.01, 0.03, 0.08, 0.2, 0.5, 1e9):
        while True:
            vertices = np.concatenate([members[i] for i in assigned])
            tree = kdtree(co[vertices])
            added = []
            for i in range(count):
                if i in assigned:
                    continue
                best = (1e18, -1, -1)
                for v in members[i]:
                    _, index, distance = tree.find(co[v])
                    if distance < best[0]:
                        best = (distance, v, vertices[index])
                if best[0] <= tolerance:
                    parent[i] = island[best[2]]
                    attach[i] = co[best[2]]
                    added.append(i)
            if not added:
                break
            assigned.update(added)
        if len(assigned) == count:
            break
    # Толщина острова — среднее расстояние вершин до его оси (главная компонента)
    radius = np.zeros(count)
    for i in range(count):
        pts = co[members[i]]
        centered = pts - pts.mean(axis=0)
        if len(pts) < 3:
            continue
        axis = np.linalg.svd(centered, full_matrices=False)[2][0]
        radius[i] = np.mean(np.linalg.norm(centered - np.outer(centered @ axis, axis), axis=1))
    children = [[] for _ in range(count)]
    for i in range(count):
        if parent[i] >= 0:
            children[parent[i]].append(i)
    trunk = [root]
    while True:
        thick = [c for c in children[trunk[-1]] if radius[c] >= trunk_min_radius]
        if not thick:
            break
        trunk.append(max(thick, key=lambda c: radius[c]))
    return parent, attach, radius, children, trunk


def build_wind(co, island, parent, attach, children, trunk, height):
    """10 чисел на вершину древесины: начало ветви (3) и вес, начало веточки (3) и вес, доля высоты, рябь"""
    count = island.max() + 1
    on_trunk = np.zeros(count, bool)
    on_trunk[trunk] = True
    group = np.full(count, -1)   # ветвь первого уровня острова (−1 — ствол)
    stack = [c for t in trunk for c in children[t] if not on_trunk[c]]
    for first in stack:
        todo = [first]
        while todo:
            i = todo.pop()
            group[i] = first
            todo.extend(children[i])
    wind = np.zeros((len(co), 10), np.float32)
    wind[:, 8] = np.clip(co[:, 2] / height, 0.0, 1.0)
    vertex_group = group[island]
    reach = {}
    for g in np.unique(vertex_group):
        if g < 0:
            continue
        mask = vertex_group == g
        distance = np.linalg.norm(co[mask] - attach[g], axis=1)
        reach[g] = max(float(distance.max()), 1e-3)
        wind[mask, 0:3] = attach[g]
        wind[mask, 3] = distance / reach[g]
    return wind, group, reach


def leaf_cards(mesh):
    """Карточки листвы — острова: номер карточки вершины"""
    return islands(mesh)


def leaf_wind(co, card, wood_co, wood_wind, group_of_wood_vertex, attach, reach, height):
    wind = np.zeros((len(co), 10), np.float32)
    wind[:, 8] = np.clip(co[:, 2] / height, 0.0, 1.0)
    tree = kdtree(wood_co)
    base_of = {}
    for c in range(card.max() + 1):
        verts = np.flatnonzero(card == c)
        best = (1e18, -1, -1)
        for v in verts:
            _, index, distance = tree.find(co[v])
            if distance < best[0]:
                best = (distance, v, index)
        base = co[best[1]]
        length = max(float(np.linalg.norm(co[verts] - base, axis=1).max()), 1e-3)
        g = group_of_wood_vertex[best[2]]
        for v in verts:
            if g >= 0:
                wind[v, 0:3] = attach[g]
                wind[v, 3] = min(float(np.linalg.norm(co[v] - attach[g])) / reach[g], 1.0)
            along = float(np.linalg.norm(co[v] - base)) / length
            wind[v, 4:7] = base
            wind[v, 7] = along
            wind[v, 9] = along
        base_of[c] = base
    return wind, base_of


def image_of(material, socket_name, via_normal_map=False):
    """Картинка, подключённая к входу Principled BSDF (или через Normal Map)"""
    nodes = material.node_tree.nodes
    bsdf = next(n for n in nodes if n.type == 'BSDF_PRINCIPLED')
    links = bsdf.inputs[socket_name].links
    if not links:
        return None
    node = links[0].from_node
    if via_normal_map and node.type == 'NORMAL_MAP':
        links = node.inputs['Color'].links
        node = links[0].from_node if links else None
    while node is not None and node.type != 'TEX_IMAGE':
        inputs = [i for i in node.inputs if i.links]
        node = inputs[0].links[0].from_node if inputs else None
    return node.image if node is not None else None


def pixels(image, size):
    copy = image.copy()
    copy.colorspace_settings.name = 'Non-Color'
    if tuple(copy.size) != (size, size):
        copy.scale(size, size)
    data = np.empty(size * size * 4, np.float32)
    copy.pixels.foreach_get(data)
    bpy.data.images.remove(copy)
    return data.reshape(size, size, 4)


def make_material(name, source, folder, size, leaves, roughness_default, params, transmission):
    color = image_of(source, 'Base Color')
    if color is None:
        raise SystemExit('error: material %s has no base color texture' % source.name)
    base = pixels(color, size)
    alpha = image_of(source, 'Alpha') if leaves else None
    base[..., 3] = pixels(alpha, size)[..., 0] if alpha is not None else 1.0
    rough_image = image_of(source, 'Roughness')
    rough = pixels(rough_image, size)[..., 0] if rough_image is not None else np.full((size, size), roughness_default, np.float32)
    mr = np.stack([np.ones_like(rough), rough, np.zeros_like(rough), np.ones_like(rough)], axis=-1)
    normal_image = image_of(source, 'Normal', True)

    mat = bpy.data.materials.new(name)
    mat.use_backface_culling = not leaves
    nodes, links = mat.node_tree.nodes, mat.node_tree.links
    bsdf = nodes['Principled BSDF']
    tex = nodes.new('ShaderNodeTexImage')
    tex.image = save_png(name + '_BaseColor', base, folder, True)
    links.new(tex.outputs['Color'], bsdf.inputs['Base Color'])
    if alpha is not None:
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
    if normal_image is not None:
        normal = pixels(normal_image, size)
        normal[..., 3] = 1.0
        n_tex = nodes.new('ShaderNodeTexImage')
        n_tex.image = save_png(name + '_Normal', normal, folder, False)
        n_map = nodes.new('ShaderNodeNormalMap')
        links.new(n_tex.outputs['Color'], n_map.inputs['Color'])
        links.new(n_map.outputs['Normal'], bsdf.inputs['Normal'])
    mat['DitheredLODTransition'] = 'true'
    mat['WindWeight'] = 1.0
    for key, value in params:
        mat[key] = value
    if leaves:
        mat['DiffuseTransmissionFactor'] = transmission
        mat['DiffuseTransmissionColorFactor'] = [1.0, 1.0, 0.6]
        mat['DiffuseTransmissionColor'] = 'BaseColor'
        mat['CrownNormalBlend'] = 0.5
    print('material %s: %s %dx%d%s%s%s' % (name, color.name, size, size, ', alpha' if alpha is not None else '',
                                         ', roughness map' if rough_image is not None else ', roughness %.2f' % roughness_default,
                                         ', normal' if normal_image is not None else ''))
    return mat


def set_wind(mesh, wind):
    if len(mesh.vertices) != len(wind):
        raise SystemExit('error: %s: %d vertices, %d wind rows' % (mesh.name, len(mesh.vertices), len(wind)))
    for attribute, first in (('_wind_branch1', 0), ('_wind_branch2', 4)):
        data = np.column_stack([wind[:, first], wind[:, first + 2], -wind[:, first + 1], wind[:, first + 3]])
        mesh.attributes.new(attribute, 'FLOAT_COLOR', 'POINT').data.foreach_set('color', data.ravel())
    mesh.attributes.new('_wind_weights', 'FLOAT2', 'POINT').data.foreach_set('vector', wind[:, 8:10].ravel())


def coordinates(mesh):
    co = np.empty(len(mesh.vertices) * 3, np.float32)
    mesh.vertices.foreach_get('co', co)
    return co.reshape(-1, 3).astype(np.float64)


def main():
    argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    parser = argparse.ArgumentParser(prog='tree_from_mesh.py')
    parser.add_argument('--fbx', required=True)
    parser.add_argument('--name', required=True, help='model name: <name>, <name>_LOD1 …')
    parser.add_argument('--out', required=True, help='output .glb')
    parser.add_argument('--wood', required=True, help='substring of the wood mesh names (trunk and branches)')
    parser.add_argument('--leaves', required=True, help='substring of the leaf card mesh names')
    parser.add_argument('--wood-ratio', default='1,0.3,0.08', help='Decimate share of the wood triangles per LOD')
    parser.add_argument('--wood-drop', default='0,0.012,0.025', help='m: wood pieces thinner than this are dropped per LOD')
    parser.add_argument('--leaf-keep', default='1,0.5,0.18', help='share of the leaf cards per LOD')
    parser.add_argument('--leaf-scale', default='1,1.35,2.0', help='leaf card scale per LOD (around its base)')
    parser.add_argument('--trunk-min-radius', type=float, default=0.06, help='m: thinner pieces are branches, not the trunk')
    parser.add_argument('--texture-size', type=int, default=2048)
    parser.add_argument('--leaf-roughness', type=float, default=0.6, help='leaf roughness without a roughness map')
    parser.add_argument('--transmission', type=float, default=0.45, help='leaf DiffuseTransmissionFactor')
    parser.add_argument('--material-param', action='append', default=[], metavar='NAME=VALUE',
                        help='engine material parameter for both materials (glTF extras), e.g. WindBranchAngle=3')
    args = parser.parse_args(argv)
    wood_ratio = parse_list(args.wood_ratio, 3)
    wood_drop = parse_list(args.wood_drop, 3)
    leaf_keep = parse_list(args.leaf_keep, 3)
    leaf_scale = parse_list(args.leaf_scale, 3)
    params = [material_param(text) for text in args.material_param]

    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=os.path.abspath(args.fbx))
    wood = join_objects(args.wood)
    leaves = join_objects(args.leaves)
    folder = tempfile.mkdtemp(prefix='dm_tree_')
    bark_mat = make_material(args.name + 'Bark', wood.data.materials[0], folder, args.texture_size, False, 0.8, params, 0.0)
    leaf_mat = make_material(args.name + 'Leaves', leaves.data.materials[0], folder, args.texture_size, True,
                             args.leaf_roughness, params, args.transmission)
    for obj in list(bpy.data.objects):
        if obj not in (wood, leaves):
            bpy.data.objects.remove(obj, do_unlink=True)

    # Начало координат — середина основания ствола
    wood_co = coordinates(wood.data)
    low = wood_co[:, 2].min()
    base = wood_co[wood_co[:, 2] < low + 0.3]
    shift = mathutils.Vector((-(base[:, 0].min() + base[:, 0].max()) / 2.0, -(base[:, 1].min() + base[:, 1].max()) / 2.0, -low))
    for obj in (wood, leaves):
        obj.data.transform(mathutils.Matrix.Translation(shift))
    wood_co = coordinates(wood.data)
    leaf_co = coordinates(leaves.data)
    height = max(wood_co[:, 2].max(), leaf_co[:, 2].max())
    trunk_radius = float(np.median(np.hypot(base[:, 0] + shift.x, base[:, 1] + shift.y)))

    wood_island = islands(wood.data)
    parent, attach, radius, children, trunk = wood_hierarchy(wood_co, wood_island, args.trunk_min_radius)
    print('wood piece radius, m: 25%% %.3f, median %.3f, 75%% %.3f' % tuple(np.percentile(radius, [25, 50, 75])))
    wood_wind, group, reach = build_wind(wood_co, wood_island, parent, attach, children, trunk, height)
    groups = len(reach)
    print('wood: %d pieces, trunk %d pieces up to %.1f m, %d first-level branches, trunk radius at the ground %.2f m' % (
        wood_island.max() + 1, len(trunk), max(attach[t][2] for t in trunk), groups, trunk_radius))
    card = leaf_cards(leaves.data)
    leaf_wind_data, card_base = leaf_wind(leaf_co, card, wood_co, wood_wind, group[wood_island], attach, reach, height)
    print('leaves: %d cards, height %.1f m' % (card.max() + 1, height))

    lods = []
    for lod in range(3):
        # Древесина
        w = wood.copy()
        w.data = wood.data.copy()
        bpy.context.scene.collection.objects.link(w)
        thin = radius[wood_island] < wood_drop[lod]
        if thin.any():
            import bmesh
            bm = bmesh.new()
            bm.from_mesh(w.data)
            bm.verts.ensure_lookup_table()
            bmesh.ops.delete(bm, geom=[bm.verts[i] for i in np.flatnonzero(thin)], context='VERTS')
            bm.to_mesh(w.data)
            bm.free()
        if wood_ratio[lod] < 1.0:
            modifier = w.modifiers.new('decimate', 'DECIMATE')
            modifier.decimate_type = 'COLLAPSE'
            modifier.ratio = wood_ratio[lod]
            bpy.context.view_layer.objects.active = w
            bpy.ops.object.modifier_apply(modifier=modifier.name)
        lod_co = coordinates(w.data)
        if lod == 0:
            w_wind = wood_wind
        else:
            tree = kdtree(wood_co)
            w_wind = np.array([wood_wind[tree.find(p)[1]] for p in lod_co], np.float32)
        set_wind(w.data, w_wind)
        # Листва: доля карточек (по номеру — одни и те же во всех LOD), крупнее вокруг основания
        l = leaves.copy()
        l.data = leaves.data.copy()
        bpy.context.scene.collection.objects.link(l)
        cards = card.max() + 1
        keep = (np.arange(cards) * 0.61803398875) % 1.0 < leaf_keep[lod]
        vertex_keep = keep[card]
        new_co = leaf_co.copy()
        for c in np.flatnonzero(keep):
            verts = card == c
            new_co[verts] = card_base[c] + (leaf_co[verts] - card_base[c]) * leaf_scale[lod]
        l.data.vertices.foreach_set('co', new_co.astype(np.float32).ravel())
        if not vertex_keep.all():
            import bmesh
            bm = bmesh.new()
            bm.from_mesh(l.data)
            bm.verts.ensure_lookup_table()
            bmesh.ops.delete(bm, geom=[bm.verts[i] for i in np.flatnonzero(~vertex_keep)], context='VERTS')
            bm.to_mesh(l.data)
            bm.free()
        set_wind(l.data, leaf_wind_data[vertex_keep])
        # Один объект на LOD: секция коры и секция листвы. Развёртки — под одним именем: при объединении слои с разными
        # именами расходятся, и материал листвы читал бы развёртку коры
        for data in (w.data, l.data):
            for layer in list(data.uv_layers)[1:]:
                data.uv_layers.remove(layer)
            data.uv_layers[0].name = 'UVMap'
        w.data.materials.clear()
        w.data.materials.append(bark_mat)
        l.data.materials.clear()
        l.data.materials.append(leaf_mat)
        for o in bpy.context.selected_objects:
            o.select_set(False)
        w.select_set(True)
        l.select_set(True)
        bpy.context.view_layer.objects.active = w
        bpy.ops.object.join()
        w.name = args.name if lod == 0 else '%s_LOD%d' % (args.name, lod)
        w.data.name = w.name
        w.data.calc_loop_triangles()
        print('%s: %d triangles (wood: thinner than %.3f m dropped, %.2f; leaf cards %.2f x %.2f)' % (
            w.name, len(w.data.loop_triangles), wood_drop[lod], wood_ratio[lod], leaf_keep[lod], leaf_scale[lod]))
        lods.append(w)
    lods[0]['collision'] = 'capsule'
    lods[0]['collision_radius'] = trunk_radius
    for obj in (wood, leaves):
        bpy.data.objects.remove(obj, do_unlink=True)

    out = os.path.abspath(args.out)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    bpy.ops.export_scene.gltf(filepath=out, export_format='GLB', export_yup=True, export_apply=True,
                              export_tangents=True, export_image_format='AUTO', export_materials='EXPORT',
                              export_extras=True, export_attributes=True, export_vertex_color='NONE',
                              export_animations=False)
    print('Written: ' + out)


if __name__ == '__main__':
    main()
