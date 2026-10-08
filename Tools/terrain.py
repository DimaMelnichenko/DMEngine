"""Высота террейна в точке мира для инструментов, которые ставят модели на землю (import_gltf.py, terrain_edit.py).

Раскладка карты — как у terrainHeight в Shaders/terrain_height.sh и docs/terrain.md: строка r — z = W − (r + 0,5)·cell,
столбец c — x = (c + 0,5)·cell, cell — Terrain.width_multiplier. Рельеф — после эрозии движка (каталог eroded рядом с
картой высот: height.dds, R32, метры), без неё — исходная карта: значение × height_multiplier + height_offset.
"""
import os

import numpy as np

import dds


def sample(height, world_x, world_z, cell):
    """Высота карты в точке мира, билинейно (строка r ↔ z = W − (r + 0,5)·cell, столбец c ↔ x = (c + 0,5)·cell)"""
    rows, cols = height.shape
    world = rows * cell
    c = np.clip(world_x / cell - 0.5, 0.0, cols - 1.001)
    r = np.clip((world - world_z) / cell - 0.5, 0.0, rows - 1.001)
    c0, r0 = int(c), int(r)
    fc, fr = c - c0, r - r0
    top = height[r0, c0] + (height[r0, c0 + 1] - height[r0, c0]) * fc
    bottom = height[r0 + 1, c0] + (height[r0 + 1, c0 + 1] - height[r0 + 1, c0]) * fc
    return top + (bottom - top) * fr


def load_level_terrain(db, level_id, root):
    """Карта высот террейна уровня в метрах и шаг её текселя: (heights, cell) или None, если террейна у уровня нет.
    db — sqlite3 на base.db3, root — корень проекта (Textures\\ относительно него)"""
    row = db.execute('SELECT t.heightmap, t.height_multiplier, t.height_offset, t.width_multiplier '
                     'FROM Levels l JOIN Terrain t ON t.id = l.terrain WHERE l.id = ?', (level_id,)).fetchone()
    if row is None:
        return None
    heightmap, multiplier, offset, cell = row
    texture = db.execute('SELECT file FROM Textures WHERE name = ?', (heightmap,)).fetchone()
    if texture is None:
        raise SystemExit('error: heightmap texture %s is not found in Textures' % heightmap)
    path = os.path.join(root, 'Textures', texture[0].replace('\\', os.sep))
    eroded = eroded_directory(path)
    if os.path.exists(os.path.join(eroded, 'height.dds')):
        return dds.read_r32f(os.path.join(eroded, 'height.dds')), cell
    return dds.read_r16(path) * multiplier + offset, cell


def eroded_directory(heightmap_path):
    """Каталог эрозии движка рядом с картой высот (CDLODTerrain::erode): height, flow, wear, deposition, talus — R32"""
    return os.path.join(os.path.dirname(heightmap_path), 'eroded')
