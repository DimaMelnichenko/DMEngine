# Растения Megascans с Fab (архив .zip формата FBX: <Ассет>_<Вар>_LOD<N>.fbx и карты JPG) → .glb для
# Tools/import_gltf.py --scatter. Сценарий:
#   - распаковывает архив во временную папку и импортирует FBX вариантов (--variants — регулярное выражение по VarA…);
#   - называет объекты <имя>_<Вар>_LOD<N> (имя — --name): вариант — модель, LOD — суффикс _LOD<N> (его понимает
#     импортёр), --lod-offset k отбрасывает LOD ниже k и нумерует оставшиеся с нуля;
#   - собирает два материала простым Principled BSDF: основной — карты <Ассет>_<размер>_BaseColor / Opacity / Roughness
#     / Normal, и billboard — карты _Billboard_* — у последнего LOD (карточка вдали, --billboard-lod, по умолчанию
#     последний); базовый цвет с альфой из Opacity (Round — alphaMode MASK), шероховатость в G картинки
#     metallicRoughness, нормаль — в соглашении OpenGL, как у glTF (карта DirectX переворачивается по G: соглашение
#     определяется по интегрируемости поля нормалей или задаётся --normal-convention); картинки — до --texture-size;
#     двусторонние; карточка billboard — крестом из двух (--no-billboard-cross — одна);
#   - просвет и отражение — расширения glTF, которые экспортёр Blender не пишет, сценарий дописывает их в .glb сам:
#     KHR_materials_diffuse_transmission — доля --diffuse-transmission t (0,5) и цвет из карты Translucency (как Subsurface
#     Color у Two Sided Foliage в UE), KHR_materials_specular — specularFactor из карты Specular (Specular UE s → F0 =
#     0,08 s, множитель к 0,04 — 2 s; среднее по непрозрачной части; у billboard своей карты нет — значение основного);
#   - яркость — как у Two Sided Foliage в UE: альбедо скана — уже доля отражённого света, а просвет — карта Translucency
#     целиком; в glTF базовый цвет делится — отражается (1 − t), проходит t × цвет просвета. Поэтому картинки BaseColor
#     и Translucency масштабируются в линейном пространстве на 1 / (1 − t) и 1 / t (при t = 0,5 — × 2; что выше 1 —
#     обрезается, доля печатается), иначе лицевая сторона темнее скана на t, а просвет — ещё и × t;
#   - параметры движка, которых нет в glTF, — --material-param ИМЯ=ЗНАЧЕНИЕ (повторяется) обоим материалам: extras glTF;
#   - печатает габариты вариантов (м, ось Z Blender вверх) и треугольники LOD, экспортирует .glb без цветов вершин
#     (движок их не читает).
# Запуск из корня проекта:
#   blender -b --factory-startup --python Tools/export_megascans.py -- --archive DownloadResources/tall_grass_rbeki_high.zip
#       --name tall_grass --out Meshes/source/tall_grass.glb [--texture-size 2048] [--material-param WindWeight=1]
import argparse
import json
import os
import struct
import re
import sys
import tempfile
import zipfile

import bmesh
import bpy
import mathutils
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from export_polyhaven import material_param, save_png, triangle_count  # noqa: E402

FBX_PATTERN = re.compile(r'_(Var[A-Za-z0-9]+)_LOD(\d+)\.fbx$', re.I)


def find_map(folder, suffix):
    """Карта по окончанию имени: _4K_BaseColor.jpg, _4K_Billboard_Normal.jpg; основная не берёт billboard"""
    found = []
    for root, _, files in os.walk(folder):
        for name in files:
            stem, ext = os.path.splitext(name)
            if ext.lower() not in ('.jpg', '.jpeg', '.png', '.exr', '.tif', '.tiff'):
                continue
            if not stem.lower().endswith('_' + suffix.lower()):
                continue
            if 'billboard' not in suffix.lower() and '_billboard_' in stem.lower():
                continue
            found.append(os.path.join(root, name))
    return sorted(found)[0] if found else None


def load_pixels(path, size):
    """Значения картинки как в файле, высота × ширина × 4 в порядке строк Blender (снизу вверх)"""
    image = bpy.data.images.load(path, check_existing=False)
    image.colorspace_settings.name = 'Non-Color'
    if size and tuple(image.size) != (size, size):
        image.scale(size, size)
    width, height = image.size
    data = np.empty(width * height * 4, dtype=np.float32)
    image.pixels.foreach_get(data)
    bpy.data.images.remove(image)
    return data.reshape(height, width, 4)


def normal_convention(folder):
    """OpenGL (G — вверх по картинке) или DirectX (G — вниз) — по интегрируемости поля нормалей: наклоны карты
    p = nx / nz, q = ny / nz (y вверх по картинке — соглашение OpenGL) — производные одной поверхности, поэтому их ротор
    ∂p/∂y − ∂q/∂x мал; у карты DirectX, прочитанной как OpenGL, q с обратным знаком, и мал уже ∂p/∂y + ∂q/∂x. Считается
    на непрозрачной части (Opacity > 0,5). Разница меньше полуторакратной — None (решить нельзя)"""
    normal_path = find_map(folder, 'Normal')
    if not normal_path:
        return None
    normal = load_pixels(normal_path, 1024)
    opacity_path = find_map(folder, 'Opacity')
    mask = load_pixels(opacity_path, 1024)[..., 0] > 0.5 if opacity_path else np.ones(normal.shape[:2], bool)
    nz = np.maximum(normal[..., 2] * 2.0 - 1.0, 0.2)
    p = ( normal[..., 0] * 2.0 - 1.0 ) / nz
    q = ( normal[..., 1] * 2.0 - 1.0 ) / nz
    # Строки Blender — снизу вверх: ось 0 — вверх по картинке; края маски (силуэт) не считаются
    dp_dy = np.gradient(p, axis=0)
    dq_dx = np.gradient(q, axis=1)
    inner = mask & np.roll(mask, 1, 0) & np.roll(mask, -1, 0) & np.roll(mask, 1, 1) & np.roll(mask, -1, 1)
    curl_gl = float(np.mean(np.abs(dp_dy - dq_dx)[inner]))
    curl_dx = float(np.mean(np.abs(dp_dy + dq_dx)[inner]))
    print('normal map check: curl as OpenGL %.4f, as DirectX %.4f (%d texels)' % (curl_gl, curl_dx, int(inner.sum())))
    if max(curl_gl, curl_dx) < 1.5 * min(curl_gl, curl_dx):
        return None
    return 'gl' if curl_gl < curl_dx else 'dx'


def scale_linear(rgba, scale, label):
    """Цвет sRGB картинки × scale в линейном пространстве (альфа не меняется); печатает долю обрезанных текселей"""
    rgb = rgba[..., :3]
    linear = np.where(rgb <= 0.04045, rgb / 12.92, ((rgb + 0.055) / 1.055) ** 2.4) * scale
    clipped = float(np.mean(np.any(linear > 1.0, axis=-1)[rgba[..., 3] > 0.5]))
    linear = np.clip(linear, 0.0, 1.0)
    rgba[..., :3] = np.where(linear <= 0.0031308, linear * 12.92, 1.055 * linear ** (1.0 / 2.4) - 0.055)
    print('%s: x%.2f in linear, clipped %.2f%% of opaque texels' % (label, scale, 100.0 * clipped))


def build_material(name, folder, prefix, size, convention, params, transmission):
    """Материал по картам с префиксом ('' — основные, 'Billboard_' — карточки); картинки — по префиксу и карте
    (BaseColor, Billboard_Normal): импортёр называет текстуры <ассет>_<картинка>"""
    base_path = find_map(folder, prefix + 'BaseColor')
    if not base_path:
        raise SystemExit('error: no %sBaseColor map in the archive' % prefix)
    base = load_pixels(base_path, size)
    opacity_path = find_map(folder, prefix + 'Opacity')
    base[..., 3] = load_pixels(opacity_path, size)[..., 0] if opacity_path else 1.0
    # Альбедо скана — отражённая доля; в glTF отражается baseColor × (1 − t)
    translucency_path = find_map(folder, prefix + 'Translucency')
    if translucency_path and transmission < 1.0:
        scale_linear(base, 1.0 / (1.0 - transmission), prefix + 'BaseColor')
    rough_path = find_map(folder, prefix + 'Roughness')
    rough = load_pixels(rough_path, size)[..., 0] if rough_path else np.full(base.shape[:2], 0.8, np.float32)
    mr = np.stack([np.ones_like(rough), rough, np.zeros_like(rough), np.ones_like(rough)], axis=-1)
    # Просвет и отражение — для расширений glTF, которые сценарий допишет в .glb (add_extensions)
    extras = {'translucency': None, 'specular': None}
    if translucency_path:
        translucency = load_pixels(translucency_path, size)
        translucency[..., 3] = base[..., 3]
        if transmission > 0.0:
            scale_linear(translucency, 1.0 / transmission, prefix + 'Translucency')
        translucency[..., 3] = 1.0
        save_png(prefix + 'Translucency', translucency, folder, True)
        extras['translucency'] = os.path.join(folder, prefix + 'Translucency.png')
    specular_path = find_map(folder, prefix + 'Specular')
    if specular_path:
        opaque = base[..., 3] > 0.5
        specular = load_pixels(specular_path, size)[..., 0]
        extras['specular'] = 2.0 * float(np.mean(specular[opaque] if opaque.any() else specular))

    mat = bpy.data.materials.new(name)
    if bpy.app.version < (5, 0, 0):
        mat.use_nodes = True
    mat.use_backface_culling = False
    nodes, links = mat.node_tree.nodes, mat.node_tree.links
    bsdf = nodes['Principled BSDF']
    tex = nodes.new('ShaderNodeTexImage')
    tex.image = save_png(prefix + 'BaseColor', base, folder, True)
    links.new(tex.outputs['Color'], bsdf.inputs['Base Color'])
    if opacity_path:
        clip = nodes.new('ShaderNodeMath')
        clip.operation = 'ROUND'
        links.new(tex.outputs['Alpha'], clip.inputs[0])
        links.new(clip.outputs[0], bsdf.inputs['Alpha'])
    mr_tex = nodes.new('ShaderNodeTexImage')
    mr_tex.image = save_png(prefix + 'MetallicRoughness', mr, folder, False)
    split = nodes.new('ShaderNodeSeparateColor')
    links.new(mr_tex.outputs['Color'], split.inputs['Color'])
    links.new(split.outputs['Green'], bsdf.inputs['Roughness'])
    links.new(split.outputs['Blue'], bsdf.inputs['Metallic'])

    normal_path = find_map(folder, prefix + 'Normal')
    if normal_path:
        normal = load_pixels(normal_path, size)
        if convention == 'dx':
            normal[..., 1] = 1.0 - normal[..., 1]
        normal[..., 3] = 1.0
        n_tex = nodes.new('ShaderNodeTexImage')
        n_tex.image = save_png(prefix + 'Normal', normal, folder, False)
        n_map = nodes.new('ShaderNodeNormalMap')
        links.new(n_tex.outputs['Color'], n_map.inputs['Color'])
        links.new(n_map.outputs['Normal'], bsdf.inputs['Normal'])
    for key, value in params:
        mat[key] = value
    print('material %s: %s, %dx%d%s%s%s%s' % (name, os.path.basename(base_path), base.shape[1], base.shape[0],
                                             ', alpha' if opacity_path else '', ', normal ' + (convention or '?') if normal_path else '',
                                             ', translucency' if translucency_path else '',
                                             ', specularFactor %.3f' % extras['specular'] if specular_path else ''))
    return mat, extras


def add_extensions(path, materials, transmission):
    """Дописывает в .glb расширения материалов, которых экспортёр Blender не пишет: materials — имя материала →
    {'translucency': PNG или None, 'specular': specularFactor или None}. Картинка — новый bufferView в конце BIN"""
    with open(path, 'rb') as f:
        data = f.read()
    magic, version, _ = struct.unpack_from('<4sII', data, 0)
    json_length, json_type = struct.unpack_from('<I4s', data, 12)
    if magic != b'glTF' or version != 2 or json_type != b'JSON':
        raise SystemExit('error: %s is not a glTF 2.0 binary' % path)
    gltf = json.loads(data[20:20 + json_length].decode('utf-8'))
    offset = 20 + json_length
    binary = bytearray()
    if offset < len(data):
        bin_length, bin_type = struct.unpack_from('<I4s', data, offset)
        if bin_type != b'BIN\x00':
            raise SystemExit('error: %s: second chunk is not BIN' % path)
        binary = bytearray(data[offset + 8:offset + 8 + bin_length])
    used = gltf.setdefault('extensionsUsed', [])
    for material in gltf.get('materials', []):
        info = materials.get(material.get('name'))
        if not info:
            continue
        extensions = material.setdefault('extensions', {})
        diffuse = {'diffuseTransmissionFactor': transmission}
        if info['translucency']:
            with open(info['translucency'], 'rb') as f:
                png = f.read()
            binary.extend(b'\x00' * (-len(binary) % 4))
            gltf.setdefault('bufferViews', []).append({'buffer': 0, 'byteOffset': len(binary), 'byteLength': len(png)})
            binary.extend(png)
            gltf.setdefault('images', []).append({'name': os.path.splitext(os.path.basename(info['translucency']))[0],
                                                  'mimeType': 'image/png', 'bufferView': len(gltf['bufferViews']) - 1})
            texture = {'source': len(gltf['images']) - 1}
            base = material.get('pbrMetallicRoughness', {}).get('baseColorTexture')
            if base is not None and 'sampler' in gltf['textures'][base['index']]:
                texture['sampler'] = gltf['textures'][base['index']]['sampler']
            gltf.setdefault('textures', []).append(texture)
            diffuse['diffuseTransmissionColorTexture'] = {'index': len(gltf['textures']) - 1}
        extensions['KHR_materials_diffuse_transmission'] = diffuse
        if info['specular'] is not None:
            extensions['KHR_materials_specular'] = {'specularFactor': round(info['specular'], 4)}
        for name in extensions:
            if name not in used:
                used.append(name)
        print('material %s: %s' % (material.get('name'), ', '.join(sorted(extensions))))
    binary.extend(b'\x00' * (-len(binary) % 4))
    gltf.setdefault('buffers', [{}])[0]['byteLength'] = len(binary)
    text = json.dumps(gltf, separators=(',', ':')).encode('utf-8')
    text += b' ' * (-len(text) % 4)
    out = struct.pack('<4sII', b'glTF', 2, 12 + 8 + len(text) + 8 + len(binary))
    out += struct.pack('<I4s', len(text), b'JSON') + text + struct.pack('<I4s', len(binary), b'BIN\x00') + bytes(binary)
    with open(path, 'wb') as f:
        f.write(out)


def main():
    argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    parser = argparse.ArgumentParser(prog='export_megascans.py')
    parser.add_argument('--archive', required=True, help='Fab / Megascans .zip with FBX and maps')
    parser.add_argument('--name', required=True, help='model name prefix: <name>_VarA …')
    parser.add_argument('--out', required=True, help='output .glb')
    parser.add_argument('--variants', default=r'.', help='regular expression for variants (VarA, VarB …)')
    parser.add_argument('--texture-size', type=int, default=2048)
    parser.add_argument('--lod-offset', type=int, default=0, help='drop LODs below this one and renumber from 0')
    parser.add_argument('--billboard-lod', type=int, help='LOD with the billboard maps (after --lod-offset); default — the last')
    parser.add_argument('--normal-convention', choices=('gl', 'dx'), help='normal map convention; default — detected from the normal map')
    parser.add_argument('--no-billboard-cross', action='store_true', help='keep the billboard LOD a single card')
    parser.add_argument('--diffuse-transmission', type=float, default=0.5,
                        help='share of diffuse light passing through (KHR_materials_diffuse_transmission); color - Translucency map')
    parser.add_argument('--material-param', action='append', default=[], metavar='NAME=VALUE',
                        help='engine material parameter for both materials (glTF extras)')
    args = parser.parse_args(argv)

    folder = tempfile.mkdtemp(prefix='dm_megascans_')
    with zipfile.ZipFile(args.archive) as zf:
        zf.extractall(folder)
    bpy.ops.wm.read_factory_settings(use_empty=True)

    variants = re.compile(args.variants, re.I)
    files = []
    for root, _, names in os.walk(folder):
        for file in names:
            match = FBX_PATTERN.search(file)
            if match and variants.search(match.group(1)):
                files.append((match.group(1), int(match.group(2)), os.path.join(root, file)))
    if not files:
        raise SystemExit('error: no <Var>_LOD<N>.fbx in %s' % args.archive)

    objects = {}   # (вариант, LOD) → объект
    for variant, lod, path in sorted(files):
        if lod < args.lod_offset:
            continue
        before = set(bpy.data.objects)
        bpy.ops.import_scene.fbx(filepath=path)
        meshes = [o for o in set(bpy.data.objects) - before if o.type == 'MESH']
        for extra in set(bpy.data.objects) - before:
            if extra.type != 'MESH':
                bpy.data.objects.remove(extra, do_unlink=True)
        if len(meshes) != 1:
            raise SystemExit('error: %s has %d meshes, expected one' % (os.path.basename(path), len(meshes)))
        obj = meshes[0]
        obj.name = '%s_%s' % (args.name, variant) if lod == args.lod_offset else '%s_%s_LOD%d' % (args.name, variant, lod - args.lod_offset)
        objects[( variant, lod - args.lod_offset )] = obj

    lod_count = max(lod for _, lod in objects) + 1
    billboard_lod = lod_count - 1 if args.billboard_lod is None else args.billboard_lod
    convention = args.normal_convention or normal_convention(folder)
    if convention is None:
        raise SystemExit('error: normal map convention is not detected, pass --normal-convention gl|dx')
    params = [material_param(text) for text in args.material_param]
    main_mat, main_extras = build_material(args.name, folder, '', args.texture_size, convention, params,
                                           args.diffuse_transmission)
    material_extras = {main_mat.name: main_extras}
    billboard_mat = None
    if find_map(folder, 'Billboard_BaseColor'):
        billboard_mat, billboard_extras = build_material(args.name + '_billboard', folder, 'Billboard_', args.texture_size,
                                                         convention, params, args.diffuse_transmission)
        if billboard_extras['specular'] is None:
            billboard_extras['specular'] = main_extras['specular']
        material_extras[billboard_mat.name] = billboard_extras

    for (variant, lod), obj in sorted(objects.items()):
        mat = billboard_mat if billboard_mat and lod == billboard_lod else main_mat
        obj.data.materials.clear()
        obj.data.materials.append(mat)
        # Origin — у основания: расстановка ставит модель им на землю; положение и поворот FBX — в вершины
        bpy.context.view_layer.objects.active = obj
        for other in bpy.context.selected_objects:
            other.select_set(False)
        obj.select_set(True)
        bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
        obj.location = (0.0, 0.0, 0.0)
        if mat is billboard_mat and not args.no_billboard_cross:
            # Billboard Megascans — одна плоская карточка: в UE её поворачивает к камере материал, у нас такого нет, и с
            # ребра она пропала бы. Вторая карточка — та же, повёрнутая на 90° вокруг вертикали (крест)
            mesh = obj.data
            count = len(mesh.vertices)
            bm = bmesh.new()
            bm.from_mesh(mesh)
            copy = bmesh.ops.duplicate(bm, geom=bm.verts[:] + bm.edges[:] + bm.faces[:])
            verts = [e for e in copy['geom'] if isinstance(e, bmesh.types.BMVert)]
            bmesh.ops.rotate(bm, verts=verts, cent=(0.0, 0.0, 0.0), matrix=mathutils.Matrix.Rotation(np.pi / 2.0, 3, 'Z'))
            bm.to_mesh(mesh)
            bm.free()
            print('object %s: billboard crossed (%d -> %d vertices)' % (obj.name, count, len(mesh.vertices)))
        corners = np.array([v.co for v in obj.data.vertices])
        size = corners.max(axis=0) - corners.min(axis=0)
        print('object %s: %d triangles, size %.2f x %.2f x %.2f m, bottom z %.3f, material %s' % (
            obj.name, triangle_count(obj), size[0], size[1], size[2], corners[:, 2].min(), mat.name))

    out = os.path.abspath(args.out)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    bpy.ops.export_scene.gltf(filepath=out, export_format='GLB', export_yup=True, export_apply=True,
                              export_tangents=True, export_image_format='AUTO', export_materials='EXPORT',
                              export_extras=True, export_animations=False, export_vertex_color='NONE')
    add_extensions(out, material_extras, args.diffuse_transmission)
    print('Written: ' + out)


if __name__ == '__main__':
    main()
