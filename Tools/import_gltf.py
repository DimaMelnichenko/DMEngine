"""Импорт моделей glTF 2.0 (.glb, .gltf) в DMEngine — как импорт FBX/glTF в редакторе UE.

    python Tools/import_gltf.py модель.glb [--level Test --position x,y,z [--snap-to-terrain]] [--scale s]
                                           [--lod-ranges 25,60] [--shadow-lod-scale 0.5] [--shadow-min-lod 2]
                                           [--collision hull|box|mesh|capsule|none] [--collision-radius r]
                                           [--name Имя] [--scatter] [--dry-run]

Пишет файлы мешей (Meshes\\models\\<ассет>\\<модель>_LOD<N>[_S<секция>].bin, формат MeshLoader), картинки текстур
(Textures\\models\\<ассет>\\) и строки base.db3: Meshes, Models, ModelProperties, Textures, MaterialInstance +
MaterialParameterInstance, по --level — LevelModels и LevelLights. Всё в одной транзакции; повторный импорт того же файла обновляет
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
  карты нормалей glTF в соглашении OpenGL — NormalGreenUp = true. Расширения: KHR_materials_emissive_strength,
  KHR_materials_diffuse_transmission (DiffuseTransmission*), KHR_materials_specular (только specularFactor —
  SpecularFactor), остальные — с предупреждением.
- У материала alphaMode MASK мипы базового цвета сохраняют покрытие альфы при его alphaCutoff
  (Textures.preserve_alpha_coverage): иначе вдали травинки и лепестки тают.
- Параметры движка без аналога в glTF (пропускание — экспортёр Blender не пишет KHR_materials_diffuse_transmission,
  DitheredLODTransition, WindWeight) — extras материала (Custom Properties материала Blender, экспорт с
  export_extras) с именами параметров PBR; значение-строка с именем текстуры материала (BaseColor) — та же текстура.
- Простая коллизия модели (Models.collision, docs/physics.md): --collision для всех моделей файла или extras объекта LOD0
  (Custom Property «collision» объекта Blender, экспорт с export_extras); ни того ни другого — не менять (у новой — NULL,
  выпуклая оболочка). Радиус ствола (capsule, Models.collision_radius) — --collision-radius или extras «collision_radius»;
  без него — по основанию модели.
- --snap-to-terrain ставит экземпляры на землю уровня, как Drop to surface в UE: высота — террейн в точке (x, z)
  плюс высота объекта над нулём в Blender (Tools/terrain.py).
- Источники света (KHR_lights_punctual: в Blender — флажок Punctual Lights при экспорте) с --level — строки LevelLights,
  как источники в уровне UE: тип, цвет, интенсивность (единицы glTF — у направленного люксы, у точечного и прожектора
  канделы — те же, что у LevelLights), range → attenuation_radius (нет — без обрезания), конус прожектора (радианы →
  градусы), место узла относительно --position (с --snap-to-terrain — над землёй, как экземпляры), направление — ось −Z
  узла. Повторный импорт обновляет строку источника уровня с тем же именем (имя узла), прочие колонки (тени, включён)
  остаются; новый направленный — без атмосферы и теней, если у уровня уже есть направленный (солнце уровня — первый),
  иначе он и есть солнце: с атмосферой и тенями. Источник, убранный в Blender, на уровне остаётся.
- Ветер дерева (docs/wind.md) — собственные атрибуты glTF, как входы Games wind SpeedTree: _WIND_BRANCH1 и
  _WIND_BRANCH2 (VEC4: начало ветви xyz в пространстве позиций и вес), _WIND_WEIGHTS (VEC2: доля высоты дерева, вес
  ряби). Пишутся блоком WIND в конце файла меша; материал секций с ними — PBRTree (id 13).

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
PBR_TREE_MATERIAL = 13   # PBR с ветром дерева (WIND_TREE): у мешей с данными ветра
WIND_ATTRIBUTES = (('_WIND_BRANCH1', 4), ('_WIND_BRANCH2', 4), ('_WIND_WEIGHTS', 2))
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

        # Источники света экспортёр Blender помечает обязательными — их скрипт читает (collect_lights); материалы
        # specular-glossiness (Sketchfab) переводит в metallic-roughness (MaterialConverter)
        required = set(self.json.get('extensionsRequired', [])) - {'KHR_lights_punctual',
                                                                   'KHR_materials_pbrSpecularGlossiness'}
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


def scene_nodes(gltf):
    """Все узлы сцены по умолчанию и их мировые матрицы."""
    nodes = gltf.json.get('nodes', [])
    scenes = gltf.json.get('scenes', [])
    roots = scenes[gltf.json.get('scene', 0)]['nodes'] if scenes else range(len(nodes))
    found = []

    def visit(index, parent):
        node = nodes[index]
        world = parent @ node_matrix(node)
        found.append((index, node, world))
        for child in node.get('children', []):
            visit(child, world)

    for root in roots:
        visit(root, np.eye(4))
    return found


def mesh_nodes(gltf):
    """Узлы с мешем и их мировые матрицы (обход сцены по умолчанию)."""
    return [(index, node, world) for index, node, world in scene_nodes(gltf) if 'mesh' in node]


def collect_lights(gltf):
    """Источники KHR_lights_punctual узлов сцены в координатах движка: место, направление (ось −Z узла — куда идёт
    свет), тип, цвет, интенсивность, радиус и конус прожектора в градусах."""
    definitions = gltf.json.get('extensions', {}).get('KHR_lights_punctual', {}).get('lights', [])
    lights = []
    for index, node, world in scene_nodes(gltf):
        reference = node.get('extensions', {}).get('KHR_lights_punctual')
        if reference is None:
            continue
        light = definitions[reference['light']]
        direction = world[:3, :3] @ np.array([0.0, 0.0, -1.0])
        direction = direction / max(np.linalg.norm(direction), 1e-12) * MIRROR
        spot = light.get('spot', {})
        lights.append({
            'name': node.get('name') or light.get('name') or 'Light%d' % index,
            'type': light['type'],
            'color': np.array(light.get('color', [1.0, 1.0, 1.0])),
            'intensity': float(light.get('intensity', 1.0)),
            'range': float(light.get('range', 0.0)),
            'inner': np.degrees(spot.get('innerConeAngle', 0.0)),
            'outer': np.degrees(spot.get('outerConeAngle', np.pi / 4.0)),
            'position': world[:3, 3] * MIRROR,
            'direction': direction})
    return lights


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


def quantize_skin(joints, weights):
    """Кости и веса вершины → 4 индекса uint8 и 4 веса unorm8 с суммой 255 (блок SKIN файла меша, VertexData::Skin)."""
    weights = np.where(weights > 0.0, weights, 0.0)
    total = weights.sum(axis=1, keepdims=True)
    weights = np.where(total > 0.0, weights / np.maximum(total, 1e-12), np.array([[1.0, 0.0, 0.0, 0.0]]))
    quantized = np.floor(weights * 255.0 + 0.5).astype(np.int64)
    # Остаток округления — самому весомому влиянию: сумма ровно 255
    largest = np.argmax(quantized, axis=1)
    quantized[np.arange(len(quantized)), largest] += 255 - quantized.sum(axis=1)
    joints = np.where(quantized > 0, joints, 0)
    return np.hstack([joints, quantized]).astype(np.uint8)


def convert_primitive(gltf, primitive, basis, label, offset=None, skin=None):
    """Примитив glTF → массивы движка: позиции, нормали, UV, касательные, бинормали, индексы треугольников, данные
    ветра дерева (10 чисел на вершину, VertexData::Wind; None — их нет) и скин (8 байт на вершину, VertexData::Skin; None —
    нет). offset — сдвиг после basis (модель со скелетом: вершины в позе покоя в пространстве модели); skin — ('weights',
    номера костей движка по номерам скина) или ('rigid', кость) — сетка на кости целиком (рога на голове)."""
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
    skin_data = None
    if skin is not None and skin[0] == 'rigid':
        skin_data = np.zeros((count, 8), dtype=np.uint8)
        skin_data[:, 0] = skin[1]
        skin_data[:, 4] = 255
    elif skin is not None:
        if 'JOINTS_0' not in attributes or 'WEIGHTS_0' not in attributes:
            raise SystemExit('error: %s: a skinned primitive without JOINTS_0 / WEIGHTS_0' % label)
        if 'JOINTS_1' in attributes:
            warn('%s: more than 4 joints per vertex, only JOINTS_0 / WEIGHTS_0 are used' % label)
        joints = skin[1][gltf.accessor(attributes['JOINTS_0']).astype(np.int64)]
        skin_data = quantize_skin(joints, gltf.accessor(attributes['WEIGHTS_0']).astype(np.float64)[:, :4])
    elif 'JOINTS_0' in attributes:
        warn('%s: skinning is ignored (the node has no skin), the mesh is imported in its bind pose' % label)
    if primitive.get('targets'):
        warn('%s: morph targets are ignored' % label)
    wind = None
    if any(name in attributes for name, _ in WIND_ATTRIBUTES):
        wind = np.hstack([gltf.accessor(attributes[name]).astype(np.float64).reshape(count, -1)[:, :size]
                          if name in attributes else np.zeros((count, size)) for name, size in WIND_ATTRIBUTES])

    # Поворот и масштаб узла; нормали — обратной транспонированной матрицей
    determinant = np.linalg.det(basis)
    positions = positions @ basis.T
    if offset is not None:
        positions = positions + offset
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
    if wind is not None:
        # Начала ветвей — точки в пространстве позиций: та же матрица узла и то же зеркало
        for first in (0, 4):
            wind[:, first:first + 3] = (wind[:, first:first + 3] @ basis.T) * mirror
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
    return positions, normals, uvs, tangents, binormals, triangles, wind, skin_data


def merge(parts):
    positions, normals, uvs, tangents, binormals, triangles, wind, skin = [], [], [], [], [], [], [], []
    base = 0
    for p, n, uv, t, b, tri, w, s in parts:
        positions.append(p), normals.append(n), uvs.append(uv), tangents.append(t), binormals.append(b)
        triangles.append(tri + base)
        wind.append(w)
        skin.append(s)
        base += len(p)
    merged = tuple(np.concatenate(x) for x in (positions, normals, uvs, tangents, binormals, triangles))
    # У примитива без данных ветра — нули (не гнётся), если они есть у другого примитива того же материала
    if all(w is None for w in wind):
        merged += (None,)
    else:
        merged += (np.concatenate([w if w is not None else np.zeros((len(p), 10)) for w, p in zip(wind, positions)]),)
    if all(s is None for s in skin):
        return merged + (None,)
    if any(s is None for s in skin):
        raise SystemExit('error: skinned and static primitives share a material')
    return merged + (np.concatenate(skin),)


def mesh_bytes(mesh):
    positions, normals, uvs, tangents, binormals, triangles, wind, skin = mesh
    vertices = np.hstack([positions, normals, uvs, tangents, binormals]).astype('<f4')
    data = struct.pack('<II', len(vertices), triangles.size) + vertices.tobytes() + triangles.astype('<u4').tobytes()
    # Необязательные блоки: 4 байта метки и данные на вершину (MeshLoader)
    if wind is not None:
        # Ветер дерева: начала и веса ветвей, доли высоты и ряби (VertexData::Wind, 40 байт)
        data += b'WIND' + wind.astype('<f4').tobytes()
    if skin is not None:
        # Скин: 4 номера костей uint8 и 4 веса unorm8 с суммой 255 (VertexData::Skin, 8 байт)
        data += b'SKIN' + skin.astype(np.uint8).tobytes()
    return data


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
            # Имя картинки, без него — имя её файла (у Sketchfab имён нет: textures/Body_diffuse.png)
            image = self.gltf.json['images'][source]
            uri = image.get('uri', '')
            file_name = os.path.splitext(os.path.basename(uri))[0] if uri and not uri.startswith('data:') else ''
            image_name = sanitize(image.get('name') or file_name or 'image%d' % source)
            if image_name in self.image_names:   # две картинки с одним именем — разные файлы
                image_name = '%s_%d' % (image_name, source)
            self.image_names.add(image_name)
            # Сжатие на GPU (Textures.compression, как Compression Settings в UE): карта нормалей — BC5, прочее — BC7
            self.textures[source] = {
                'name': '%s_%s' % (self.asset, image_name),
                'file': 'models\\%s\\%s.%s' % (self.asset, image_name, extension),
                'srgb': srgb, 'data': data, 'coverage': None,
                'compression': 'normalmap' if slot == 'normalTexture' else 'default'}
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
        extensions = material.get('extensions', {})
        glossiness = extensions.get('KHR_materials_pbrSpecularGlossiness')
        pbr = material.get('pbrMetallicRoughness', {})
        if glossiness is not None:
            pbr = self.from_specular_glossiness(glossiness, name)
        params = {
            'BaseColorFactor': ','.join('%g' % v for v in pbr.get('baseColorFactor', [1.0, 1.0, 1.0, 1.0])),
            'Metallic': '%g' % pbr.get('metallicFactor', 1.0),
            'Roughness': '%g' % pbr.get('roughnessFactor', 1.0),
        }
        if 'specularFactor' in pbr:
            params['SpecularFactor'] = '%g' % pbr['specularFactor']
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

        # Сила отражения диэлектрика (F0 = 0,04 x specularFactor); цвет отражения и текстуры движок не читает
        specular = extensions.get('KHR_materials_specular')
        if specular is not None:
            params['SpecularFactor'] = '%g' % specular.get('specularFactor', 1.0)
            for key in ('specularTexture', 'specularColorTexture', 'specularColorFactor'):
                if key in specular and specular[key] != [1.0, 1.0, 1.0]:
                    warn('material %s: KHR_materials_specular %s is ignored' % (name, key))

        ignored = sorted(set(extensions) - {'KHR_materials_emissive_strength', 'KHR_materials_diffuse_transmission',
                                            'KHR_materials_specular', 'KHR_materials_pbrSpecularGlossiness'})
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
        compensate_transmission(name, params, textures)
        return '%s/%s' % (self.asset, sanitize(name)), params

    def from_specular_glossiness(self, glossiness, name):
        """KHR_materials_pbrSpecularGlossiness (архивное расширение, так экспортирует Sketchfab) → metallic-roughness, как
        его перевод в спецификации glTF для диэлектриков: диффузный цвет — базовый, металличность 0, шероховатость —
        1 − глянец, сила отражения — F0 / 0,04 (KHR_materials_specular). Цвет отражения и карта specularGlossiness не
        переносятся."""
        pbr = {'baseColorFactor': glossiness.get('diffuseFactor', [1.0, 1.0, 1.0, 1.0]), 'metallicFactor': 0.0,
               'roughnessFactor': 1.0 - glossiness.get('glossinessFactor', 1.0)}
        if 'diffuseTexture' in glossiness:
            pbr['baseColorTexture'] = glossiness['diffuseTexture']
        f0 = max(glossiness.get('specularFactor', [1.0, 1.0, 1.0]))
        pbr['specularFactor'] = min(f0 / 0.04, 1.0)
        if f0 > 0.04:
            warn('material %s: specular F0 %g is above a dielectric (0.04), clamped' % (name, f0))
        if 'specularGlossinessTexture' in glossiness:
            warn('material %s: specularGlossinessTexture is ignored, glossiness is the factor only' % name)
        return pbr


_compensated = set()   # материалы, о поправке которых уже сказано (convert зовётся на каждую секцию)


def compensate_transmission(name, params, textures):
    """Пропускание с цветом просвета из той же текстуры, что базовый цвет (DiffuseTransmissionColor = BaseColor: трава и
    цветы Poly Haven, свои пучки из Blender). Фото — отражённый цвет, а движок, как KHR_materials_diffuse_transmission,
    отражает базовый цвет x (1 - t) — растение темнело бы на долю пропускания. BaseColorFactor x 1 / (1 - t): отражённое — как
    на фото, прошедшее — t x цвет просвета (как Two Sided Foliage в UE). Отдельная карта просвета (Megascans) — уже учтено в
    export_megascans.py (альбедо x 1 / (1 - t)), здесь не трогается."""
    base = textures.get('BaseColor')
    share = float(params.get('DiffuseTransmissionFactor', '0'))
    if not base or params.get('DiffuseTransmissionColor') != base or not 0.0 < share < 1.0:
        return
    factor = [float(v) for v in params['BaseColorFactor'].split(',')]
    factor[:3] = [v / (1.0 - share) for v in factor[:3]]
    params['BaseColorFactor'] = ','.join('%g' % v for v in factor)
    if name in _compensated:
        return
    _compensated.add(name)
    print('material %s: BaseColorFactor x %.3g - the reflected share of the base color photo stays as photographed '
          '(diffuse transmission %g)' % (name, 1.0 / (1.0 - share), share))


# ---------------------------------------------------------------------------------------------------------------------
# Разбор сцены на модели

# Простая коллизия модели (Models.collision, DMModel::Collision)
COLLISIONS = ('hull', 'box', 'mesh', 'capsule', 'none')

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
        spec = {'base': base, 'lods': [lods[lod][0] for lod in order], 'bases': bases, 'instances': [placement],
                'collision': lods[order[0]][0].get('extras', {}).get('collision'),
                'collision_radius': lods[order[0]][0].get('extras', {}).get('collision_radius')}
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
            models.append({'name': base, 'lods': lods, 'instances': spec['instances'], 'collision': spec['collision'],
                           'collision_radius': spec['collision_radius']})
    return models


# ---------------------------------------------------------------------------------------------------------------------
# Модель со скелетом: скин, кости, клипы (как Skeletal Mesh, Skeleton и AnimSequence в UE)

CLIP_RATE = 30.0                       # кадров в секунду в файле клипа
ROOT_MOTION_THRESHOLD = 0.05           # кость корня — первая, что уходит по горизонтали дальше, м
LOOP_TOLERANCE = 0.02                  # клип зациклен, если кости в первом и последнем кадре ближе, м
# Поворот на 180° вокруг вертикали в координатах glTF: с ним и зеркалом по Z перед модели glTF (+Z) — перед движка (+Z, взгляд
# 0 у Locomotion); вместе это зеркало по X
TURN = np.diag([-1.0, 1.0, -1.0, 1.0])
MIRROR_X = np.array([-1.0, 1.0, 1.0])


def quaternion_matrices(q):
    """Кватернионы x, y, z, w (N×4) → матрицы поворота N×3×3 (точка — столбец)."""
    q = q / np.linalg.norm(q, axis=1, keepdims=True)
    x, y, z, w = q[:, 0], q[:, 1], q[:, 2], q[:, 3]
    return np.stack([
        np.stack([1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)], axis=1),
        np.stack([2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)], axis=1),
        np.stack([2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)], axis=1)], axis=1)


def trs_matrices(t, q, s):
    m = np.zeros((len(t), 4, 4))
    m[:, :3, :3] = quaternion_matrices(q) * s[:, None, :]
    m[:, :3, 3] = t
    m[:, 3, 3] = 1.0
    return m


def matrix_quaternions(r):
    """Матрицы поворота N×3×3 → кватернионы x, y, z, w (как quaternion(), но пачкой)."""
    n = len(r)
    q = np.zeros((n, 4))
    trace = r[:, 0, 0] + r[:, 1, 1] + r[:, 2, 2]
    c0 = trace > 0.0
    c1 = ~c0 & (r[:, 0, 0] > r[:, 1, 1]) & (r[:, 0, 0] > r[:, 2, 2])
    c2 = ~c0 & ~c1 & (r[:, 1, 1] > r[:, 2, 2])
    c3 = ~(c0 | c1 | c2)
    s = 2.0 * np.sqrt(np.maximum(trace[c0] + 1.0, 1e-12))
    q[c0] = np.stack([(r[c0, 2, 1] - r[c0, 1, 2]) / s, (r[c0, 0, 2] - r[c0, 2, 0]) / s, (r[c0, 1, 0] - r[c0, 0, 1]) / s,
                      0.25 * s], axis=1)
    s = 2.0 * np.sqrt(np.maximum(1.0 + r[c1, 0, 0] - r[c1, 1, 1] - r[c1, 2, 2], 1e-12))
    q[c1] = np.stack([0.25 * s, (r[c1, 0, 1] + r[c1, 1, 0]) / s, (r[c1, 0, 2] + r[c1, 2, 0]) / s,
                      (r[c1, 2, 1] - r[c1, 1, 2]) / s], axis=1)
    s = 2.0 * np.sqrt(np.maximum(1.0 + r[c2, 1, 1] - r[c2, 0, 0] - r[c2, 2, 2], 1e-12))
    q[c2] = np.stack([(r[c2, 0, 1] + r[c2, 1, 0]) / s, 0.25 * s, (r[c2, 1, 2] + r[c2, 2, 1]) / s,
                      (r[c2, 0, 2] - r[c2, 2, 0]) / s], axis=1)
    s = 2.0 * np.sqrt(np.maximum(1.0 + r[c3, 2, 2] - r[c3, 0, 0] - r[c3, 1, 1], 1e-12))
    q[c3] = np.stack([(r[c3, 0, 2] + r[c3, 2, 0]) / s, (r[c3, 1, 2] + r[c3, 2, 1]) / s, 0.25 * s,
                      (r[c3, 1, 0] - r[c3, 0, 1]) / s], axis=1)
    return q / np.linalg.norm(q, axis=1, keepdims=True)


def matrices_trs(m, label):
    """Матрицы N×4×4 → перемещение, поворот, масштаб (без смены системы координат)."""
    t = m[:, :3, 3].copy()
    linear = m[:, :3, :3]
    s = np.linalg.norm(linear, axis=1)          # длины столбцов
    r = linear / np.maximum(s[:, None, :], 1e-12)
    mirrored = np.linalg.det(r) < 0.0
    if mirrored.any():
        warn('%s: a mirrored joint transform, X scale is negated' % label)
        s[mirrored, 0] = -s[mirrored, 0]
        r[mirrored, :, 0] = -r[mirrored, :, 0]
    if not np.allclose(np.einsum('nji,njk->nik', r, r), np.eye(3), atol=1e-3):
        warn('%s: a sheared joint transform (non-uniform scale under rotation) is approximated' % label)
    return t, matrix_quaternions(r), s


def to_engine(t, q, s):
    """Место кости в координатах glTF → движка: поворот на 180° вокруг вертикали и зеркало по Z — вместе зеркало по X
    (TURN, MIRROR): у перемещения минус x, у кватерниона — минус y и z."""
    return t * MIRROR_X, q * np.array([1.0, -1.0, -1.0, 1.0]), s


def node_trs(node):
    """Поза узла glTF: перемещение, поворот, масштаб (из matrix — разложением)."""
    if 'matrix' in node:
        t, q, s = matrices_trs(node_matrix(node)[None], node.get('name', 'node'))
        return t[0], q[0], s[0]
    return (np.array(node.get('translation', [0.0, 0.0, 0.0]), dtype=np.float64),
            np.array(node.get('rotation', [0.0, 0.0, 0.0, 1.0]), dtype=np.float64),
            np.array(node.get('scale', [1.0, 1.0, 1.0]), dtype=np.float64))


def slerp(a, b, f):
    dot = np.sum(a * b, axis=1)
    b = np.where(dot[:, None] < 0.0, -b, b)
    dot = np.abs(dot)
    theta = np.arccos(np.clip(dot, -1.0, 1.0))
    sin = np.sin(theta)
    small = sin < 1e-6
    safe = np.where(small, 1.0, sin)
    wa = np.where(small, 1.0 - f, np.sin((1.0 - f) * theta) / safe)
    wb = np.where(small, f, np.sin(f * theta) / safe)
    q = wa[:, None] * a + wb[:, None] * b
    return q / np.linalg.norm(q, axis=1, keepdims=True)


_cubic_warned = set()


def sample_channel(gltf, sampler, path, times, label):
    """Канал анимации glTF в моменты times: LINEAR (поворот — slerp), STEP; у CUBICSPLINE — значения ключей линейно."""
    keys = gltf.accessor(sampler['input']).astype(np.float64)[:, 0]
    values = gltf.accessor(sampler['output']).astype(np.float64)
    interpolation = sampler.get('interpolation', 'LINEAR')
    if interpolation == 'CUBICSPLINE':
        values = values.reshape(len(keys), 3, -1)[:, 1, :]
        if label not in _cubic_warned:
            _cubic_warned.add(label)
            warn('%s: CUBICSPLINE channels are sampled linearly between keys' % label)
    if len(keys) == 1:
        return np.repeat(values[:1], len(times), axis=0)
    index = np.clip(np.searchsorted(keys, times, side='right') - 1, 0, len(keys) - 2)
    span = np.maximum(keys[index + 1] - keys[index], 1e-12)
    f = np.clip((times - keys[index]) / span, 0.0, 1.0)
    if interpolation == 'STEP':
        f = (times >= keys[index + 1]).astype(np.float64)
    a, b = values[index], values[index + 1]
    if path == 'rotation':
        return slerp(a, b, f)
    return a + (b - a) * f[:, None]


def collect_skinned_model(gltf, materials, name, scale):
    """Модель со скелетом skins[0]: сетки со скином и сетки на костях (рога на голове) — одна модель, секции по материалам,
    LOD — по суффиксу _LOD<N> имени узла. Вершины — в позе покоя в пространстве модели: общая матрица привязки (поза
    покоя кости × обратная привязка) уходит в вершины, узлы над скелетом и --scale — в корневые кости."""
    nodes = gltf.json['nodes']
    skins = gltf.json['skins']
    if len(skins) > 1:
        warn('%d skins in the file, only skin 0 is imported' % len(skins))
    skin = skins[0]
    world = {index: matrix for index, _, matrix in scene_nodes(gltf)}
    parent = {child: i for i, node in enumerate(nodes) for child in node.get('children', [])}
    scale_matrix = np.diag([scale, scale, scale, 1.0])

    def model_world(index):
        return scale_matrix @ world[index]

    skin_joints = list(skin['joints'])
    joint_set = set(skin_joints)

    def parent_joint(index):
        p = parent.get(index)
        while p is not None and p not in joint_set:
            p = parent.get(p)
        return p

    def depth(index):
        d = 0
        while index in parent:
            index = parent[index]
            d += 1
        return d

    # Кости движка — родитель раньше потомка (по глубине в дереве узлов), номер в весах — по этому порядку
    order = sorted(skin_joints, key=lambda j: (depth(j), skin_joints.index(j)))
    engine_index = {node: k for k, node in enumerate(order)}
    remap = np.array([engine_index[j] for j in skin_joints])
    parents = [engine_index[parent_joint(j)] if parent_joint(j) is not None else -1 for j in order]
    # Неподвижная часть пути к родителю: узлы между костью и родительской костью, у корня — узлы над скелетом и --scale
    pre = []
    for j in order:
        pj = parent_joint(j)
        above = model_world(parent[j]) if j in parent else scale_matrix
        pre.append(np.linalg.inv(model_world(pj)) @ above if pj is not None else above)
        if j in parent and pj is not None and parent[j] != pj:
            warn('joint %s: nodes between it and its parent joint are taken as fixed' % nodes[j].get('name', j))

    rest = [to_engine(*matrices_trs((pre[k] @ node_matrix(nodes[j]))[None], nodes[j].get('name', '')))
            for k, j in enumerate(order)]
    rest_t = np.array([r[0][0] for r in rest])
    rest_q = np.array([r[1][0] for r in rest])
    rest_s = np.array([r[2][0] for r in rest])

    # Общая матрица привязки: поза покоя кости × обратная привязка — одна у всех костей с весами (так у экспортов FBX)
    if 'inverseBindMatrices' in skin:
        inverse_bind = gltf.accessor(skin['inverseBindMatrices']).astype(np.float64).reshape(-1, 4, 4).transpose(0, 2, 1)
    else:
        inverse_bind = np.repeat(np.eye(4)[None], len(skin_joints), axis=0)
    bind = np.array([model_world(j) @ inverse_bind[i] for i, j in enumerate(skin_joints)])

    skinned_nodes, rigid_nodes = [], []
    used = np.zeros(len(skin_joints), dtype=np.int64)
    for index, node, _ in mesh_nodes(gltf):
        if node.get('skin') == 0:
            skinned_nodes.append(index)
            for primitive in gltf.json['meshes'][node['mesh']]['primitives']:
                attributes = primitive['attributes']
                if 'JOINTS_0' in attributes and 'WEIGHTS_0' in attributes:
                    joints = gltf.accessor(attributes['JOINTS_0']).astype(np.int64)
                    weights = gltf.accessor(attributes['WEIGHTS_0']).astype(np.float64)[:, :4]
                    np.add.at(used, joints[weights > 0.0], 1)
        elif parent_joint(index) is not None:
            rigid_nodes.append(index)
        else:
            warn('mesh node %s has no skin and is not under a joint, skipped' % node.get('name', index))
    if not skinned_nodes and not rigid_nodes:
        raise SystemExit('error: no meshes use skin 0')
    common = bind[int(np.argmax(used))]
    differs = [nodes[skin_joints[i]].get('name', '') for i in np.nonzero(used)[0]
               if not np.allclose(bind[i], common, atol=1e-3 * max(1.0, np.abs(common).max()))]
    if differs:
        warn('the bind pose differs from the rest pose at joints %s: the mesh follows the most used joint'
             % ', '.join(differs[:5]))

    lod_pattern = re.compile(r'^(.*)_LOD(\d+)$', re.IGNORECASE)
    parts_by_lod = {}
    for index in skinned_nodes + rigid_nodes:
        node = nodes[index]
        match = lod_pattern.match(node.get('name', ''))
        lod = int(match.group(2)) if match else 0
        if index in skinned_nodes:
            matrix = TURN @ common
            skin_spec = ('weights', remap)
        else:
            matrix = TURN @ model_world(index)
            skin_spec = ('rigid', engine_index[parent_joint(index)])
        for p_index, primitive in enumerate(gltf.json['meshes'][node['mesh']]['primitives']):
            label = '%s %s primitive %d' % (name, node.get('name', index), p_index)
            if primitive.get('mode', TRIANGLES) != TRIANGLES:
                warn('%s: mode %d is not triangles, skipped' % (label, primitive['mode']))
                continue
            parts_by_lod.setdefault(lod, {}).setdefault(primitive.get('material'), []).append(
                convert_primitive(gltf, primitive, matrix[:3, :3], label, matrix[:3, 3], skin_spec))

    slots, lods = [], []
    for lod in sorted(parts_by_lod):
        parts = parts_by_lod[lod]
        for material_index in parts:
            if material_index not in slots:
                slots.append(material_index)
        sections = []
        for slot, material_index in enumerate(slots):
            if material_index in parts:
                instance, params = materials.convert(material_index)
                sections.append({'slot': slot, 'mesh': merge(parts[material_index]), 'instance': instance, 'params': params})
        lods.append(sections)
    skeleton = {'names': [nodes[j].get('name') or 'joint%d' % j for j in order], 'parents': parents, 'order': order,
                'pre': pre, 'rest': (rest_t, rest_q, rest_s)}
    return {'name': name, 'lods': lods, 'collision': None, 'collision_radius': None, 'skeleton': skeleton,
            'instances': [{'position': np.zeros(3), 'rotation': np.array([0.0, 0.0, 0.0, 1.0]), 'scale': np.ones(3)}]}


def globals_of(parents, t, q, s):
    """Места костей в пространстве модели по кадрам: F × J × 4 × 4 (родитель раньше потомка)."""
    frames, count = t.shape[:2]
    result = np.zeros((frames, count, 4, 4))
    for j in range(count):
        local = trs_matrices(t[:, j], q[:, j], s[:, j])
        result[:, j] = local if parents[j] < 0 else result[:, parents[j]] @ local
    return result


def yaw_rotation(angle):
    """Поворот вокруг вертикали на angle (+Z к +X), F × 4 × 4."""
    m = np.repeat(np.eye(4)[None], len(angle), axis=0)
    c, s = np.cos(angle), np.sin(angle)
    m[:, 0, 0], m[:, 0, 2], m[:, 2, 0], m[:, 2, 2] = c, s, -s, c
    return m


def collect_clips(gltf, skeleton):
    """Клипы glTF → кадры мест костей движка с частотой CLIP_RATE (пока с движением корня)."""
    nodes = gltf.json['nodes']
    order = skeleton['order']
    engine_index = {node: k for k, node in enumerate(order)}
    clips = []
    for a_index, animation in enumerate(gltf.json.get('animations', [])):
        clip_name = animation.get('name') or 'animation%d' % a_index
        channels = {}
        start, end = None, None
        for channel in animation['channels']:
            target = channel['target']
            if 'node' not in target or target['path'] == 'weights':
                continue
            sampler = animation['samplers'][channel['sampler']]
            keys = gltf.accessor(sampler['input'])[:, 0]
            start = keys[0] if start is None else min(start, keys[0])
            end = keys[-1] if end is None else max(end, keys[-1])
            if target['node'] not in engine_index:
                warn('clip %s: channel of node %s (not a joint) is ignored' % (clip_name,
                                                                                nodes[target['node']].get('name', '')))
                continue
            channels.setdefault(target['node'], {})[target['path']] = sampler
        if start is None:
            warn('clip %s has no joint channels, skipped' % clip_name)
            continue
        duration = float(end - start)
        frame_count = max(int(round(duration * CLIP_RATE)) + 1, 1)
        rate = (frame_count - 1) / duration if frame_count > 1 and duration > 0.0 else CLIP_RATE
        times = start + np.arange(frame_count) / rate

        t = np.zeros((frame_count, len(order), 3))
        q = np.zeros((frame_count, len(order), 4))
        s = np.zeros((frame_count, len(order), 3))
        for k, j in enumerate(order):
            rest_t, rest_q, rest_s = node_trs(nodes[j])
            sampled = {'translation': np.repeat(rest_t[None], frame_count, axis=0),
                       'rotation': np.repeat(rest_q[None], frame_count, axis=0),
                       'scale': np.repeat(rest_s[None], frame_count, axis=0)}
            for path, sampler in channels.get(j, {}).items():
                sampled[path] = sample_channel(gltf, sampler, path, times, clip_name)
            local = trs_matrices(sampled['translation'], sampled['rotation'], sampled['scale'])
            if not np.allclose(skeleton['pre'][k], np.eye(4), atol=1e-9):
                local = skeleton['pre'][k][None] @ local
            t[:, k], q[:, k], s[:, k] = to_engine(*matrices_trs(local, '%s %s' % (clip_name, nodes[j].get('name', ''))))
        # Кватернионы подряд — по одной стороне (q и −q — один поворот)
        for f in range(1, frame_count):
            flip = np.sum(q[f] * q[f - 1], axis=1) < 0.0
            q[f, flip] = -q[f, flip]
        clips.append({'name': clip_name, 'rate': rate, 't': t, 'q': q, 's': s})
    return clips


def find_root_motion_joint(skeleton, clips, name):
    """Кость корня: по имени (--root-motion-bone) или первая (от корня), что уходит по горизонтали дальше порога в каком-то
    клипе; None — клипы на месте."""
    names = skeleton['names']
    if name:
        if name not in names:
            raise SystemExit('error: --root-motion-bone %s: no such joint (%s)' % (name, ', '.join(names[:10])))
        return names.index(name)
    best = None
    for clip in clips:
        g = globals_of(skeleton['parents'], clip['t'], clip['q'], clip['s'])
        travel = np.linalg.norm((g[:, :, :3, 3] - g[:1, :, :3, 3])[:, :, [0, 2]], axis=2).max(axis=0)
        moving = np.nonzero(travel > ROOT_MOTION_THRESHOLD)[0]
        if len(moving) and (best is None or moving[0] < best):
            best = int(moving[0])
    return best


def extract_root_motion(skeleton, clip, root):
    """Движение корня из клипа (клип — на месте): сдвиг кости корня по горизонтали и её поворот вокруг вертикали от первого
    кадра убираются из позы и пишутся кривой (x, z, yaw по кадрам в пространстве модели); средние скорость по пути и
    поворот. Высота и наклоны корня остаются в позе."""
    frames = len(clip['t'])
    clip['motion'] = np.zeros((frames, 3))
    clip['speed'], clip['turn'] = 0.0, 0.0
    if root is None or frames < 2:
        return
    parents = skeleton['parents']
    g = globals_of(parents, clip['t'], clip['q'], clip['s'])
    root_global = g[:, root]
    position = root_global[:, :3, 3].copy()
    position[:, 1] = 0.0
    rotation = root_global[:, :3, :3] / np.linalg.norm(root_global[:, :3, :3], axis=1)[:, None, :]
    relative = matrix_quaternions(np.einsum('nij,kj->nik', rotation, rotation[0]))
    yaw = np.unwrap(2.0 * np.arctan2(relative[:, 1], relative[:, 3]))
    to_start = np.repeat(np.eye(4)[None], frames, axis=0)
    to_start[:, :3, 3] = -position[0]
    move = np.repeat(np.eye(4)[None], frames, axis=0)
    move[:, :3, 3] = position
    delta = move @ yaw_rotation(yaw) @ to_start
    in_place = np.linalg.inv(delta) @ root_global
    local = in_place if parents[root] < 0 else np.linalg.inv(g[:, parents[root]]) @ in_place
    t, q, s = matrices_trs(local, clip['name'])
    for f in range(1, frames):
        if np.dot(q[f], q[f - 1]) < 0.0:
            q[f] = -q[f]
    clip['t'][:, root], clip['q'][:, root], clip['s'][:, root] = t, q, s
    offset = position - position[0]
    clip['motion'] = np.stack([offset[:, 0], offset[:, 2], yaw], axis=1)
    duration = (frames - 1) / clip['rate']
    clip['speed'] = float(np.linalg.norm(np.diff(offset[:, [0, 2]], axis=0), axis=1).sum() / duration)
    clip['turn'] = float(yaw[-1] / duration)


def skinned_bounds(skeleton, clip, vertices, skin):
    """Границы модели по кадрам клипа (каждый второй и последний): скиннинг вершин, как в шейдере."""
    rest = globals_of(skeleton['parents'], *[c[None] for c in skeleton['rest']])[0]
    inverse_bind = np.linalg.inv(rest)
    frames = sorted(set(range(0, len(clip['t']), 2)) | {len(clip['t']) - 1})
    g = globals_of(skeleton['parents'], clip['t'][frames], clip['q'][frames], clip['s'][frames])
    joints = skin[:, :4].astype(np.int64)
    weights = skin[:, 4:].astype(np.float64) / 255.0
    points = np.hstack([vertices, np.ones((len(vertices), 1))])
    lo, hi = np.full(3, np.inf), np.full(3, -np.inf)
    for f in range(len(frames)):
        palette = g[f] @ inverse_bind
        p = np.zeros((len(vertices), 4))
        for k in range(4):
            p += weights[:, k:k + 1] * np.einsum('nij,nj->ni', palette[joints[:, k]], points)
        lo, hi = np.minimum(lo, p[:, :3].min(axis=0)), np.maximum(hi, p[:, :3].max(axis=0))
    return lo, hi


def is_looping(skeleton, clip):
    if len(clip['t']) < 2:
        return False
    g = globals_of(skeleton['parents'], clip['t'][[0, -1]], clip['q'][[0, -1]], clip['s'][[0, -1]])
    return bool(np.abs(g[0, :, :3, 3] - g[1, :, :3, 3]).max() < LOOP_TOLERANCE)


def skeleton_bytes(skeleton):
    t, q, s = skeleton['rest']
    data = b'DMSK' + struct.pack('<II', 1, len(skeleton['names']))
    for k, name in enumerate(skeleton['names']):
        encoded = name.encode('utf-8')
        data += struct.pack('<I', len(encoded)) + encoded + struct.pack('<i', skeleton['parents'][k])
        data += np.hstack([t[k], q[k], s[k]]).astype('<f4').tobytes()
    return data


def clip_bytes(clip):
    frames, joints = clip['t'].shape[:2]
    data = b'DMAN' + struct.pack('<IIIfIff', 1, joints, frames, clip['rate'], 1 if clip['looping'] else 0,
                                 clip['speed'], clip['turn'])
    data += np.hstack([clip['bounds'][0], clip['bounds'][1]]).astype('<f4').tobytes()
    data += np.concatenate([clip['t'], clip['q'], clip['s']], axis=2).astype('<f4').tobytes()
    data += clip['motion'].astype('<f4').tobytes()
    return data


def parse_locomotion(text):
    """--locomotion: «клип[=скорость],…» → {имя: скорость или None (по движению корня)}."""
    points = {}
    for item in [v.strip() for v in text.split(',') if v.strip()]:
        name, _, speed = item.partition('=')
        points[name.strip()] = float(speed) if speed else None
    return points


# ---------------------------------------------------------------------------------------------------------------------
# Запись в base.db3

class Database:
    def __init__(self, path, material_id):
        self.connection = sqlite3.connect(path)
        self.cursor = self.connection.cursor()
        self.material_id = material_id
        self.definitions_by_material = {}
        self.definitions(material_id)

    def definitions(self, material_id):
        """Параметры материала: имя → id определения"""
        if material_id not in self.definitions_by_material:
            definitions = dict(self.cursor.execute(
                'SELECT param_name, id_parameter_def FROM MaterialParameterDef WHERE id_material = ?', (material_id,)))
            if not definitions:
                raise SystemExit('error: material %d has no parameters in MaterialParameterDef' % material_id)
            self.definitions_by_material[material_id] = definitions
        return self.definitions_by_material[material_id]

    def one(self, sql, args=()):
        row = self.cursor.execute(sql, args).fetchone()
        return row[0] if row else None

    def texture(self, name, file, srgb, coverage, compression):
        texture_id = self.one('SELECT id FROM Textures WHERE name = ?', (name,))
        if texture_id is None:
            self.cursor.execute('INSERT INTO Textures (name, file, generate_mipmap, sRGB, preserve_alpha_coverage, compression) '
                                'VALUES (?, ?, 1, ?, ?, ?)', (name, file, int(srgb), coverage, compression))
            return self.cursor.lastrowid
        self.cursor.execute('UPDATE Textures SET file = ?, generate_mipmap = 1, sRGB = ?, preserve_alpha_coverage = ?, '
                            'compression = ? WHERE id = ?', (file, int(srgb), coverage, compression, texture_id))
        return texture_id

    def instance(self, name, params, texture_ids, material_id=None):
        material_id = material_id or self.material_id
        definitions = self.definitions(material_id)
        instance_id = self.one('SELECT id_instance FROM MaterialInstance WHERE name = ? AND id_material = ?',
                               (name, material_id))
        if instance_id is None:
            self.cursor.execute('INSERT INTO MaterialInstance (id_material, name) VALUES (?, ?)', (material_id, name))
            instance_id = self.cursor.lastrowid
        self.cursor.execute('DELETE FROM MaterialParameterInstance WHERE id_instance = ?', (instance_id,))
        for param, value in params.items():
            if param not in definitions:
                raise SystemExit('error: material %d has no parameter %s' % (material_id, param))
            value = str(texture_ids[value]) if value in texture_ids else value
            self.cursor.execute('INSERT INTO MaterialParameterInstance (id_instance, id_material_def, value) '
                                'VALUES (?, ?, ?)', (instance_id, definitions[param], value))
        return instance_id

    def instance_material(self, instance_id):
        """Материал секции — материал её экземпляра (PBR, PBRInstance или PBRTree): по нему выбирается шейдер"""
        return self.one('SELECT id_material FROM MaterialInstance WHERE id_instance = ?', (instance_id,))

    def remove_replaced_instances(self, instances):
        """Экземпляры с теми же именами у другого материала (например, у меша появились данные ветра — стал PBRTree),
        на которые больше не ссылается ни одна секция модели"""
        for name, instance_id in instances.items():
            for (old,) in self.cursor.execute('SELECT id_instance FROM MaterialInstance WHERE name = ? AND id_instance != ?',
                                              (name, instance_id)).fetchall():
                if self.one('SELECT COUNT(*) FROM ModelProperties WHERE material_instance_id = ?', (old,)) == 0:
                    self.cursor.execute('DELETE FROM MaterialParameterInstance WHERE id_instance = ?', (old,))
                    self.cursor.execute('DELETE FROM MaterialInstance WHERE id_instance = ?', (old,))
                    print('Material instance %s: the old one (id %d) is replaced and removed' % (name, old))

    def mesh(self, name, file):
        mesh_id = self.one('SELECT id FROM Meshes WHERE name = ?', (name,))
        if mesh_id is None:
            self.cursor.execute('INSERT INTO Meshes (name, file, primitive) VALUES (?, ?, ?)',
                                (name, file, FALLBACK_PRIMITIVE))
            return self.cursor.lastrowid
        self.cursor.execute('UPDATE Meshes SET file = ?, primitive = ? WHERE id = ?', (file, FALLBACK_PRIMITIVE, mesh_id))
        return mesh_id

    def model(self, name, lods, render, shadow_lod_scale=None, shadow_min_lod=None, collision=None, collision_radius=None):
        """lods — по LOD: ([(секция, id меша, id экземпляра материала)], дальность); строка ModelProperties — секция.
        shadow_lod_scale, shadow_min_lod — Models.shadow_lod_scale и shadow_min_lod (None — не менять, у новой модели
        1 и 0); collision — Models.collision (None — не менять)"""
        model_id = self.one('SELECT id FROM Models WHERE name = ?', (name,))
        if model_id is None:
            self.cursor.execute('INSERT INTO Models (name) VALUES (?)', (name,))
            model_id = self.cursor.lastrowid
        if shadow_lod_scale is not None:
            self.cursor.execute('UPDATE Models SET shadow_lod_scale = ? WHERE id = ?', (shadow_lod_scale, model_id))
        if shadow_min_lod is not None:
            self.cursor.execute('UPDATE Models SET shadow_min_lod = ? WHERE id = ?',
                                (min(shadow_min_lod, len(lods) - 1), model_id))
        if collision is not None:
            self.cursor.execute('UPDATE Models SET collision = ? WHERE id = ?', (collision, model_id))
        if collision_radius is not None:
            self.cursor.execute('UPDATE Models SET collision_radius = ? WHERE id = ?', (collision_radius, model_id))
        old_meshes = [r[0] for r in self.cursor.execute('SELECT mesh_id FROM ModelProperties WHERE model_id = ?',
                                                        (model_id,))]
        self.cursor.execute('DELETE FROM ModelProperties WHERE model_id = ?', (model_id,))
        new_meshes = set()
        for lod, (sections, lod_range) in enumerate(lods):
            for section, mesh_id, instance_id in sections:
                self.cursor.execute('INSERT INTO ModelProperties (lod, section, model_id, range, material_id, mesh_id, '
                                    'render, material_instance_id) VALUES (?, ?, ?, ?, ?, ?, ?, ?)',
                                    (lod, section, model_id, lod_range, self.instance_material(instance_id), mesh_id, render,
                                     instance_id))
                new_meshes.add(mesh_id)
        # Меши LOD и секций, которых больше нет (reimport с меньшим числом LOD), — если на них никто не ссылается
        for mesh_id in set(old_meshes) - new_meshes:
            if self.one('SELECT COUNT(*) FROM ModelProperties WHERE mesh_id = ?', (mesh_id,)) == 0:
                self.cursor.execute('DELETE FROM Meshes WHERE id = ?', (mesh_id,))
        return model_id

    def light(self, level_id, light):
        """Источник уровня по имени: есть строка — обновляются тип, цвет, интенсивность, место, направление, радиус и
        конус (тени, включён и атмосфера — как настроили на уровне); нет — новая строка. Возвращает (id, новая ли)."""
        def text(vector):
            return ','.join('%g' % (round(float(c), 6) + 0.0) for c in vector)
        values = (light['type'], text(light['color']), light['intensity'], text(light['position']), text(light['direction']),
                  light['range'], round(float(light['inner']), 4), round(float(light['outer']), 4))
        light_id = self.one('SELECT id FROM LevelLights WHERE level = ? AND name = ?', (level_id, light['name']))
        if light_id is not None:
            self.cursor.execute('UPDATE LevelLights SET type = ?, color = ?, intensity = ?, position = ?, direction = ?, '
                                'attenuation_radius = ?, inner_cone_angle = ?, outer_cone_angle = ? WHERE id = ?',
                                values + (light_id,))
            return light_id, False
        # Солнце уровня — первый направленный: новый направленный при солнце — без атмосферы и теней
        sun = light['type'] == 'directional' and self.one(
            "SELECT COUNT(*) FROM LevelLights WHERE level = ? AND type = 'directional'", (level_id,)) == 0
        self.cursor.execute('INSERT INTO LevelLights (level, name, type, color, intensity, position, direction, '
                            'attenuation_radius, inner_cone_angle, outer_cone_angle, cast_shadows, atmosphere_sun_light) '
                            'VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)',
                            (level_id, light['name']) + values + (int(sun), int(sun)))
        return self.cursor.lastrowid, True

    def place(self, level_id, model_id, instances, animation_id=None):
        """Экземпляры модели на уровне: повторный импорт заменяет прежние строки этой модели. animation_id — клип, который
        играют экземпляры модели со скелетом (LevelModels.animation_id)."""
        def text(vector, digits):
            return ','.join('%g' % (round(float(c), digits) + 0.0) for c in vector)
        self.cursor.execute('DELETE FROM LevelModels WHERE level = ? AND model = ?', (level_id, model_id))
        for instance in instances:
            self.cursor.execute('INSERT INTO LevelModels (level, model, position, rotation, scale, animation_id) '
                                'VALUES (?, ?, ?, ?, ?, ?)',
                                (level_id, model_id, text(instance['position'], 4), text(instance['rotation'], 6),
                                 text(instance['scale'], 4), animation_id))

    def skeleton(self, model_id, name, file):
        """Скелет модели (Skeletons, Models.skeleton_id) по имени модели."""
        skeleton_id = self.one('SELECT id FROM Skeletons WHERE name = ?', (name,))
        if skeleton_id is None:
            self.cursor.execute('INSERT INTO Skeletons (name, file) VALUES (?, ?)', (name, file))
            skeleton_id = self.cursor.lastrowid
        else:
            self.cursor.execute('UPDATE Skeletons SET file = ? WHERE id = ?', (file, skeleton_id))
        self.cursor.execute('UPDATE Models SET skeleton_id = ? WHERE id = ?', (skeleton_id, model_id))
        return skeleton_id

    def animations(self, skeleton_id, clips, locomotion):
        """Клипы скелета (Animations) по имени: файлы и точки шага по скорости. locomotion — {имя: скорость} (None —
        оставить прежние). Клипы, которых больше нет, удаляются; экземпляры уровня с ними — без клипа. Возвращает {имя: id}."""
        old = {name: (clip_id, speed) for clip_id, name, speed in self.cursor.execute(
            'SELECT id, name, locomotion_speed FROM Animations WHERE skeleton_id = ?', (skeleton_id,)).fetchall()}
        ids = {}
        for clip in clips:
            if locomotion is None:
                speed = old[clip['name']][1] if clip['name'] in old else None
            elif clip['name'] in locomotion:
                speed = locomotion[clip['name']] if locomotion[clip['name']] is not None else clip['speed']
            else:
                speed = None
            if clip['name'] in old:
                clip_id = old[clip['name']][0]
                self.cursor.execute('UPDATE Animations SET file = ?, locomotion_speed = ? WHERE id = ?',
                                    (clip['file'], speed, clip_id))
            else:
                self.cursor.execute('INSERT INTO Animations (name, skeleton_id, file, locomotion_speed) VALUES (?, ?, ?, ?)',
                                    (clip['name'], skeleton_id, clip['file'], speed))
                clip_id = self.cursor.lastrowid
            ids[clip['name']] = clip_id
        for name, (clip_id, _) in old.items():
            if name not in ids:
                self.cursor.execute('UPDATE LevelModels SET animation_id = NULL WHERE animation_id = ?', (clip_id,))
                self.cursor.execute('DELETE FROM Animations WHERE id = ?', (clip_id,))
                print('Clip %s: no longer in the file, removed' % name)
        return ids


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
    parser.add_argument('--shadow-lod-scale', type=float,
                        help='LOD distances in shadow views are multiplied by it, e.g. 0.5: a coarser LOD casts the shadow '
                             'closer (Models.shadow_lod_scale; default: keep, 1 for a new model)')
    parser.add_argument('--shadow-min-lod', type=int,
                        help='shadow views draw at most this detailed LOD, e.g. 2: grass casts its shadow with the '
                             'coarsest LOD (Models.shadow_min_lod; default: keep, 0 for a new model)')
    parser.add_argument('--collision', choices=COLLISIONS,
                        help='simple collision of the models (Models.collision): hull of LOD0 vertices, box of its bounds, '
                             'mesh of its triangles, capsule - a trunk, none; default: keep, extras "collision" of the '
                             'LOD0 object, hull for a new model')
    parser.add_argument('--collision-radius', type=float,
                        help='trunk radius of a capsule collision, m in model units (Models.collision_radius); default: keep, '
                             'extras "collision_radius" of the LOD0 object, the base of the model for a new one')
    parser.add_argument('--animation', help='model with a skeleton: the clip its level instances play '
                                            '(LevelModels.animation_id); default: none, the rest pose')
    parser.add_argument('--locomotion',
                        help='model with a skeleton: clips of the locomotion blend space by speed, e.g. '
                             '"Stand_Breathing_01,WalkSlow,Walk,Trot,Run" (Animations.locomotion_speed); the speed of a '
                             'clip is its root motion, "name=speed" sets it (in-place clips); default: keep')
    parser.add_argument('--root-motion-bone',
                        help='model with a skeleton: the joint whose horizontal move and turn are taken out of the clips '
                             'as root motion; default: the first joint from the root that moves')
    parser.add_argument('--scatter', action='store_true', help='model for scatter layers: PBRInstance, render = 0')
    parser.add_argument('--dry-run', action='store_true', help='print what would be written, write nothing')
    parser.add_argument('--db', default=os.path.join(ROOT, 'base.db3'))
    args = parser.parse_args()

    if args.scatter and args.level:
        raise SystemExit('error: --scatter models are placed by scatter layers, not by --level')
    if args.snap_to_terrain and not args.level:
        raise SystemExit('error: --snap-to-terrain needs --level')
    if args.shadow_lod_scale is not None and not 0.1 <= args.shadow_lod_scale <= 1.0:
        raise SystemExit('error: --shadow-lod-scale expects a value in 0.1..1')
    if args.shadow_min_lod is not None and args.shadow_min_lod < 0:
        raise SystemExit('error: --shadow-min-lod expects a LOD number >= 0')

    gltf = Gltf(args.file)
    asset = sanitize(args.asset or os.path.splitext(os.path.basename(args.file))[0])
    materials = MaterialConverter(gltf, asset)
    skinned = bool(gltf.json.get('skins')) and not args.scatter
    clips = []
    if skinned:
        # Модель со скелетом — одна: сетки со скином и на костях; --scale запекается в скелет и вершины
        model = collect_skinned_model(gltf, materials, sanitize(args.name or asset), args.scale)
        models = [model]
        skeleton = model['skeleton']
        clips = collect_clips(gltf, skeleton)
        root = find_root_motion_joint(skeleton, clips, args.root_motion_bone)
        sections = model['lods'][0]
        vertices = np.concatenate([section['mesh'][0] for section in sections])
        skin = np.concatenate([section['mesh'][7] for section in sections])
        for clip in clips:
            extract_root_motion(skeleton, clip, root)
            clip['looping'] = is_looping(skeleton, clip)
            clip['bounds'] = skinned_bounds(skeleton, clip, vertices, skin)
            clip['file'] = 'models\\%s\\%s_%s.anim' % (asset, model['name'], sanitize(clip['name']))
        print('Skeleton %s: %d joints, root motion: %s' % (model['name'], len(skeleton['names']),
                                                            skeleton['names'][root] if root is not None else 'none'))
        for clip in clips:
            print('  clip %-32s %6.2f s %4d frames %s speed %5.2f m/s turn %6.1f deg/s' % (
                clip['name'], (len(clip['t']) - 1) / clip['rate'], len(clip['t']), 'loop' if clip['looping'] else 'once',
                clip['speed'], np.degrees(clip['turn'])))
        names = {clip['name'] for clip in clips}
        locomotion = parse_locomotion(args.locomotion) if args.locomotion is not None else None
        for name in ([args.animation] if args.animation else []) + list(locomotion or []):
            if name not in names:
                raise SystemExit('error: clip %s is not in the file' % name)
    else:
        if args.animation or args.locomotion or args.root_motion_bone:
            raise SystemExit('error: --animation, --locomotion and --root-motion-bone need a model with a skeleton')
        models = collect_models(gltf, asset, materials, args.name, args.scatter, args.scale)
    lights = collect_lights(gltf)
    if not models and not lights:
        raise SystemExit('error: no meshes and no lights in %s' % args.file)
    if lights and not args.level:
        warn('%d lights are skipped: lights are level data, import them with --level' % len(lights))
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
        texture_ids[texture['name']] = db.texture(texture['name'], texture['file'], texture['srgb'], texture['coverage'],
                                                  texture['compression'])
        files[os.path.join('Textures', texture['file'])] = texture['data']
        coverage = ', alpha coverage %g' % texture['coverage'] if texture['coverage'] is not None else ''
        print('Texture %s -> Textures\\%s (%s, %s%s)' % (texture['name'], texture['file'],
                                                          'sRGB' if texture['srgb'] else 'linear', texture['compression'], coverage))

    # Экземпляр материала, у сеток которого есть данные ветра дерева, — PBRTree
    tree_instances = {section['instance'] for model in models for sections in model['lods'] for section in sections
                      if section['mesh'][6] is not None}
    if tree_instances and args.scatter:
        warn('tree wind data (_WIND_*) is kept in the mesh files, but scatter layers have no tree wind variant: '
             'PBRInstance bends like grass (WindWeight)')
    instances = {}
    for model in models:
        ranges = lod_ranges(len(model['lods']), args.lod_ranges)
        lods = []
        for lod, sections in enumerate(model['lods']):
            rows = []
            for section in sections:
                if section['instance'] not in instances:
                    tree = section['instance'] in tree_instances and not args.scatter
                    instances[section['instance']] = db.instance(section['instance'], section['params'], texture_ids,
                                                                 PBR_TREE_MATERIAL if tree else None)
                # Секция 0 — прежнее имя меша LOD: повторный импорт модели из одного материала ничего не переименует
                mesh_name = '%s_LOD%d' % (model['name'], lod) + ('_S%d' % section['slot'] if section['slot'] else '')
                mesh_file = 'models\\%s\\%s.bin' % (asset, mesh_name)
                rows.append((section['slot'], db.mesh(mesh_name, mesh_file), instances[section['instance']]))
                files[os.path.join('Meshes', mesh_file)] = mesh_bytes(section['mesh'])
            lods.append((rows, ranges[lod]))
        collision = args.collision or model['collision']
        if collision is not None and collision not in COLLISIONS:
            raise SystemExit('error: model %s: collision %r, expected one of %s' % (model['name'], collision, ', '.join(COLLISIONS)))
        collision_radius = args.collision_radius if args.collision_radius is not None else model['collision_radius']
        model_id = db.model(model['name'], lods, 0 if args.scatter else 1, args.shadow_lod_scale, args.shadow_min_lod,
                            collision, None if collision_radius is None else float(collision_radius))
        animation_id = None
        if 'skeleton' in model:
            skeleton_file = 'models\\%s\\%s.skel' % (asset, model['name'])
            files[os.path.join('Meshes', skeleton_file)] = skeleton_bytes(model['skeleton'])
            for clip in clips:
                files[os.path.join('Meshes', clip['file'])] = clip_bytes(clip)
            clip_ids = db.animations(db.skeleton(model_id, model['name'], skeleton_file), clips, locomotion)
            animation_id = clip_ids.get(args.animation)
            points = sorted((speed, name) for name, speed in db.cursor.execute(
                'SELECT name, locomotion_speed FROM Animations WHERE skeleton_id = (SELECT skeleton_id FROM Models WHERE '
                'id = ?) AND locomotion_speed IS NOT NULL', (model_id,)).fetchall())
            print('Locomotion of %s: %s' % (model['name'], ', '.join('%s %.2f m/s' % (n, s) for s, n in points) or 'none'))

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
            # Origin файла — в --position с равномерным масштабом --scale (у модели со скелетом он уже в вершинах)
            scale = 1.0 if 'skeleton' in model else args.scale
            placed = [{'position': position + instance['position'] * scale, 'rotation': instance['rotation'],
                       'scale': instance['scale'] * scale} for instance in model['instances']]
            if ground is not None:
                # Высота над нулём в Blender — над землёй: y --position не учитывается
                heights, cell = ground
                for instance, source in zip(placed, model['instances']):
                    x, _, z = instance['position']
                    instance['position'][1] = terrain.sample(heights, x, z, cell) + source['position'][1] * scale
            db.place(level_id, model_id, placed, animation_id)
            for instance in placed:
                print('  level %s: position %.2f,%.2f,%.2f rotation %.3f,%.3f,%.3f,%.3f scale %.3g,%.3g,%.3g' % (
                    (args.level,) + tuple(instance['position']) + tuple(instance['rotation']) +
                    tuple(instance['scale'])))

    if level_id is not None:
        for light in lights:
            # Как экземпляры: место относительно --position с масштабом --scale, с --snap-to-terrain — над землёй
            light['position'] = position + light['position'] * args.scale
            if ground is not None and light['type'] != 'directional':
                heights, cell = ground
                x, _, z = light['position']
                light['position'][1] = terrain.sample(heights, x, z, cell) + (light['position'][1] - position[1])
            light_id, new = db.light(level_id, light)
            details = ''
            if light['type'] != 'directional':
                details += ' position %.2f,%.2f,%.2f' % tuple(light['position'])
                details += ' range %g' % light['range'] if light['range'] > 0.0 else ' no range'
            if light['type'] != 'point':
                details += ' direction %.3f,%.3f,%.3f' % tuple(light['direction'])
            if light['type'] == 'spot':
                details += ' cone %.1f..%.1f deg' % (light['inner'], light['outer'])
            print('Light %s (%s, id %d, %s): %g %s, color %.3f,%.3f,%.3f%s' % (
                light['name'], light['type'], light_id, 'new' if new else 'updated', light['intensity'],
                'lx' if light['type'] == 'directional' else 'cd', *light['color'], details))

    db.remove_replaced_instances(instances)
    for name in sorted(tree_instances):
        if not args.scatter:
            print('Material instance %s: PBRTree (tree wind data in the meshes)' % name)

    if args.dry_run:
        db.connection.rollback()
        print('Dry run: nothing written (%d files, %d models, %d lights)' % (len(files), len(models),
                                                                           len(lights) if level_id is not None else 0))
        return

    for path, data in files.items():
        full = os.path.join(ROOT, path.replace('\\', os.sep))
        os.makedirs(os.path.dirname(full), exist_ok=True)
        with open(full, 'wb') as out:
            out.write(data)
    db.connection.commit()
    print('Imported %d models, %d lights, %d files%s' % (len(models), len(lights) if level_id is not None else 0, len(files),
                                                         ', %d warnings' % len(warnings) if warnings else ''))


if __name__ == '__main__':
    main()
