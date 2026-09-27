# DDS без сжатия для данных движка из сценариев Tools/: запись RGBA8 (слои и splat-карта террейна, маски
# расстановки) и чтение R16 (карта высот). Заголовок DDS с расширением DX10, как у texconv; движок читает их DirectXTex.
# Картинка — массив numpy высота × ширина × 4, значения 0…1, строки сверху вниз.
import os
import struct

import numpy as np

DXGI_FORMAT_R8G8B8A8_UNORM = 28
DXGI_FORMAT_R8G8B8A8_UNORM_SRGB = 29


def to_bytes(values):
    return np.clip(np.rint(values * 255.0), 0, 255).astype(np.uint8)


def linear_to_srgb(values):
    # Кривая sRGB (IEC 61966-2-1), как у формата *_UNORM_SRGB
    values = np.clip(values, 0.0, 1.0)
    return np.where(values <= 0.0031308, values * 12.92, 1.055 * np.power(values, 1.0 / 2.4) - 0.055)


def write_rgba8(path, rgba, srgb=False):
    """srgb — RGB уже в sRGB (альбедо): формат R8G8B8A8_UNORM_SRGB, иначе UNORM. Альфа всегда линейная"""
    height, width = rgba.shape[:2]
    header = struct.pack('<4sIIIIIII44sIIIIIIIIIIIII',
        b'DDS ', 124, 0x100F, height, width, width * 4, 0, 0, b'\0' * 44,
        32, 0x4, struct.unpack('<I', b'DX10')[0], 0, 0, 0, 0, 0,
        0x1000, 0, 0, 0, 0)
    fmt = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB if srgb else DXGI_FORMAT_R8G8B8A8_UNORM
    dx10 = struct.pack('<IIIII', fmt, 3, 0, 1, 0)   # TEXTURE2D, массив из одной картинки
    os.makedirs(os.path.dirname(path) or '.', exist_ok=True)
    with open(path, 'wb') as f:
        f.write(header)
        f.write(dx10)
        f.write(to_bytes(rgba).tobytes())
    print('written', path, '%dx%d' % (width, height), 'sRGB' if srgb else 'UNORM')


def read_r16(path):
    with open(path, 'rb') as f:
        data = f.read()
    height, width = struct.unpack('<II', data[12:20])
    offset = 4 + 124 + (20 if data[84:88] == b'DX10' else 0)
    pixels = np.frombuffer(data, dtype='<u2', count=width * height, offset=offset)
    return pixels.reshape(height, width) / 65535.0
