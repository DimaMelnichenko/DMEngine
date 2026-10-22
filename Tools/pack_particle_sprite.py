# Спрайт частицы из фото на ровном тёмном фоне (всплеск, клочок пены): фон — в прозрачность, текстура
# Textures\particles\<имя>.dds (R8G8B8A8_UNORM, данные, с цепочкой мипов) и строка Textures. Несколько фото — атлас кадров
# в ряд (ширина — кадров × высота): частица берёт кадр по своему зерну. Частица формы froth с этим спрайтом (колонка
# sprite строки ParticleEmitters) берёт из него покрытие, рельеф и плотность, а свет и искры считает сама
# (Shaders/particles.ps, docs/particles.md).
#   python Tools/pack_particle_sprite.py DownloadResources/<фото> [<фото> ...] --name water_splash [--size 512] [--db base.db3]
# Фон — медиана цвета по краям картинки; пиксель — проекция на ось от фона к самому светлому (верхние --white-percent %
# по яркости): 0 — фон, 1 — белое. Покрытие — плавный порог этой проекции (--alpha-from … --alpha-to), плотность — она
# же без порога. Картинка обрезается по покрытию с полем --margin, дополняется до квадрата, к краю покрытие гаснет (край
# спрайта прозрачный при любом повороте). Каналы: R — покрытие, G, B — нормаль по размытому покрытию (0,5 + 0,5 · n),
# A — плотность. Нужны numpy и Pillow. Сообщения — ASCII (правило Tools/).
import argparse
import os
import sqlite3
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dds  # noqa: E402


def luminance(rgb):
    return 0.2126 * rgb[..., 0] + 0.7152 * rgb[..., 1] + 0.0722 * rgb[..., 2]


def smoothstep(a, b, x):
    t = np.clip((x - a) / (b - a), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def blur(values, sigma):
    """Гауссово размытие (края — нулём: вокруг спрайта пусто)"""
    radius = int(np.ceil(3.0 * sigma))
    kernel = np.exp(-0.5 * (np.arange(-radius, radius + 1) / sigma) ** 2)
    kernel /= kernel.sum()
    result = values
    for axis in (0, 1):
        padded = np.pad(result, [(radius, radius) if a == axis else (0, 0) for a in (0, 1)])
        result = sum(k * np.take(padded, np.arange(i, i + values.shape[axis]), axis=axis) for i, k in enumerate(kernel))
    return result


def main():
    parser = argparse.ArgumentParser(description='Particle sprite from a photo on an even dark background')
    parser.add_argument('images', nargs='+')
    parser.add_argument('--name', required=True, help='Textures row name (and the file name)')
    parser.add_argument('--size', type=int, default=512, help='sprite side, pixels (power of two)')
    parser.add_argument('--white-percent', type=float, default=2.0, help='brightest share of pixels taken as white')
    parser.add_argument('--alpha-from', type=float, default=0.06, help='coverage starts at this share between background and white')
    parser.add_argument('--alpha-to', type=float, default=0.45, help='and is full at this share')
    parser.add_argument('--margin', type=float, default=0.04, help='crop margin around the coverage, share of the side')
    parser.add_argument('--bump', type=float, default=3.0, help='normal steepness: coverage height per pixel step')
    parser.add_argument('--blur', type=float, default=1.5, help='coverage blur for the normal, pixels')
    parser.add_argument('--db', help='base.db3: insert or update the Textures row')
    args = parser.parse_args()

    frames = [frame(path, args) for path in args.images]
    rgba = np.concatenate(frames, axis=1)
    out = os.path.join('Textures', 'particles', args.name + '.dds')
    os.makedirs(os.path.dirname(out), exist_ok=True)
    dds.write_rgba8(out, rgba, srgb=False, mips=True)
    print('wrote %s (%d frames of %dx%d)' % (out, len(frames), args.size, args.size))

    if args.db:
        file = os.path.relpath(out, 'Textures').replace('/', '\\')
        db = sqlite3.connect(args.db)
        row = db.execute('SELECT id FROM Textures WHERE name = ?', (args.name,)).fetchone()
        if row:
            db.execute("UPDATE Textures SET file = ?, generate_mipmap = 0, sRGB = 0, compression = 'none' WHERE id = ?", (file, row[0]))
        else:
            db.execute("INSERT INTO Textures (name, file, generate_mipmap, sRGB, compression) VALUES (?, ?, 0, 0, 'none')",
                       (args.name, file))
        db.commit()
        db.close()
        print('Textures row %s -> %s' % (args.name, file))


def frame(path, args):
    """Кадр спрайта из фото: size × size × 4 (покрытие, нормаль, плотность)"""
    rgb = np.asarray(Image.open(path).convert('RGB')).astype(np.float32) / 255.0
    border = np.concatenate([rgb[0], rgb[-1], rgb[:, 0], rgb[:, -1]])
    background = np.median(border, axis=0)
    lum = luminance(rgb)
    white = rgb[lum >= np.percentile(lum, 100.0 - args.white_percent)].mean(axis=0)
    axis = white - background
    density = np.clip(((rgb - background) @ axis) / float(axis @ axis), 0.0, 1.0)
    coverage = smoothstep(args.alpha_from, args.alpha_to, density)
    print('%s: background %s, white %s, covered share %.3f' % (os.path.basename(path), np.round(background * 255).astype(int).tolist(),
                                                              np.round(white * 255).astype(int).tolist(), float((coverage > 0.5).mean())))

    # Обрезка по покрытию с полем, до квадрата
    rows = np.where(coverage.max(axis=1) > 0.05)[0]
    cols = np.where(coverage.max(axis=0) > 0.05)[0]
    if len(rows) == 0:
        raise SystemExit('error: nothing but background')
    top, bottom, left, right = rows[0], rows[-1] + 1, cols[0], cols[-1] + 1
    side = max(bottom - top, right - left)
    side = int(side * (1.0 + 2.0 * args.margin))
    cy, cx = (top + bottom) // 2, (left + right) // 2
    canvas = np.zeros((side, side, 2), np.float32)
    y0, x0 = cy - side // 2, cx - side // 2
    sy0, sx0 = max(y0, 0), max(x0, 0)
    sy1, sx1 = min(y0 + side, rgb.shape[0]), min(x0 + side, rgb.shape[1])
    canvas[sy0 - y0:sy1 - y0, sx0 - x0:sx1 - x0, 0] = coverage[sy0:sy1, sx0:sx1]
    canvas[sy0 - y0:sy1 - y0, sx0 - x0:sx1 - x0, 1] = density[sy0:sy1, sx0:sx1]
    image = np.stack([np.asarray(Image.fromarray(canvas[..., c]).resize((args.size, args.size), Image.LANCZOS)) for c in (0, 1)], axis=-1)
    image = np.clip(image, 0.0, 1.0)
    # К краю — прозрачно: спрайт повёрнут как угодно, его углы выходят за круг
    grid = (np.arange(args.size) + 0.5) / args.size * 2.0 - 1.0
    radius = np.sqrt(grid[None, :] ** 2 + grid[:, None] ** 2)
    fade = 1.0 - smoothstep(0.85, 1.0, radius)
    cover, dense = image[..., 0] * fade, image[..., 1]

    height = blur(cover, args.blur) * args.bump
    dx = 0.5 * (np.roll(height, -1, axis=1) - np.roll(height, 1, axis=1))
    dy = 0.5 * (np.roll(height, -1, axis=0) - np.roll(height, 1, axis=0))
    normal = np.stack([-dx, -dy, np.ones_like(dx)], axis=-1)
    normal /= np.linalg.norm(normal, axis=-1, keepdims=True)
    print('  coverage mean %.3f' % float(cover.mean()))
    return np.stack([cover, 0.5 + 0.5 * normal[..., 0], 0.5 + 0.5 * normal[..., 1], dense], axis=-1)


if __name__ == '__main__':
    main()
