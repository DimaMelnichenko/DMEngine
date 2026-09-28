# Слой материала террейна из фото-текстур (Poly Haven и подобных): альбедо, нормаль, шероховатость и высота
# собираются в два файла формата слоя (TerrainMaterial, Textures\terrain\layers):
#   <имя>_albedo.dds — RGB альбедо в sRGB, как в фото (R8G8B8A8_UNORM_SRGB), A — высота для смешивания слоёв,
#                      растянутая на 0…1 по 1-му и 99-му процентилю (у разных материалов разный разброс disp-карт);
#   <имя>_normal.dds — RGB нормаль в соглашении DirectX (у карты OpenGL — nor_gl — зелёный канал переворачивается),
#                      A — шероховатость.
# Файлы — с полной цепочкой мипов (альбедо фильтруется в линейном): движок не строит их при загрузке.
# С --layer обновляет строку TerrainLayers террейна (имя, файлы, повтор в метрах). Картинки читает Blender (JPG, PNG,
# EXR), поэтому запуск — через него, из корня проекта. Файл внутри архива — «архив.zip:путь внутри»:
#   blender -b --factory-startup --python Tools/pack_terrain_layer.py -- --name leafy_grass
#       --albedo DownloadResources/leafy_grass_2k.gltf.zip:textures/leafy_grass_diff_2k.jpg
#       --normal DownloadResources/leafy_grass_2k.gltf.zip:textures/leafy_grass_nor_gl_2k.jpg --normal-convention gl
#       --roughness DownloadResources/leafy_grass_2k.gltf.zip:textures/leafy_grass_arm_2k.jpg:g
#       --height DownloadResources/polyhaven/leafy_grass_disp_2k.exr --layer 0 --tiling 2
# Шероховатость — «файл:канал» (r, g, b, a; по умолчанию r): у ARM Poly Haven — g. Размер — --size (2048).
import argparse
import os
import sqlite3
import sys
import tempfile
import zipfile

import bpy
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dds  # noqa: E402

LAYERS_DIR = os.path.join('Textures', 'terrain', 'layers')
CHANNELS = {'r': 0, 'g': 1, 'b': 2, 'a': 3}
_temp_dir = None


def resolve(path):
    """Путь к файлу; «архив.zip:путь внутри» распаковывается во временную папку"""
    global _temp_dir
    marker = path.lower().find('.zip:')
    if marker < 0:
        return path
    archive, member = path[:marker + 4], path[marker + 5:]
    if _temp_dir is None:
        _temp_dir = tempfile.mkdtemp(prefix='dm_terrain_')
    with zipfile.ZipFile(archive) as zf:
        return zf.extract(member, _temp_dir)


def load(path, size):
    """Картинка как массив высота × ширина × 4 (0…1 для 8-битных, как есть для EXR), строки сверху вниз"""
    image = bpy.data.images.load(os.path.abspath(resolve(path)), check_existing=False)
    # Без преобразований цвета: байты JPG / PNG — как в файле (альбедо остаётся в sRGB), EXR — как записан
    image.colorspace_settings.name = 'Non-Color'
    if tuple(image.size) != (size, size):
        print('resize', path, tuple(image.size), '->', size)
        image.scale(size, size)
    width, height = image.size
    pixels = np.empty(width * height * 4, dtype=np.float32)
    image.pixels.foreach_get(pixels)
    bpy.data.images.remove(image)
    return pixels.reshape(height, width, 4)[::-1]   # у Blender строки снизу вверх


def split_channel(spec, default):
    """«файл:канал» → (файл, номер канала); двоеточие диска (C:) каналом не считается"""
    head, sep, tail = spec.rpartition(':')
    if sep and tail.lower() in CHANNELS and head and not head.endswith(('.zip',)):
        return head, CHANNELS[tail.lower()]
    return spec, default


def normalized_height(values):
    low, high = np.percentile(values, [1.0, 99.0])
    if high - low < 1e-6:
        return np.full_like(values, 0.5)
    return np.clip((values - low) / (high - low), 0.0, 1.0)


def update_layer(terrain, layer, name, albedo_file, normal_file, tiling):
    db = sqlite3.connect('base.db3')
    try:
        row = db.execute('SELECT id FROM Terrain WHERE name = ?', (terrain,)).fetchone()
        if row is None:
            raise SystemExit('error: no terrain %s in base.db3' % terrain)
        terrain_id = row[0]
        existing = db.execute('SELECT id FROM TerrainLayers WHERE terrain = ? AND layer = ?', (terrain_id, layer)).fetchone()
        if existing:
            db.execute('UPDATE TerrainLayers SET name = ?, albedo = ?, normal = ?, tiling = ? WHERE id = ?',
                       (name, albedo_file, normal_file, tiling, existing[0]))
        else:
            db.execute('INSERT INTO TerrainLayers (terrain, layer, name, albedo, normal, tiling) VALUES (?, ?, ?, ?, ?, ?)',
                       (terrain_id, layer, name, albedo_file, normal_file, tiling))
        db.commit()
        print('TerrainLayers: terrain %s layer %d = %s, tiling %g m' % (terrain, layer, name, tiling))
    finally:
        db.close()


def main():
    argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    parser = argparse.ArgumentParser(prog='pack_terrain_layer.py')
    parser.add_argument('--name', required=True, help='layer name: files <name>_albedo.dds and <name>_normal.dds')
    parser.add_argument('--albedo', required=True, help='base color, sRGB')
    parser.add_argument('--normal', required=True, help='tangent-space normal map')
    parser.add_argument('--normal-convention', choices=['gl', 'dx'], default='gl', help='gl: green up (glTF, Poly Haven nor_gl)')
    parser.add_argument('--roughness', required=True, help='roughness map, file[:channel]')
    parser.add_argument('--height', required=True, help='height (displacement) map')
    parser.add_argument('--size', type=int, default=2048)
    parser.add_argument('--layer', type=int, choices=range(8), help='TerrainLayers.layer to update (0..7)')
    parser.add_argument('--tiling', type=float, help='meters per texture repeat for --layer')
    parser.add_argument('--terrain', default='Terrain', help='Terrain.name for --layer')
    args = parser.parse_args(argv)
    if args.layer is not None and not args.tiling:
        raise SystemExit('error: --layer needs --tiling')

    albedo = load(args.albedo, args.size)[..., :3]
    normal = load(args.normal, args.size)[..., :3].copy()
    if args.normal_convention == 'gl':
        normal[..., 1] = 1.0 - normal[..., 1]
    roughness_file, roughness_channel = split_channel(args.roughness, 0)
    roughness = load(roughness_file, args.size)[..., roughness_channel]
    height = normalized_height(load(args.height, args.size)[..., 0])

    albedo_path = os.path.join(LAYERS_DIR, args.name + '_albedo.dds')
    normal_path = os.path.join(LAYERS_DIR, args.name + '_normal.dds')
    # С готовыми мипами: TerrainMaterial берёт их из файлов, а не строит при каждом запуске
    dds.write_rgba8(albedo_path, np.concatenate([albedo, height[..., None]], axis=-1), srgb=True, mips=True)
    dds.write_rgba8(normal_path, np.concatenate([normal, roughness[..., None]], axis=-1), mips=True)

    if args.layer is not None:
        # Пути в базе — от каталога текстур, как у остальных строк
        update_layer(args.terrain, args.layer, args.name, os.path.relpath(albedo_path, 'Textures'),
                     os.path.relpath(normal_path, 'Textures'), args.tiling)


main()
