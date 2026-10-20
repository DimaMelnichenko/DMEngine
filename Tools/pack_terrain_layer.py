# Слой материала террейна из фото-текстур (Poly Haven и подобных): альбедо, нормаль, шероховатость и высота
# собираются в два файла формата слоя (TerrainMaterial, Textures\terrain\layers):
#   <имя>_albedo.dds — RGB альбедо в sRGB, как в фото (R8G8B8A8_UNORM_SRGB), A — высота для смешивания слоёв,
#                      растянутая на 0…1 по 1-му и 99-му процентилю (у разных материалов разный разброс disp-карт);
#   <имя>_normal.dds — RG нормаль в соглашении DirectX (у карты OpenGL — nor_gl — зелёный канал переворачивается;
#                      Z шейдер не читает), B — затенение окружением (AO: карта --ao, по карте высоты --ao-from-height
#                      или 1 — без затенения), A — шероховатость.
# Файлы — с полной цепочкой мипов (альбедо фильтруется в линейном): движок не строит их при загрузке.
# С --layer обновляет строку TerrainLayers террейна (имя, файлы, повтор в метрах). Картинки читает Blender (JPG, PNG,
# EXR), поэтому запуск — через него, из корня проекта. Файл внутри архива — «архив.zip:путь внутри»:
#   blender -b --factory-startup --python Tools/pack_terrain_layer.py -- --name leafy_grass
#       --albedo DownloadResources/leafy_grass_2k.gltf.zip:textures/leafy_grass_diff_2k.jpg
#       --normal DownloadResources/leafy_grass_2k.gltf.zip:textures/leafy_grass_nor_gl_2k.jpg --normal-convention gl
#       --roughness DownloadResources/leafy_grass_2k.gltf.zip:textures/leafy_grass_arm_2k.jpg:g
#       --height DownloadResources/polyhaven/leafy_grass_disp_2k.exr --layer 0 --tiling 2
# Шероховатость — «файл:канал» (r, g, b, a; по умолчанию r): у ARM Poly Haven — g. AO — так же (у ARM — r); карты нет —
# --ao-from-height <перепад, м> --tiling <м>: горизонт по бесшовной карте высоты в радиусе --ao-radius. Размер — --size (2048).
# Пятна с прозрачностью (декаль Megascans «Moss Patches»: цвет и альфа в _B-O) — --coverage-from-alpha: высота × альфа
# (при смешивании по высоте слой виден только пятнами, в дырах — соседний слой), цвет в дырах — цвет пятен по соседству
# (заливка pull-push: без неё мипы темнеют к краю пятна от чёрного под нулевой альфой).
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


def fill_holes(rgb, alpha):
    """Цвет под нулевой альфой — средний цвет покрытого вокруг (pull-push: сумма с весом альфы по уровням вниз, затем
    вверх — где у уровня нет веса, цвет родителя); покрытое остаётся, край смешивается по альфе"""
    levels = [(rgb * alpha[..., None], alpha.copy())]
    while levels[-1][1].shape[0] > 1:
        c, w = levels[-1]
        h, wd = c.shape[0] // 2, c.shape[1] // 2
        levels.append((c.reshape(h, 2, wd, 2, 3).sum(axis=(1, 3)), w.reshape(h, 2, wd, 2).sum(axis=(1, 3))))
    c, w = levels[-1]
    fill = c / np.maximum(w, 1e-6)[..., None]
    for c, w in reversed(levels[:-1]):
        parent = np.repeat(np.repeat(fill, 2, axis=0), 2, axis=1)
        # Сгладить повтор (иначе в дырах — квадраты родительских текселей): среднее 3 × 3 по кругу (текстура бесшовная)
        parent = sum(np.roll(parent, (dy, dx), axis=(0, 1)) for dy in (-1, 0, 1) for dx in (-1, 0, 1)) / 9.0
        own = c / np.maximum(w, 1e-6)[..., None]
        fill = np.where((w > 1e-6)[..., None], own, parent)
    return rgb * alpha[..., None] + fill * (1.0 - alpha[..., None])


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


def height_ao(height, relief, radius, directions=16, steps=12):
    """Затенение окружением по бесшовной карте высоты (0…1 → relief текселей перепада): в каждом из directions направлений
    — наибольший подъём горизонта в радиусе radius текселей, видимая доля неба с косинусным весом — cos² угла горизонта"""
    h = height.astype(np.float32) * relief
    visibility = np.zeros_like(h)
    for d in range(directions):
        angle = 2.0 * np.pi * (d + 0.5) / directions
        slope = np.zeros_like(h)
        seen = set()
        for s in range(1, steps + 1):
            distance = radius * (s / steps) ** 1.5     # чаще у точки: мелкие камни рядом
            offset = (int(round(np.sin(angle) * distance)), int(round(np.cos(angle) * distance)))
            if offset == (0, 0) or offset in seen:
                continue
            seen.add(offset)
            neighbour = np.roll(h, (-offset[0], -offset[1]), axis=(0, 1))
            slope = np.maximum(slope, (neighbour - h) / np.hypot(*offset))
        visibility += 1.0 / (1.0 + slope * slope)
    return visibility / directions


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
    parser.add_argument('--ao', help='ambient occlusion map, file[:channel] (ARM of Poly Haven - :r)')
    parser.add_argument('--ao-from-height', type=float, metavar='RELIEF',
                        help='no AO map: AO from the height map, RELIEF - metres of its full range (needs --tiling)')
    parser.add_argument('--ao-radius', type=float, default=0.15, help='horizon search radius for --ao-from-height, m')
    parser.add_argument('--coverage-from-alpha', action='store_true',
                        help='the albedo alpha is coverage (patches): height x alpha, holes filled with the patch colour')
    parser.add_argument('--size', type=int, default=2048)
    parser.add_argument('--layer', type=int, choices=range(12), help='TerrainLayers.layer to update (0..11)')
    parser.add_argument('--tiling', type=float, help='meters per texture repeat for --layer')
    parser.add_argument('--terrain', default='Terrain', help='Terrain.name for --layer')
    args = parser.parse_args(argv)
    if args.layer is not None and not args.tiling:
        raise SystemExit('error: --layer needs --tiling')
    if args.ao_from_height and not args.tiling:
        raise SystemExit('error: --ao-from-height needs --tiling (metres per texture repeat)')

    albedo_rgba = load(args.albedo, args.size)
    albedo = albedo_rgba[..., :3]
    normal = load(args.normal, args.size)[..., :3].copy()
    if args.normal_convention == 'gl':
        normal[..., 1] = 1.0 - normal[..., 1]
    roughness_file, roughness_channel = split_channel(args.roughness, 0)
    roughness = load(roughness_file, args.size)[..., roughness_channel]
    height = normalized_height(load(args.height, args.size)[..., 0])
    if args.coverage_from_alpha:
        coverage = np.clip(albedo_rgba[..., 3], 0.0, 1.0)
        print('coverage mean %.3f' % float(coverage.mean()))
        height = height * coverage
        albedo = fill_holes(albedo, coverage)
    if args.ao:
        ao_file, ao_channel = split_channel(args.ao, 0)
        occlusion = load(ao_file, args.size)[..., ao_channel]
    elif args.ao_from_height:
        texel = args.tiling / args.size
        occlusion = height_ao(height, args.ao_from_height / texel, args.ao_radius / texel)
    else:
        occlusion = np.ones_like(height)
    print('AO mean %.3f, 5%% %.3f' % (float(occlusion.mean()), float(np.percentile(occlusion, 5))))
    normal[..., 2] = occlusion

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
