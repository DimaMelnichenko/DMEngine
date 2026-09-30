"""Сравнение двух снимков кадра по пикселям — правило «картинка совпадает до пикселя» из docs/d3d12.md §4.

    python Tools/compare_frames.py a.png b.png [--tolerance 0] [--diff diff.png] [--quiet]
    python Tools/compare_frames.py папка_a папка_b [--tolerance 0] [--diff папка_diff]

Печатает размер, число отличающихся пикселей (всего и сверх допуска), наибольшую и среднюю разницу по каналам 0…255.
--tolerance N — разница до N включительно по каждому каналу считается совпадением (±1 — округление, B8 плана).
--diff файл.png — карта расхождений: серым — исходник a, красным — пиксели сверх допуска (ярче — больше разница).
Две папки — сравниваются одноимённые .png в них (снимки серии камер), итог по каждому и общий.
Код выхода 0 — все пары совпали в пределах допуска, 1 — есть расхождения, 2 — ошибка (нет файла, разные размеры).
Нужны numpy и Pillow. Сообщения скрипта — ASCII (правило Tools/).
"""
import argparse
import os
import sys

import numpy as np
from PIL import Image


def load(path):
    try:
        return np.asarray(Image.open(path).convert('RGB'), dtype=np.int16)
    except (OSError, ValueError) as error:
        raise SystemExit('error: cannot read %s: %s' % (path, error))


def compare(path_a, path_b, tolerance, diff_path, quiet):
    """True, если снимки совпали в пределах допуска."""
    a = load(path_a)
    b = load(path_b)
    if a.shape != b.shape:
        print('%s: size differs, %dx%d vs %dx%d' % (os.path.basename(path_a), a.shape[1], a.shape[0], b.shape[1], b.shape[0]))
        return None
    delta = np.abs(a - b)                     # (h, w, 3)
    per_pixel = delta.max(axis=2)             # наибольшая разница среди каналов
    differing = int((per_pixel > 0).sum())
    beyond = int((per_pixel > tolerance).sum())
    total = per_pixel.size
    name = '%s vs %s' % (os.path.basename(path_a), os.path.basename(path_b))
    if differing == 0:
        if not quiet:
            print('%s: identical, %dx%d' % (name, a.shape[1], a.shape[0]))
    else:
        print('%s: %dx%d, differing pixels %d (%.3f%%), beyond tolerance %d: %d (%.3f%%), max diff %d, mean diff %.4f'
              % (name, a.shape[1], a.shape[0], differing, 100.0 * differing / total, tolerance, beyond,
                 100.0 * beyond / total, int(per_pixel.max()), float(delta.mean())))
    if diff_path:
        gray = (a.mean(axis=2) * 0.5).astype(np.uint8)
        image = np.stack([gray, gray, gray], axis=2)
        mask = per_pixel > tolerance
        strength = np.clip(per_pixel[mask] * 4 + 96, 0, 255).astype(np.uint8)
        image[mask] = 0
        image[mask, 0] = strength
        os.makedirs(os.path.dirname(os.path.abspath(diff_path)), exist_ok=True)
        Image.fromarray(image).save(diff_path)
    return beyond == 0


def main():
    parser = argparse.ArgumentParser(description='Compare two screenshots (or two folders of screenshots) pixel by pixel')
    parser.add_argument('a')
    parser.add_argument('b')
    parser.add_argument('--tolerance', type=int, default=0, help='per-channel difference treated as equal (0..255)')
    parser.add_argument('--diff', help='difference map: a .png for two files, a folder for two folders')
    parser.add_argument('--quiet', action='store_true', help='print only the pairs that differ')
    args = parser.parse_args()

    if os.path.isdir(args.a) and os.path.isdir(args.b):
        names = sorted(n for n in os.listdir(args.a) if n.lower().endswith('.png'))
        if not names:
            raise SystemExit('error: no .png in %s' % args.a)
        results = []
        for n in names:
            other = os.path.join(args.b, n)
            if not os.path.exists(other):
                print('%s: missing in %s' % (n, args.b))
                results.append(None)
                continue
            diff = os.path.join(args.diff, os.path.splitext(n)[0] + '_diff.png') if args.diff else None
            results.append(compare(os.path.join(args.a, n), other, args.tolerance, diff, args.quiet))
        if any(r is None for r in results):
            return 2
        passed = sum(1 for r in results if r)
        print('%d of %d match within tolerance %d' % (passed, len(results), args.tolerance))
        return 0 if passed == len(results) else 1

    if os.path.isdir(args.a) != os.path.isdir(args.b):
        raise SystemExit('error: compare two files or two folders')
    result = compare(args.a, args.b, args.tolerance, args.diff, args.quiet)
    if result is None:
        return 2
    return 0 if result else 1


if __name__ == '__main__':
    sys.exit(main())
