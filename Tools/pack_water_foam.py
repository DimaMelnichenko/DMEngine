# Текстура пены воды из фото или картинки без альфы (белая пена на воде): маска пены и нормаль по ней — в один файл
# Textures\water\foam.dds (R8G8B8A8_UNORM, данные, с цепочкой мипов). Шейдер воды (water_shading.sh) несёт маску течением,
# как рябь, и порогом по доле пены оставляет где только узлы кружева, где сплошную пену (docs/water.md, «Пена»).
#   python Tools/pack_water_foam.py DownloadResources/foam.png [--out Textures/water/foam.dds] [--db base.db3]
# Маска: цвет просвета (тёмные --gap-percent % пикселей по яркости) — 0, цвет пены (светлые) — 1, пиксель — проекция на
# ось между ними: она учитывает и яркость, и оттенок (просветы зеленоватые, пена белая). Картинку, которая не повторяется
# без шва, сценарий делает бесшовной: у краёв — смешение с копией, сдвинутой на полкартинки, с сохранением разброса
# (деление на sqrt(w^2 + (1 - w)^2), как у плиток ряби), полоса --seam-band пикселей; --check-seam печатает разрыв на
# краях до и после. Каналы: R — маска (толщина пены 0…1), G, B — нормаль по размытой маске (0,5 + 0,5 · n, x и y в
# координатах картинки, --bump — крутизна), A — 1. С --db — строка water_foam в Textures (sRGB 0, без сжатия, свои мипы).
# Нужны numpy и Pillow. Сообщения — ASCII (правило Tools/).
import argparse
import os
import sqlite3
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dds  # noqa: E402

TEXTURE_NAME = 'water_foam'


def luminance(rgb):
    return 0.2126 * rgb[..., 0] + 0.7152 * rgb[..., 1] + 0.0722 * rgb[..., 2]


def seam_gap(values):
    """Средний разрыв через край картинки против среднего шага между соседями внутри"""
    across = 0.5 * (np.abs(values[0] - values[-1]).mean() + np.abs(values[:, 0] - values[:, -1]).mean())
    inside = 0.5 * (np.abs(np.diff(values, axis=0)).mean() + np.abs(np.diff(values, axis=1)).mean())
    return across, inside


def make_seamless(mask, band):
    """Смешение у краёв с копией, сдвинутой на полкартинки: на краю — копия (её края — середина картинки, через край
    она непрерывна), внутри — картинка; разброс вокруг среднего сохраняется"""
    h, w = mask.shape
    shifted = np.roll(mask, (h // 2, w // 2), axis=(0, 1))

    def ramp(n):
        distance = np.minimum(np.arange(n), np.arange(n)[::-1]).astype(np.float32)
        t = np.clip(distance / band, 0.0, 1.0)
        return t * t * (3.0 - 2.0 * t)

    weight = np.outer(ramp(h), ramp(w))
    mean = mask.mean()
    blended = weight * (mask - mean) + (1.0 - weight) * (shifted - mean)
    return np.clip(mean + blended / np.sqrt(weight * weight + (1.0 - weight) ** 2), 0.0, 1.0)


def blur(values, sigma):
    """Гауссово размытие с повтором через край (картинка бесшовная)"""
    radius = int(np.ceil(3.0 * sigma))
    kernel = np.exp(-0.5 * (np.arange(-radius, radius + 1) / sigma) ** 2)
    kernel /= kernel.sum()
    result = values
    for axis in (0, 1):
        result = sum(k * np.roll(result, offset, axis=axis) for offset, k in zip(range(-radius, radius + 1), kernel))
    return result


def main():
    parser = argparse.ArgumentParser(description='Water foam texture: foam mask and normal from an image without alpha')
    parser.add_argument('image')
    parser.add_argument('--out', default=os.path.join('Textures', 'water', 'foam.dds'))
    parser.add_argument('--gap-percent', type=float, default=20.0, help='darkest share of pixels taken as the gap color')
    parser.add_argument('--foam-percent', type=float, default=20.0, help='brightest share of pixels taken as the foam color')
    parser.add_argument('--seam-band', type=int, default=128, help='seam blend band at the edges, pixels (0 - image is seamless)')
    parser.add_argument('--bump', type=float, default=4.0, help='normal steepness: mask height per pixel step')
    parser.add_argument('--blur', type=float, default=1.5, help='mask blur for the normal, pixels')
    parser.add_argument('--db', help='base.db3: insert or update Textures row ' + TEXTURE_NAME)
    args = parser.parse_args()

    rgb = np.asarray(Image.open(args.image).convert('RGB')).astype(np.float32) / 255.0
    lum = luminance(rgb)
    gap = rgb[lum <= np.percentile(lum, args.gap_percent)].mean(axis=0)
    foam = rgb[lum >= np.percentile(lum, 100.0 - args.foam_percent)].mean(axis=0)
    axis = foam - gap
    mask = np.clip(((rgb - gap) @ axis) / float(axis @ axis), 0.0, 1.0)
    print('gap color %s, foam color %s, foam share (mask > 0.5) %.2f' % (
        np.round(gap * 255).astype(int).tolist(), np.round(foam * 255).astype(int).tolist(), float((mask > 0.5).mean())))

    before = seam_gap(mask)
    if args.seam_band > 0:
        mask = make_seamless(mask, args.seam_band)
    after = seam_gap(mask)
    print('seam: across the edge %.3f (inside %.3f) -> %.3f (inside %.3f)' % (before[0], before[1], after[0], after[1]))

    height = blur(mask, args.blur) * args.bump
    dx = 0.5 * (np.roll(height, -1, axis=1) - np.roll(height, 1, axis=1))
    dy = 0.5 * (np.roll(height, -1, axis=0) - np.roll(height, 1, axis=0))
    normal = np.stack([-dx, -dy, np.ones_like(dx)], axis=-1)
    normal /= np.linalg.norm(normal, axis=-1, keepdims=True)
    rgba = np.stack([mask, 0.5 + 0.5 * normal[..., 0], 0.5 + 0.5 * normal[..., 1], np.ones_like(mask)], axis=-1)
    dds.write_rgba8(args.out, rgba, srgb=False, mips=True)

    if args.db:
        file = os.path.relpath(args.out, 'Textures').replace('/', '\\')
        db = sqlite3.connect(args.db)
        row = db.execute('SELECT id FROM Textures WHERE name = ?', (TEXTURE_NAME,)).fetchone()
        if row:
            db.execute("UPDATE Textures SET file = ?, generate_mipmap = 0, sRGB = 0, compression = 'none' WHERE id = ?", (file, row[0]))
        else:
            db.execute("INSERT INTO Textures (name, file, generate_mipmap, sRGB, compression) VALUES (?, ?, 0, 0, 'none')",
                       (TEXTURE_NAME, file))
        db.commit()
        db.close()
        print('Textures row %s -> %s' % (TEXTURE_NAME, file))


if __name__ == '__main__':
    main()
