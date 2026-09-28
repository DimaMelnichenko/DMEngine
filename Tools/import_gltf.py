"""Импорт моделей glTF 2.0 (.glb, .gltf) в DMEngine — как импорт FBX/glTF в редакторе UE.

    python Tools/import_gltf.py модель.glb [--level Test --position x,y,z [--snap-to-terrain]] [--scale s]
                                           [--lod-ranges 25,60] [--name Имя] [--scatter] [--dry-run]

Пишет файлы мешей (Meshes\\models\\<ассет>\\<модель>_LOD<N>[_S<секция>].bin, формат MeshLoader), картинки текстур
(Textures\\models\\<ассет>\\) и строки base.db3: Meshes, Models, ModelProperties, Textures, MaterialInstance +
MaterialParameterInstance, по --level — LevelModels. Всё в одной транзакции; повторный импорт того же файла обновляет
строки и файлы (reimport), лишние LOD удаляет. Подробно — docs/models.md.

- Объект (узел с мешем) — модель; суффикс _LOD<N> в имени — LOD N модели с именем без суффикса.
- Связанные дубликаты Blender (Alt+D: объекты с общим мешем) — одна модель с несколькими экземплярами на уровне.
- Материалы объекта — секции LOD, как primitives меша glTF и sections Static Mesh в UE: секция — меш со своим
  материалом (примитивы одного материала сливаются). Номер секции — порядок появления материала в LOD0 и дальше;
  в LOD, где материала нет, этой секции нет.
- Опорная точка модели — origin объекта в Blender: вершины остаются в координатах объекта, а его положение, поворот
  и масштаб становятся экземпляром модели на уровне (LevelModels) относительно --position; зеркальный объект —
  экземпляр с отрицательным масштабом по X. Сдвиг осей от неравномерного масштаба родителя запекается в вершины;
  модели для расстановки (--scatter) — тоже, их ставят слои расстановки.
- glTF правосторонний, движок левосторонний (оба Y вверх, метры): Z с минусом, порядок вершин треугольника обратный.
- Материал glTF переносится в экземпляр материала PBR (id 12; с --scatter — PBRInstance, id 9) один в один;
  карты нормалей glTF в соглашении OpenGL — NormalGreenUp = true.
- У материала alphaMode MASK мипы базового цвета сохраняют покрытие альфы при его alphaCutoff
  (Textures.preserve_alpha_coverage): иначе вдали травинки и лепестки тают.
- Параметры движка без аналога в glTF (пропускание — экспортёр Blender не пишет KHR_materials_diffuse_transmission,
  DitheredLODTransition, WindWeight) — extras материала (Custom Properties материала Blender, экспорт с
  export_extras) с именами параметров PBR; значение-строка с именем текстуры материала (BaseColor) — та же текстура.
- --snap-to-terrain ставит экземпляры на землю уровня, как Drop to surface в UE: высота — террейн в точке (x, z)
  плюс высота объекта над нулём в Blender (Tools/terrain.py).

Сообщения скрипта — ASCII (правило Tools/).
"""
import argparse
import base64
import json
import os
import re
import sqlite3
import struct
import sys

import numpy as np

import terrain

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

PBR_MATERIAL = 12
PBR_INSTANCE_MATERIAL = 9
FALLBACK_PRIMITIVE = 'box'   # Meshes.primitive: заглушка, если файла меша нет (Meshes\ не хранится в git)
LAST_LOD_RANGE = 10000.0

COMPONENT_TYPES = {5120: np.int8, 5121: np.uint8, 5122: np.int16, 5123: np.uint16, 5125: np.uint32, 5126: np.float32}
TYPE_SIZES = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT2': 4, 'MAT3': 9, 'MAT4': 16}
TRIANGLES = 4
WRAP_REPEAT = 10497
MIRROR = np.array([1.0, 1.0, -1.0])   # glTF правый → движок левый: Z с минусом

warnings = []


def warn(text):
    warnings.append(text)
    print('warning: ' + text)


def sanitize(name):
    return re.sub(r'[^A-Za-z0-9_\-]', '_', name) or 'unnamed'


# ---------------------------------------------------------------------------------------------------------------------
# Чтение glTF

class Gltf:
    def __init__(self, path):
        self.folder = os.path.dirname(os.path.abspath(path))
        data = open(path, 'rb').read()
        binary = None
        if data[:4] == b'glTF':
            _, _, length = struct.unpack_from('<III', data, 0)
            offset = 12
            self.json = None
            while offset < length:
                chunk_length, chunk_type = struct.unpack_from('<II', data, offset)
                chunk = data[offset + 8:offset + 8 + chunk_length]
                if chunk_type == 0x4E4F534A:    # JSON
                    self.json = json.loads(chunk.decode('utf-8'))
                elif chunk_type == 0x004E4942:  # BIN
                    binary = chunk
                offset += 8 + chunk_length
        else:
            self.json = json.loads(data.decode('utf-8'))

        required = set(self.json.get('extensionsRequired', []))
        if required:
            raise SystemExit('error: required glTF extensions are not supported: %s. Export without compression '
                             '(Draco, meshopt) and without image formats other than PNG/JPEG' % ', '.join(sorted(required)))

        self.buffers = []
        for buffer in self.json.get('buffers', []):
            uri = buffer.get('uri')
            if uri is None:
                self.buffers.append(binary)
            else:
                self.buffers.append(self.read_uri(uri))

    def read_uri(self, uri):
        if uri.startswith('data:'):
            return base64.b64decode(uri.split(',', 1)[1])
        from urllib.parse import unquote
        return open(os.path.join(self.folder, unquote(uri)), 'rb').read()

    def buffer_view(self, index):
        view = self.json['bufferViews'][index]
        buffer = self.buffers[view['buffer']]
        start = view.get('byteOffset', 0)
        return buffer, start, view.get('byteStride'), view['byteLength']

    def accessor(self, index):
        accessor = self.json['accessors'][index]
        if 'sparse' in accessor:
            raise SystemExit('error: sparse accessors are not supported (accessor %d)' % index)
        dtype = np.dtype(COMPONENT_TYPES[accessor['componentType']]).newbyteorder('<')
        width = TYPE_SIZES[accessor['type']]
        count = accessor['count']
        if 'bufferView' not in accessor:
            values = np.zeros((count, width), dtype=dtype)
        else:
            buffer, start, stride, _ = self.buffer_view(accessor['bufferView'])
            start += accessor.get('byteOffset', 0)
            stride = stride or dtype.itemsize * width
            values = np.ndarray((count, width), dtype=dtype, buffer=buffer, offset=start,
                                strides=(stride, dtype.itemsize)).copy()
        if accessor.get('normalized'):
            limit = float(np.iinfo(dtype).max)
            values = np.maximum(values.astype(np.float64) / limit, -1.0)
        return values

    def image(self, index):
        image = self.json['images'][index]
        if 'uri' in image:
            uri = image['uri']
            data = self.read_uri(uri)
            mime = image.get('mimeType') or ('image/jpeg' if uri.lower().endswith(('.jpg', '.jpeg')) else 'image/png')
            if uri.startswith('data:'):
                mime = uri[5:].split(';', 1)[0]
        else:
            buffer, start, _, length = self.buffer_view(image['bufferView'])
            data = buffer[start:start + length]
            mime = image['mimeType']
        extension = {'image/png': 'png', 'image/jpeg': 'jpg'}.get(mime)
        if extension is None:
            raise SystemExit('error: image %d has unsupported type %s' % (index, mime))
        return data, extension


def node_matrix(node):
    if 'matrix' in node:
        return np.array(node['matrix'], dtype=np.float64).reshape(4, 4).T   # glTF хранит по столбцам
    translation = np.array(node.get('translation', [0.0, 0.0, 0.0]))
    x, y, z, w = node.get('rotation', [0.0, 0.0, 0.0, 1.0])
    scale = np.array(node.get('scale', [1.0, 1.0, 1.0]))
    rotation = np.array([
        [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
        [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
        [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])
    matrix = np.eye(4)
    matrix[:3, :3] = rotation * scale
    matrix[:3, 3] = translation
    return matrix


def quaternion(rotation):
    """Матрица поворота (по столбцам, как в glTF) → кватернион x, y, z, w с w >= 0."""
    m = rotation
    trace = m[0, 0] + m[1, 1] + m[2, 2]
    if trace > 0.0:
        s = 2.0 * np.sqrt(trace + 1.0)
        q = [(m[2, 1] - m[1, 2]) / s, (m[0, 2] - m[2, 0]) / s, (m[1, 0] - m[0, 1]) / s, 0.25 * s]
    elif m[0, 0] > m[1, 1] and m[0, 0] > m[2, 2]:
        s = 2.0 * np.sqrt(1.0 + m[0, 0] - m[1, 1] - m[2, 2])
        q = [0.25 * s, (m[0, 1] + m[1, 0]) / s, (m[0, 2] + m[2, 0]) / s, (m[2, 1] - m[1, 2]) / s]
    elif m[1, 1] > m[2, 2]:
        s = 2.0 * np.sqrt(1.0 + m[1, 1] - m[0, 0] - m[2, 2])
        q = [(m[0, 1] + m[1, 0]) / s, 0.25 * s, (m[1, 2] + m[2, 1]) / s, (m[0, 2] - m[2, 0]) / s]
    else:
        s = 2.0 * np.sqrt(1.0 + m[2, 2] - m[0, 0] - m[1, 1])
        q = [(m[0, 2] + m[2, 0]) / s, (m[1, 2] + m[2, 1]) / s, 0.25 * s, (m[1, 0] - m[0, 1]) / s]
    q = np.array(q) / np.linalg.norm(q)
    return -q if q[3] < 0.0 else q


def decompose(matrix):
    """Мировая матрица узла glTF → положение, поворот (кватернион) и масштаб в координатах движка.
    Зеркальная матрица — отрицательный масштаб по X (движок рисует такой экземпляр с обратным обходом граней).
    None — если у матрицы есть сдвиг осей (неравномерный масштаб родителя под поворотом): его не записать
    экземпляром, такой узел запекается в вершины."""
    linear = matrix[:3, :3]
    scale = np.linalg.norm(linear, axis=0)
    if np.any(scale < 1e-8):
        return None
    rotation = linear / scale
    if np.linalg.det(rotation) < 0.0:
        scale[0] = -scale[0]
        rotation[:, 0] = -rotation[:, 0]
    if not np.allclose(rotation.T @ rotation, np.eye(3), atol=1e-4):
        return None
    # Поворот R в правой системе → F·R·F в левой (F — зеркало по Z): у кватерниона меняют знак x и y
    x, y, z, w = quaternion(rotation)
    return {'position': matrix[:3, 3] * MIRROR, 'rotation': np.array([-x, -y, z, w]), 'scale': scale}


def mesh_nodes(gltf):
    """Узлы с мешем и их мировые матрицы (обход сцены по умолчанию)."""
    nodes = gltf.json.get('nodes', [])
    scenes = gltf.json.get('scenes', [])
    roots = scenes[gltf.json.get('scene', 0)]['nodes'] if scenes else range(len(nodes))
    found = []

    def visit(index, parent):
        node = nodes[index]
        world = parent @ node_matrix(node)
        if 'mesh' in node:
            found.append((index, node, world))
        for child in node.get('children', []):
            visit(child, world)

    for root in roots:
        visit(root, np.eye(4))
    return found


# ---------------------------------------------------------------------------------------------------------------------
# Геометрия

def normalize(vectors):
    length = np.linalg.norm(vectors, axis=1, keepdims=True)
    return vectors / np.maximum(length, 1e-12)


def face_normals(positions, triangles):
    # В движке у лицевого треугольника (b − a) × (c − a) смотрит наружу
    a, b, c = (positions[triangles[:, i]] for i in range(3))
    normals = np.zeros_like(positions)
    face = np.cross(b - a, c - a)
    for i in range(3):
        np.add.at(normals, triangles[:, i], face)
    return normalize(normals)


def uv_tangents(positions, normals, uvs, triangles):
    """Касательная dP/du и знак бинормали dP/dv по развёртке — если в файле нет TANGENT."""
    a, b, c = (triangles[:, i] for i in range(3))
    e1, e2 = positions[b] - positions[a], positions[c] - positions[a]
    d1, d2 = uvs[b] - uvs[a], uvs[c] - uvs[a]
    det = d1[:, 0] * d2[:, 1] - d2[:, 0] * d1[:, 1]
    r = np.where(np.abs(det) > 1e-12, 1.0 / np.where(det == 0, 1.0, det), 0.0)[:, None]
    face_t = (e1 * d2[:, 1:2] - e2 * d1[:, 1:2]) * r
    face_b = (e2 * d1[:, 0:1] - e1 * d2[:, 0:1]) * r
    tangents = np.zeros_like(positions)
    bitangents = np.zeros_like(positions)
    for i in (a, b, c):
        np.add.at(tangents, i, face_t)
        np.add.at(bitangents, i, face_b)
    tangents = tangents - normals * np.sum(normals * tangents, axis=1, keepdims=True)
    # Где развёртки нет (вырожденный треугольник) — любая перпендикулярная нормали ось
    bad = np.linalg.norm(tangents, axis=1) < 1e-9
    if bad.any():
        helper = np.where(np.abs(normals[bad, 0:1]) < 0.9, [[1.0, 0.0, 0.0]], [[0.0, 1.0, 0.0]])
        tangents[bad] = np.cross(normals[bad], helper)
    tangents = normalize(tangents)
    sign = np.where(np.sum(np.cross(normals, tangents) * bitangents, axis=1) < 0.0, -1.0, 1.0)
    return tangents, sign


def convert_primitive(gltf, primitive, basis, label):
    """Примитив glTF → массивы движка: позиции, нормали, UV, касательные, бинормали, индексы треугольников."""
    attributes = primitive['attributes']
    positions = gltf.accessor(attributes['POSITION']).astype(np.float64)
    count = len(positions)
    if 'indices' in primitive:
        indices = gltf.accessor(primitive['indices']).astype(np.int64).ravel()
    else:
        indices = np.arange(count, dtype=np.int64)
    triangles = indices.reshape(-1, 3)

    if 'TEXCOORD_0' in attributes:
        uvs = gltf.accessor(attributes['TEXCOORD_0']).astype(np.float64)[:, :2]
    else:
        warn('%s: no TEXCOORD_0, UV set to zero' % label)
        uvs = np.zeros((count, 2))
    if 'COLOR_0' in attributes:
        warn('%s: vertex colors (COLOR_0) are ignored' % label)
    if 'JOINTS_0' in attributes or primitive.get('targets'):
        warn('%s: skinning and morph targets are ignored, the mesh is imported in its bind pose' % label)

    # Поворот и масштаб узла; нормали — обратной транспонированной матрицей
    determinant = np.linalg.det(basis)
    positions = positions @ basis.T
    normals = None
    if 'NORMAL' in attributes:
        normals = normalize(gltf.accessor(attributes['NORMAL']).astype(np.float64) @ np.linalg.inv(basis))
    tangents = None
    if 'TANGENT' in attributes:
        tangent4 = gltf.accessor(attributes['TANGENT']).astype(np.float64)
        tangents = tangent4[:, :3] @ basis.T
        # Зеркальная матрица узла меняет направление бинормали cross(N, T)·w
        handedness = tangent4[:, 3] * np.sign(determinant)

    # glTF правый → движок левый: зеркало по Z. Треугольник меняет обход: лицевой в glTF — (b − a) × (c − a)
    # наружу, после зеркала — внутрь, поэтому две вершины меняются местами (ещё раз — если зеркальна матрица узла)
    mirror = MIRROR
    positions = positions * mirror
    if determinant > 0.0:
        triangles = triangles[:, [0, 2, 1]]

    if normals is None:
        warn('%s: no NORMAL, smooth normals are computed' % label)
        normals = face_normals(positions, triangles)
    else:
        normals = normals * mirror

    if tangents is None:
        warn('%s: no TANGENT, tangents are computed from UV (export with Tangents for exact MikkTSpace)' % label)
        tangents, handedness = uv_tangents(positions, normals, uvs, triangles)
    else:
        tangents = normalize(tangents * mirror)
        tangents = normalize(tangents - normals * np.sum(normals * tangents, axis=1, keepdims=True))

    # Бинормаль движка — направление роста v (вниз по картинке): у зеркально отражённого базиса glTF это
    # cross(N, T)·w; карта нормалей glTF (зелёный — вверх) читается с NormalGreenUp = true
    binormals = np.cross(normals, tangents) * handedness[:, None]
    return positions, normals, uvs, tangents, binormals, triangles


def merge(parts):
    positions, normals, uvs, tangents, binormals, triangles = [], [], [], [], [], []
    base = 0
    for p, n, uv, t, b, tri in parts:
        positions.append(p), normals.append(n), uvs.append(uv), tangents.append(t), binormals.append(b)
        triangles.append(tri + base)
        base += len(p)
    return tuple(np.concatenate(x) for x in (positions, normals, uvs, tangents, binormals, triangles))


def mesh_bytes(mesh):
    positions, normals, uvs, tangents, binormals, triangles = mesh
    vertices = np.hstack([positions, normals, uvs, tangents, binormals]).astype('<f4')
    return struct.pack('<II', len(vertices), triangles.size) + vertices.tobytes() + triangles.astype('<u4').tobytes()


# ---------------------------------------------------------------------------------------------------------------------
# Материалы

class MaterialConverter:
    def __init__(self, gltf, asset):
        self.gltf = gltf
        self.asset = asset
        self.textures = {}   # индекс картинки → {'name', 'file', 'srgb', 'data'}
        self.image_names = set()
        self.double_sided = []

    def texture(self, info, srgb, material_name, slot):
        if info.get('texCoord', 0) != 0:
            warn('material %s: %s uses TEXCOORD_%d, the engine has only TEXCOORD_0' % (material_name, slot, info['texCoord']))
        if 'KHR_texture_transform' in info.get('extensions', {}):
            warn('material %s: KHR_texture_transform of %s is ignored' % (material_name, slot))
        texture = self.gltf.json['textures'][info['index']]
        sampler = self.gltf.json.get('samplers', [{}])[texture['sampler']] if 'sampler' in texture else {}
        if sampler.get('wrapS', WRAP_REPEAT) != WRAP_REPEAT or sampler.get('wrapT', WRAP_REPEAT) != WRAP_REPEAT:
            warn('material %s: %s wrap mode is not REPEAT, the engine repeats textures' % (material_name, slot))
        if 'source' not in texture:
            raise SystemExit('error: texture %d has no PNG/JPEG source (extensions are not supported)' % info['index'])
        source = texture['source']
        if source not in self.textures:
            data, extension = self.gltf.image(source)
            image_name = sanitize(self.gltf.json['images'][source].get('name') or 'image%d' % source)
            if image_name in self.image_names:   # две картинки с одним именем — разные файлы
                image_name = '%s_%d' % (image_name, source)
            self.image_names.add(image_name)
            self.textures[source] = {
                'name': '%s_%s' % (self.asset, image_name),
                'file': 'models\\%s\\%s.%s' % (self.asset, image_name, extension),
                'srgb': srgb, 'data': data, 'coverage': None}
        elif self.textures[source]['srgb'] != srgb:
            warn('image %s is used both as color and as data, it is read as %s'
                 % (self.textures[source]['name'], 'sRGB' if self.textures[source]['srgb'] else 'linear'))
        return self.textures[source]['name']

    def convert(self, index):
        """Экземпляр PBR: имя и значения параметров (строки; текстуры — именами, id подставит запись в БД)."""
        if index is None:
            # Материал по умолчанию из спецификации glTF
            return '%s/default' % self.asset, {'BaseColorFactor': '1,1,1,1', 'Metallic': '1', 'Roughness': '1'}

        material = self.gltf.json['materials'][index]
        name = material.get('name') or 'material%d' % index
        pbr = material.get('pbrMetallicRoughness', {})
        params = {
            'BaseColorFactor': ','.join('%g' % v for v in pbr.get('baseColorFactor', [1.0, 1.0, 1.0, 1.0])),
            'Metallic': '%g' % pbr.get('metallicFactor', 1.0),
            'Roughness': '%g' % pbr.get('roughnessFactor', 1.0),
        }
        textures = {}
        if 'baseColorTexture' in pbr:
            textures['BaseColor'] = self.texture(pbr['baseColorTexture'], True, name, 'baseColorTexture')
        if 'metallicRoughnessTexture' in pbr:
            textures['MetallicRoughness'] = self.texture(pbr['metallicRoughnessTexture'], False, name,
                                                         'metallicRoughnessTexture')
        if 'normalTexture' in material:
            textures['Normal'] = self.texture(material['normalTexture'], False, name, 'normalTexture')
            params['NormalScale'] = '%g' % material['normalTexture'].get('scale', 1.0)
            params['NormalGreenUp'] = 'true'
        if 'occlusionTexture' in material:
            textures['Occlusion'] = self.texture(material['occlusionTexture'], False, name, 'occlusionTexture')
            params['OcclusionStrength'] = '%g' % material['occlusionTexture'].get('strength', 1.0)

        emissive = np.array(material.get('emissiveFactor', [0.0, 0.0, 0.0]))
        extensions = material.get('extensions', {})
        emissive = emissive * extensions.get('KHR_materials_emissive_strength', {}).get('emissiveStrength', 1.0)
        if 'emissiveTexture' in material:
            textures['Emissive'] = self.texture(material['emissiveTexture'], True, name, 'emissiveTexture')
        if emissive.any() or 'emissiveTexture' in material:
            params['EmissiveFactor'] = ','.join('%g' % v for v in emissive)

        # Пропускание тонкой поверхностью (листья, травинки) — параметры с теми же именами
        transmission = extensions.get('KHR_materials_diffuse_transmission')
        if transmission is not None:
            params['DiffuseTransmissionFactor'] = '%g' % transmission.get('diffuseTransmissionFactor', 0.0)
            params['DiffuseTransmissionColorFactor'] = ','.join(
                '%g' % v for v in transmission.get('diffuseTransmissionColorFactor', [1.0, 1.0, 1.0]))
            if 'diffuseTransmissionColorTexture' in transmission:
                textures['DiffuseTransmissionColor'] = self.texture(transmission['diffuseTransmissionColorTexture'], True,
                                                                    name, 'diffuseTransmissionColorTexture')
            if 'diffuseTransmissionTexture' in transmission:
                warn('material %s: diffuseTransmissionTexture is ignored, only the factor is used' % name)

        ignored = sorted(set(extensions) - {'KHR_materials_emissive_strength', 'KHR_materials_diffuse_transmission'})
        if ignored:
            warn('material %s: extensions are ignored: %s' % (name, ', '.join(ignored)))
        # Режим материала — как в glTF: AlphaMode 0 OPAQUE, 1 MASK, 2 BLEND; порог отсечения; двусторонность
        alpha = material.get('alphaMode', 'OPAQUE')
        params['AlphaMode'] = str({'OPAQUE': 0, 'MASK': 1, 'BLEND': 2}.get(alpha, 0))
        if alpha == 'MASK':
            params['AlphaCutoff'] = '%g' % material.get('alphaCutoff', 0.5)
            if 'baseColorTexture' in pbr:
                # Мипы с сохранением покрытия альфы при пороге отсечения
                source = self.gltf.json['textures'][pbr['baseColorTexture']['index']]['source']
                self.textures[source]['coverage'] = material.get('alphaCutoff', 0.5)
        if alpha == 'BLEND':
            warn('material %s: alphaMode BLEND - translucent models are sorted by origin, not by triangle' % name)
        params['DoubleSided'] = 'true' if material.get('doubleSided') else 'false'
        if material.get('doubleSided'):
            self.double_sided.append(name)

        params.update(textures)
        # Параметры движка из extras (Custom Properties материала Blender) — поверх перенесённых из glTF. Строка с именем
        # текстуры материала — та же текстура (DiffuseTransmissionColor = BaseColor)
        for key, value in material.get('extras', {}).items():
            if isinstance(value, bool):
                params[key] = 'true' if value else 'false'
            elif isinstance(value, (int, float)):
                params[key] = '%g' % value
            elif isinstance(value, list):
                params[key] = ','.join('%g' % v for v in value)
            elif value in textures:
                params[key] = textures[value]
            else:
                params[key] = str(value)
        return '%s/%s' % (self.asset, sanitize(name)), params


# ---------------------------------------------------------------------------------------------------------------------
# Разбор сцены на модели

def collect_models(gltf, asset, materials, rename, bake, bake_scale=1.0):
    """bake — запечь поворот и масштаб объектов в вершины (модели для расстановки: их ставят слои), bake_scale —
    ещё и общий масштаб (--scale у --scatter)."""
    lod_pattern = re.compile(r'^(.*)_LOD(\d+)$', re.IGNORECASE)
    groups = {}   # имя модели → {lod: (узел, мировая матрица)}
    for index, node, world in mesh_nodes(gltf):
        name = node.get('name') or gltf.json['meshes'][node['mesh']].get('name') or 'node%d' % index
        match = lod_pattern.match(name)
        base, lod = (match.group(1), int(match.group(2))) if match else (name, 0)
        base = sanitize(base)
        if lod in groups.setdefault(base, {}):
            raise SystemExit('error: two objects are LOD%d of model %s' % (lod, base))
        groups[base][lod] = (node, world)

    # Экземпляр — положение, поворот и масштаб LOD0 на уровне. Объекты с теми же мешами всех LOD (связанные
    # дубликаты) — экземпляры первой такой модели
    specs = []
    by_meshes = {}   # меши LOD → описание модели
    for base, lods in groups.items():
        order = sorted(lods)
        if order != list(range(len(order))):
            warn('model %s: LOD numbers %s are renumbered from 0' % (base, order))
        lod0_world = lods[order[0]][1]
        placement = None if bake else decompose(lod0_world)
        key = tuple(lods[lod][0]['mesh'] for lod in order)
        if placement is not None and key in by_meshes:
            by_meshes[key]['instances'].append(placement)
            continue
        baked = placement is None
        if baked:
            if not bake:
                warn('model %s: object transform is sheared, it is baked into vertices' % base)
            placement = {'position': lod0_world[:3, 3] * MIRROR, 'rotation': np.array([0.0, 0.0, 0.0, 1.0]),
                         'scale': np.ones(3)}
            # каждый LOD — со своими поворотом и масштабом
            bases = [lods[lod][1][:3, :3] * (bake_scale if bake else 1.0) for lod in order]
        else:
            # Вершины — в координатах объекта LOD0; у других LOD — их поворот и масштаб относительно LOD0
            # (сдвиг LOD не важен: в Blender их обычно ставят рядом)
            inverse = np.linalg.inv(lod0_world[:3, :3])
            bases = [inverse @ lods[lod][1][:3, :3] for lod in order]
        spec = {'base': base, 'lods': [lods[lod][0] for lod in order], 'bases': bases, 'instances': [placement]}
        specs.append(spec)
        if not baked:
            by_meshes[key] = spec

    if rename:
        if len(specs) != 1:
            raise SystemExit('error: --name needs a file with one model, found: %s' %
                             ', '.join(spec['base'] for spec in specs))
        specs[0]['base'] = sanitize(rename)

    models = []
    for spec in specs:
        base = spec['base']

        # Секции LOD по материалам: примитивы одного материала сливаются в один меш. Номер секции (слот материала)
        # один во всех LOD модели — по порядку появления материала
        slots = []      # индексы материалов glTF по слотам
        lods = []       # по LOD: [{'slot', 'mesh', 'instance', 'params'}]
        for lod_number, (node, basis) in enumerate(zip(spec['lods'], spec['bases'])):
            parts = {}
            for p_index, primitive in enumerate(gltf.json['meshes'][node['mesh']]['primitives']):
                label = '%s LOD%d primitive %d' % (base, lod_number, p_index)
                if primitive.get('mode', TRIANGLES) != TRIANGLES:
                    warn('%s: mode %d is not triangles, skipped' % (label, primitive['mode']))
                    continue
                parts.setdefault(primitive.get('material'), []).append(
                    convert_primitive(gltf, primitive, basis, label))
            for material_index in parts:
                if material_index not in slots:
                    slots.append(material_index)
            sections = []
            for slot, material_index in enumerate(slots):
                if material_index in parts:
                    instance, params = materials.convert(material_index)
                    sections.append({'slot': slot, 'mesh': merge(parts[material_index]), 'instance': instance,
                                     'params': params})
            if not sections:
                warn('model %s: LOD%d has no triangles, LODs from there are dropped' % (base, lod_number))
                break
            lods.append(sections)
        if lods:
            models.append({'name': base, 'lods': lods, 'instances': spec['instances']})
    return models


# ---------------------------------------------------------------------------------------------------------------------
# Запись в base.db3

class Database:
    def __init__(self, path, material_id):
        self.connection = sqlite3.connect(path)
        self.cursor = self.connection.cursor()
        self.material_id = material_id
        self.definitions = dict(self.cursor.execute(
            'SELECT param_name, id_parameter_def FROM MaterialParameterDef WHERE id_material = ?', (material_id,)))
        if not self.definitions:
            raise SystemExit('error: material %d has no parameters in MaterialParameterDef' % material_id)

    def one(self, sql, args=()):
        row = self.cursor.execute(sql, args).fetchone()
        return row[0] if row else None

    def texture(self, name, file, srgb, coverage):
        texture_id = self.one('SELECT id FROM Textures WHERE name = ?', (name,))
        if texture_id is None:
            self.cursor.execute('INSERT INTO Textures (name, file, generate_mipmap, sRGB, preserve_alpha_coverage) '
                                'VALUES (?, ?, 1, ?, ?)', (name, file, int(srgb), coverage))
            return self.cursor.lastrowid
        self.cursor.execute('UPDATE Textures SET file = ?, generate_mipmap = 1, sRGB = ?, preserve_alpha_coverage = ? '
                            'WHERE id = ?', (file, int(srgb), coverage, texture_id))
        return texture_id

    def instance(self, name, params, texture_ids):
        instance_id = self.one('SELECT id_instance FROM MaterialInstance WHERE name = ? AND id_material = ?',
                               (name, self.material_id))
        if instance_id is None:
            self.cursor.execute('INSERT INTO MaterialInstance (id_material, name) VALUES (?, ?)', (self.material_id, name))
            instance_id = self.cursor.lastrowid
        self.cursor.execute('DELETE FROM MaterialParameterInstance WHERE id_instance = ?', (instance_id,))
        for param, value in params.items():
            if param not in self.definitions:
                raise SystemExit('error: material %d has no parameter %s' % (self.material_id, param))
            value = str(texture_ids[value]) if value in texture_ids else value
            self.cursor.execute('INSERT INTO MaterialParameterInstance (id_instance, id_material_def, value) '
                                'VALUES (?, ?, ?)', (instance_id, self.definitions[param], value))
        return instance_id

    def mesh(self, name, file):
        mesh_id = self.one('SELECT id FROM Meshes WHERE name = ?', (name,))
        if mesh_id is None:
            self.cursor.execute('INSERT INTO Meshes (name, file, primitive) VALUES (?, ?, ?)',
                                (name, file, FALLBACK_PRIMITIVE))
            return self.cursor.lastrowid
        self.cursor.execute('UPDATE Meshes SET file = ?, primitive = ? WHERE id = ?', (file, FALLBACK_PRIMITIVE, mesh_id))
        return mesh_id

    def model(self, name, lods, render):
        """lods — по LOD: ([(секция, id меша, id экземпляра материала)], дальность); строка ModelProperties — секция"""
        model_id = self.one('SELECT id FROM Models WHERE name = ?', (name,))
        if model_id is None:
            self.cursor.execute('INSERT INTO Models (name) VALUES (?)', (name,))
            model_id = self.cursor.lastrowid
        old_meshes = [r[0] for r in self.cursor.execute('SELECT mesh_id FROM ModelProperties WHERE model_id = ?',
                                                        (model_id,))]
        self.cursor.execute('DELETE FROM ModelProperties WHERE model_id = ?', (model_id,))
        new_meshes = set()
        for lod, (sections, lod_range) in enumerate(lods):
            for section, mesh_id, instance_id in sections:
                self.cursor.execute('INSERT INTO ModelProperties (lod, section, model_id, range, material_id, mesh_id, '
                                    'render, material_instance_id) VALUES (?, ?, ?, ?, ?, ?, ?, ?)',
                                    (lod, section, model_id, lod_range, self.material_id, mesh_id, render, instance_id))
                new_meshes.add(mesh_id)
        # Меши LOD и секций, которых больше нет (reimport с меньшим числом LOD), — если на них никто не ссылается
        for mesh_id in set(old_meshes) - new_meshes:
            if self.one('SELECT COUNT(*) FROM ModelProperties WHERE mesh_id = ?', (mesh_id,)) == 0:
                self.cursor.execute('DELETE FROM Meshes WHERE id = ?', (mesh_id,))
        return model_id

    def place(self, level_id, model_id, instances):
        """Экземпляры модели на уровне: повторный импорт заменяет прежние строки этой модели."""
        def text(vector, digits):
            return ','.join('%g' % (round(float(c), digits) + 0.0) for c in vector)
        self.cursor.execute('DELETE FROM LevelModels WHERE level = ? AND model = ?', (level_id, model_id))
        for instance in instances:
            self.cursor.execute('INSERT INTO LevelModels (level, model, position, rotation, scale) '
                                'VALUES (?, ?, ?, ?, ?)',
                                (level_id, model_id, text(instance['position'], 4), text(instance['rotation'], 6),
                                 text(instance['scale'], 4)))


# ---------------------------------------------------------------------------------------------------------------------

def parse_vector(text, size, option):
    values = [float(v) for v in re.split(r'[,\s]+', text.strip()) if v]
    if len(values) != size:
        raise SystemExit('error: %s expects %d numbers' % (option, size))
    return np.array(values)


def lod_ranges(count, text):
    if text:
        values = [float(v) for v in re.split(r'[,\s]+', text.strip()) if v]
        if len(values) not in (count - 1, count):
            raise SystemExit('error: --lod-ranges expects %d or %d values for %d LODs' % (count - 1, count, count))
    else:
        values = [25.0 * 2 ** i for i in range(count - 1)]
    if len(values) == count - 1:
        values.append(LAST_LOD_RANGE)
    if any(b <= a for a, b in zip(values, values[1:])):
        raise SystemExit('error: LOD ranges must grow: %s' % values)
    return values


def main():
    parser = argparse.ArgumentParser(description='Import a glTF 2.0 model (.glb/.gltf) into DMEngine (base.db3).')
    parser.add_argument('file', help='.glb or .gltf file')
    parser.add_argument('--name', help='model name instead of the object name (file with one model)')
    parser.add_argument('--asset', help='folder and prefix for files and textures (default: file name)')
    parser.add_argument('--level', help='place the models on this level (Levels.name)')
    parser.add_argument('--position', default='0,0,0', help='level position of the file origin: x,y,z')
    parser.add_argument('--snap-to-terrain', action='store_true',
                        help='put the level instances on the level terrain: y = terrain height + object height in Blender')
    parser.add_argument('--scale', type=float, default=1.0,
                        help='uniform scale on the level; with --scatter it is baked into vertices')
    parser.add_argument('--lod-ranges', help='LOD distances in meters, e.g. 25,60 (last LOD: 10000)')
    parser.add_argument('--scatter', action='store_true', help='model for scatter layers: PBRInstance, render = 0')
    parser.add_argument('--dry-run', action='store_true', help='print what would be written, write nothing')
    parser.add_argument('--db', default=os.path.join(ROOT, 'base.db3'))
    args = parser.parse_args()

    if args.scatter and args.level:
        raise SystemExit('error: --scatter models are placed by scatter layers, not by --level')
    if args.snap_to_terrain and not args.level:
        raise SystemExit('error: --snap-to-terrain needs --level')

    gltf = Gltf(args.file)
    asset = sanitize(args.asset or os.path.splitext(os.path.basename(args.file))[0])
    materials = MaterialConverter(gltf, asset)
    models = collect_models(gltf, asset, materials, args.name, args.scatter, args.scale)
    if not models:
        raise SystemExit('error: no meshes in %s' % args.file)
    if materials.double_sided:
        print('Double-sided materials (drawn without back-face culling): %s. A closed mesh does not need it: '
              'enable Backface Culling in the Blender material' % ', '.join(materials.double_sided))

    material_id = PBR_INSTANCE_MATERIAL if args.scatter else PBR_MATERIAL
    db = Database(args.db, material_id)
    level_id = None
    if args.level:
        level_id = db.one('SELECT id FROM Levels WHERE name = ?', (args.level,))
        if level_id is None:
            raise SystemExit('error: level %s is not found in Levels' % args.level)
    position = parse_vector(args.position, 3, '--position')
    ground = None
    if args.snap_to_terrain:
        ground = terrain.load_level_terrain(db.connection, level_id, ROOT)
        if ground is None:
            raise SystemExit('error: level %s has no terrain for --snap-to-terrain' % args.level)

    files = {}   # путь относительно корня → байты
    texture_ids = {}
    for texture in materials.textures.values():
        texture_ids[texture['name']] = db.texture(texture['name'], texture['file'], texture['srgb'], texture['coverage'])
        files[os.path.join('Textures', texture['file'])] = texture['data']
        coverage = ', alpha coverage %g' % texture['coverage'] if texture['coverage'] is not None else ''
        print('Texture %s -> Textures\\%s (%s%s)' % (texture['name'], texture['file'],
                                                      'sRGB' if texture['srgb'] else 'linear', coverage))

    instances = {}
    for model in models:
        ranges = lod_ranges(len(model['lods']), args.lod_ranges)
        lods = []
        for lod, sections in enumerate(model['lods']):
            rows = []
            for section in sections:
                if section['instance'] not in instances:
                    instances[section['instance']] = db.instance(section['instance'], section['params'], texture_ids)
                # Секция 0 — прежнее имя меша LOD: повторный импорт модели из одного материала ничего не переименует
                mesh_name = '%s_LOD%d' % (model['name'], lod) + ('_S%d' % section['slot'] if section['slot'] else '')
                mesh_file = 'models\\%s\\%s.bin' % (asset, mesh_name)
                rows.append((section['slot'], db.mesh(mesh_name, mesh_file), instances[section['instance']]))
                files[os.path.join('Meshes', mesh_file)] = mesh_bytes(section['mesh'])
            lods.append((rows, ranges[lod]))
        model_id = db.model(model['name'], lods, 0 if args.scatter else 1)

        positions = np.concatenate([section['mesh'][0] for section in model['lods'][0]])
        lo, hi = positions.min(axis=0), positions.max(axis=0)
        print('Model %s: %s, bounds (%.2f %.2f %.2f)..(%.2f %.2f %.2f)' % (
            model['name'], ', '.join('LOD%d %d verts %d tris <= %gm' % (
                i, sum(len(s['mesh'][0]) for s in sections), sum(len(s['mesh'][5]) for s in sections), ranges[i])
                for i, sections in enumerate(model['lods'])),
            lo[0], lo[1], lo[2], hi[0], hi[1], hi[2]))
        for section in model['lods'][0]:
            print('  section %d: material instance %s' % (section['slot'], section['instance']))
        if level_id is not None:
            # Origin файла — в --position с равномерным масштабом --scale
            placed = [{'position': position + instance['position'] * args.scale, 'rotation': instance['rotation'],
                       'scale': instance['scale'] * args.scale} for instance in model['instances']]
            if ground is not None:
                # Высота над нулём в Blender — над землёй: y --position не учитывается
                heights, cell = ground
                for instance, source in zip(placed, model['instances']):
                    x, _, z = instance['position']
                    instance['position'][1] = terrain.sample(heights, x, z, cell) + source['position'][1] * args.scale
            db.place(level_id, model_id, placed)
            for instance in placed:
                print('  level %s: position %.2f,%.2f,%.2f rotation %.3f,%.3f,%.3f,%.3f scale %.3g,%.3g,%.3g' % (
                    (args.level,) + tuple(instance['position']) + tuple(instance['rotation']) +
                    tuple(instance['scale'])))

    if args.dry_run:
        db.connection.rollback()
        print('Dry run: nothing written (%d files, %d models)' % (len(files), len(models)))
        return

    for path, data in files.items():
        full = os.path.join(ROOT, path.replace('\\', os.sep))
        os.makedirs(os.path.dirname(full), exist_ok=True)
        with open(full, 'wb') as out:
            out.write(data)
    db.connection.commit()
    print('Imported %d models, %d files%s' % (len(models), len(files),
                                              ', %d warnings' % len(warnings) if warnings else ''))


if __name__ == '__main__':
    main()
