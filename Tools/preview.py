# Превью карт террейна для сценариев Tools/ — посмотреть рельеф и маски без запуска движка: запись PNG (zlib, без
# сторонних библиотек) и отмывка рельефа. Картинка — numpy высота × ширина × 3, значения 0…1, строки сверху вниз.
import os
import struct
import zlib

import numpy as np


def write_png(path, rgb):
    height, width = rgb.shape[:2]
    pixels = np.clip(np.rint(rgb * 255.0), 0, 255).astype(np.uint8)
    rows = b''.join(b'\0' + pixels[r].tobytes() for r in range(height))   # фильтр 0 у каждой строки

    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)

    os.makedirs(os.path.dirname(path) or '.', exist_ok=True)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n')
        f.write(chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)))
        f.write(chunk(b'IDAT', zlib.compress(rows, 6)))
        f.write(chunk(b'IEND', b''))
    print('written', path, '%dx%d' % (width, height))


def hillshade(height, cell, azimuth=315.0, elevation=45.0):
    """Освещённость рельефа 0…1 светом с азимута azimuth (от севера — верха картинки — по часовой) и высоты
    elevation, как Hillshade в ГИС; height — метры"""
    padded = np.pad(height, 1, mode='edge')
    dzdx = (padded[1:-1, 2:] - padded[1:-1, :-2]) / (2.0 * cell)
    dzdy = (padded[:-2, 1:-1] - padded[2:, 1:-1]) / (2.0 * cell)   # вверх по картинке — на север
    normal = np.stack([-dzdx, -dzdy, np.ones_like(height)], axis=-1)
    normal /= np.linalg.norm(normal, axis=-1, keepdims=True)
    az, el = np.radians(azimuth), np.radians(elevation)
    light = np.array([np.sin(az) * np.cos(el), np.cos(az) * np.cos(el), np.sin(el)])
    return np.clip(normal @ light, 0.0, 1.0)
