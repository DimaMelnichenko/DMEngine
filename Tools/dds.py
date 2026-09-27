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


def srgb_to_linear(values):
    values = np.clip(values, 0.0, 1.0)
    return np.where(values <= 0.04045, values / 12.92, np.power((values + 0.055) / 1.055, 2.4))


def mip_chain(rgba, srgb=False):
    """Полная цепочка мипов (фильтр box 2 × 2) до 1 × 1; у sRGB цвет усредняется в линейном, альфа — как есть"""
    color = srgb_to_linear(rgba[..., :3]) if srgb else rgba[..., :3]
    alpha = rgba[..., 3:]
    levels = [rgba]
    while color.shape[0] > 1 or color.shape[1] > 1:
        h, w = max(color.shape[0] // 2, 1), max(color.shape[1] // 2, 1)

        def box(values):
            v = values[:h * 2, :w * 2] if values.shape[0] > 1 and values.shape[1] > 1 else values
            sy, sx = v.shape[0] // h, v.shape[1] // w
            return v.reshape(h, sy, w, sx, v.shape[2]).mean(axis=(1, 3))

        color, alpha = box(color), box(alpha)
        levels.append(np.concatenate([linear_to_srgb(color) if srgb else color, alpha], axis=-1))
    return levels


def write_rgba8(path, rgba, srgb=False, mips=False):
    """srgb — RGB уже в sRGB (альбедо): формат R8G8B8A8_UNORM_SRGB, иначе UNORM. Альфа всегда линейная.
    mips — записать полную цепочку мипов (mip_chain): движку не нужно строить их при загрузке"""
    levels = mip_chain(rgba, srgb) if mips else [rgba]
    height, width = rgba.shape[:2]
    flags = 0x100F | (0x20000 if len(levels) > 1 else 0)            # + DDSD_MIPMAPCOUNT
    caps = 0x1000 | (0x400008 if len(levels) > 1 else 0)             # TEXTURE (+ MIPMAP, COMPLEX)
    header = struct.pack('<4sIIIIIII44sIIIIIIIIIIIII',
        b'DDS ', 124, flags, height, width, width * 4, 0, len(levels), b'\0' * 44,
        32, 0x4, struct.unpack('<I', b'DX10')[0], 0, 0, 0, 0, 0,
        caps, 0, 0, 0, 0)
    fmt = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB if srgb else DXGI_FORMAT_R8G8B8A8_UNORM
    dx10 = struct.pack('<IIIII', fmt, 3, 0, 1, 0)   # TEXTURE2D, массив из одной картинки
    os.makedirs(os.path.dirname(path) or '.', exist_ok=True)
    with open(path, 'wb') as f:
        f.write(header)
        f.write(dx10)
        for level in levels:
            f.write(to_bytes(level).tobytes())
    print('written', path, '%dx%d' % (width, height), 'sRGB' if srgb else 'UNORM', '%d mips' % len(levels))


def read_r16(path):
    with open(path, 'rb') as f:
        data = f.read()
    height, width = struct.unpack('<II', data[12:20])
    offset = 4 + 124 + (20 if data[84:88] == b'DX10' else 0)
    pixels = np.frombuffer(data, dtype='<u2', count=width * height, offset=offset)
    return pixels.reshape(height, width) / 65535.0
