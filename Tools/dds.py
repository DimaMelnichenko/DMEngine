# DDS без сжатия для данных движка из сценариев Tools/: запись RGBA8 (слои и splat-карта террейна, маски
# расстановки), R16 (карта высот) и R32_FLOAT (карты эрозии), чтение R16 и R32_FLOAT. Заголовок DDS с расширением
# DX10, как у texconv; движок читает их DirectXTex.
# Картинка — массив numpy высота × ширина (× 4 у RGBA, значения 0…1), строки сверху вниз.
import os
import struct

import numpy as np

DXGI_FORMAT_R32G32B32A32_FLOAT = 2
DXGI_FORMAT_R32_FLOAT = 41
DXGI_FORMAT_R16_UNORM = 56
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
    """rgba — картинка или массив срезов (срезы × высота × ширина × 4) — массив текстур (splat-карта террейна).
    srgb — RGB уже в sRGB (альбедо): формат R8G8B8A8_UNORM_SRGB, иначе UNORM. Альфа всегда линейная.
    mips — записать полную цепочку мипов (mip_chain): движку не нужно строить их при загрузке"""
    slices = rgba if rgba.ndim == 4 else rgba[None]
    chains = [mip_chain(image, srgb) if mips else [image] for image in slices]
    levels = chains[0]
    height, width = slices.shape[1:3]
    flags = 0x100F | (0x20000 if len(levels) > 1 else 0)            # + DDSD_MIPMAPCOUNT
    caps = 0x1000 | (0x400008 if len(levels) > 1 else 0)             # TEXTURE (+ MIPMAP, COMPLEX)
    header = struct.pack('<4sIIIIIII44sIIIIIIIIIIIII',
        b'DDS ', 124, flags, height, width, width * 4, 0, len(levels), b'\0' * 44,
        32, 0x4, struct.unpack('<I', b'DX10')[0], 0, 0, 0, 0, 0,
        caps, 0, 0, 0, 0)
    fmt = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB if srgb else DXGI_FORMAT_R8G8B8A8_UNORM
    dx10 = struct.pack('<IIIII', fmt, 3, 0, len(slices), 0)   # TEXTURE2D, массив из len(slices) картинок
    os.makedirs(os.path.dirname(path) or '.', exist_ok=True)
    with open(path, 'wb') as f:
        f.write(header)
        f.write(dx10)
        for chain in chains:   # в DDS — по срезу со всеми его мипами
            for level in chain:
                f.write(to_bytes(level).tobytes())
    print('written', path, '%dx%d' % (width, height), 'sRGB' if srgb else 'UNORM', '%d mips' % len(levels),
          '%d slices' % len(slices) if len(slices) > 1 else '')


def write_single(path, pixels, fmt, bytes_per_pixel):
    """Одна картинка без мипов: pixels — уже нужного типа (uint16, float32), высота × ширина (× каналы)"""
    height, width = pixels.shape[:2]
    header = struct.pack('<4sIIIIIII44sIIIIIIIIIIIII',
        b'DDS ', 124, 0x100F, height, width, width * bytes_per_pixel, 0, 0, b'\0' * 44,
        32, 0x4, struct.unpack('<I', b'DX10')[0], 0, 0, 0, 0, 0,
        0x1000, 0, 0, 0, 0)
    dx10 = struct.pack('<IIIII', fmt, 3, 0, 1, 0)
    os.makedirs(os.path.dirname(path) or '.', exist_ok=True)
    with open(path, 'wb') as f:
        f.write(header)
        f.write(dx10)
        f.write(np.ascontiguousarray(pixels).tobytes())
    print('written', path, '%dx%d' % (width, height))


def write_r16(path, values):
    """Карта высот: values 0…1 → R16_UNORM"""
    write_single(path, np.clip(np.rint(values * 65535.0), 0, 65535).astype('<u2'), DXGI_FORMAT_R16_UNORM, 2)


def write_r32f(path, values):
    """Данные без нормировки (карты эрозии в метрах и м²) → R32_FLOAT"""
    write_single(path, values.astype('<f4'), DXGI_FORMAT_R32_FLOAT, 4)


def write_rgba32f(path, values):
    """Четыре канала данных (высота × ширина × 4) → R32G32B32A32_FLOAT"""
    write_single(path, values.astype('<f4'), DXGI_FORMAT_R32G32B32A32_FLOAT, 16)


def read_single(path, dtype):
    with open(path, 'rb') as f:
        data = f.read()
    height, width = struct.unpack('<II', data[12:20])
    offset = 4 + 124 + (20 if data[84:88] == b'DX10' else 0)
    return np.frombuffer(data, dtype=dtype, count=width * height, offset=offset).reshape(height, width)


def read_r16(path):
    return read_single(path, '<u2') / 65535.0


def read_r32f(path):
    return read_single(path, '<f4').astype(np.float64)
